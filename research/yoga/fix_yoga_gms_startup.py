"""Guarded Yoga-only GMS decoding fix. Writes external ESP config only with --apply.

No hardware register writes, APFS writes, driver replacement, or internal ESP use.
"""
from pathlib import Path
import copy
import datetime
import hashlib
import json
import os
import plistlib
import subprocess
import sys

ROOT = Path(__file__).resolve().parent
SNAP = ROOT/'installed-external-readonly-20261023-225755/EFI'
EXPECTED = 'ff453e2d08c8bad6ac74ad647b9063d191b4f119a1a31c0148f536e4d40ee4c6'
IDENT = 'com.xxxxx.driver.AppleIntelTGLGraphics'
BASE = '__ZN15IGMemoryManager12initSegmentsEv'
OLD = bytes.fromhex('44 0f b7 f0 41 81 e6 00 ff 00 00 49 c1 e6 11')
# mov r14,0x03c00000; nop*8. 0xFE means (14+1)*4 MiB, NOT 254*32 MiB.
NEW = bytes.fromhex('49 c7 c6 00 00 c0 03') + b'\x90'*8
APPLY = sys.argv[1:] == ['--apply']
assert not sys.argv[1:] or APPLY
stamp = datetime.datetime.now().strftime('%Y%m%d-%H%M%S')
OUT = ROOT/('yoga-gms60-'+('apply-' if APPLY else 'stage-')+stamp)
OUT.mkdir()

def run(*args):
    return subprocess.check_output(args,text=True,stderr=subprocess.STDOUT)

def sha(data):
    return hashlib.sha256(data).hexdigest()

def manifest(folder):
    return {str(p.relative_to(folder)):sha(p.read_bytes()) for p in folder.rglob('*') if p.is_file()}

original = (SNAP/'OC/config.plist').read_bytes()
assert sha(original) == EXPECTED
config = plistlib.loads(original)
new = copy.deepcopy(config)
patch = dict(Arch='x86_64', Base=BASE,
             Comment='Yoga 46A6 GMS FE=60MiB: fix task allocator underflow (25G241/TGL16.0.32)',
             Count=1, Enabled=True, Find=OLD, Identifier=IDENT, Limit=128,
             Mask=b'', MaxKernel='25.99.99', MinKernel='25.0.0', Replace=NEW,
             ReplaceMask=b'', Skip=0)
assert not any(p.get('Comment')==patch['Comment'] for p in new['Kernel']['Patch'])
new['Kernel']['Patch'].append(patch)
check = copy.deepcopy(new)
assert check['Kernel']['Patch'].pop() == patch and check == config
binary = ROOT/'cross-build/gpu-stack-assembled/Library/Extensions/AppleIntelTGLGraphics.kext/Contents/MacOS/AppleIntelTGLGraphics'
b = binary.read_bytes()
# This Mach-O maps __text VA == file offset; independently anchor exact function.
assert b[0xebc2:0xebc6] == bytes.fromhex('55 48 89 e5')
assert b.count(OLD)==1 and b.find(OLD)==0xec02
assert b[0xec02+len(OLD):0xec02+len(OLD)+7] == bytes.fromhex('48 c7 83 a0 00 00 00')
assert 0xec02-0xebc2+len(OLD)<=patch['Limit']
assert len(OLD)==len(NEW)==15
fixed = bytearray(b)
fixed[0xec02:0xec02+len(OLD)] = NEW
assert sum(x!=y for x,y in zip(b,fixed))==15
(OUT/'initSegments-original.bin').write_bytes(b[0xebc2:0xec76])
(OUT/'initSegments-patched.bin').write_bytes(fixed[0xebc2:0xec76])
for name in ['original','patched']:
    (OUT/('initSegments-'+name+'.txt')).write_text(run('objdump','-D','-b','binary','-m','i386:x86-64','--adjust-vma=0xebc2',str(OUT/('initSegments-'+name+'.bin'))))
payload = plistlib.dumps(new,sort_keys=False)
(OUT/'config.original.plist').write_bytes(original)
(OUT/'config.plist').write_bytes(payload)
validator = '/path/to/local-workspace/nvidia-macos-driver/_oc_stage/rel/Utilities/ocvalidate/ocvalidate.linux'
(OUT/'ocvalidate.txt').write_text(run(validator,str(OUT/'config.plist')))
aperture = 256*1024*1024
old_start = 254*32*1024*1024
new_start = ((254-240)+1)*4*1024*1024
assert new_start == 60*1024*1024 < aperture < old_start
# withRange first creates [0, aperture-1], then reserves [0, stolen-1].
# Old stolen boundary exceeds that allocator: impossible reservation -> NULL task.
assert old_start-1 > aperture-1 and new_start-1 < aperture-1
report = dict(applied=False, external_disk_writes=False, apfs_writes=False,
              original_config_sha256=EXPECTED, new_config_sha256=sha(payload),
              patch_file_offset=hex(0xec02), patch_function_offset=hex(0x40),
              old_stolen_mib=8128, corrected_stolen_mib=60, aperture_mib=256,
              unchanged_boot_args=config['NVRAM']['Add']['7C436110-AB2A-4BBB-A880-FE41995C9F82']['boot-args'],
              metal_runtime_confirmed=False)
if APPLY:
    assert os.geteuid()==0
    disk = json.loads(run('lsblk','-b','-J','-o','NAME,PATH,SERIAL,TRAN,SIZE,PARTUUID,MOUNTPOINTS','/dev/sda'))['blockdevices'][0]
    assert disk['serial']=='REDACTED_EXTERNAL_SERIAL' and disk['tran']=='usb' and disk['size']==256060514304
    assert disk['children'][0]['partuuid']=='3aa58d4a-c7b7-4134-9c20-39c175f39f30'
    assert disk['children'][2]['partuuid']=='7bb5254d-220e-4792-9421-1682c9f75718'
    assert all(not any(p['mountpoints'] or []) for p in disk['children'])
    ggc = run('setpci','-s','00:02.0','50.w').strip()
    assert int(ggc,16)==0xfec1
    resource = Path('/sys/bus/pci/devices/0000:00:02.0/resource').read_text().splitlines()[2].split()
    assert int(resource[1],16)-int(resource[0],16)+1==aperture
    report['ggc_readonly']=ggc
    mount=OUT/'esp'
    mount.mkdir()
    mounted=False
    try:
        run('mount','-t','vfat','-o','ro,nosuid,nodev,noexec','/dev/sda1',str(mount))
        mounted=True
        assert sha((mount/'EFI/OC/config.plist').read_bytes())==EXPECTED
        before=manifest(mount/'EFI')
        assert before==manifest(SNAP), 'External EFI changed: stop and investigate'
        (OUT/'efi-before-sha256.json').write_text(json.dumps(before,indent=2)+'\n')
        run('mount','-o','remount,rw',str(mount))
        backup=mount/'YogaXeGPU-backups'/('gms60-'+stamp)
        backup.mkdir(parents=True)
        (backup/'config.plist').write_bytes(original)
        assert sha((backup/'config.plist').read_bytes())==EXPECTED
        target=mount/'EFI/OC/config.plist'
        tmp=target.with_name('config.gms60.tmp')
        assert not tmp.exists()
        with tmp.open('xb') as f:
            f.write(payload)
            f.flush()
            os.fsync(f.fileno())
        os.replace(tmp,target)
        with target.open('rb') as f: os.fsync(f.fileno())
        assert target.read_bytes()==payload
        after=manifest(mount/'EFI')
        changed=[p for p in sorted(set(before)|set(after)) if before.get(p)!=after.get(p)]
        assert changed==['OC/config.plist'], changed
        report.update(applied=True,external_disk_writes=True,changed_efi_files=changed,
                      backup_on_external=str(backup.relative_to(mount)),
                      efi_files_verified=len(after),external_serial=disk['serial'])
        run('sync','-f',str(mount))
    finally:
        if mounted: run('umount',str(mount))
(OUT/'report.json').write_text(json.dumps(report,indent=2)+'\n')
if os.geteuid()==0:
    for p in [OUT,*OUT.rglob('*')]: os.chown(p,1000,1000)
print(json.dumps(dict(evidence=str(OUT),**report),indent=2))
