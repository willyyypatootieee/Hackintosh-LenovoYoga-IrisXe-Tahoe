"""Deliver prebuilt archive to external FAT ESP without changing EFI payloads.

This stages an installer; it does NOT install/register macOS GPU drivers.
"""
from pathlib import Path
import datetime
import hashlib
import json
import os
import subprocess
import sys

ROOT = Path(__file__).resolve().parent
NAME = sys.argv[1] if len(sys.argv)>1 else 'YogaXeGPU-no-Xcode'
assert Path(NAME).name == NAME and NAME.startswith('YogaXeGPU-no-Xcode')
PACKAGE = ROOT/('cross-build/'+NAME+'.tar.gz')
STATUS_PATH = ROOT/('cross-build/'+NAME+'-status.json')
if NAME == 'YogaXeGPU-no-Xcode' and not STATUS_PATH.exists():
    STATUS_PATH = ROOT/'cross-build/no-xcode-package-status.json'
STATUS = json.loads(STATUS_PATH.read_text())
assert os.geteuid() == 0
def run(*args):
    return subprocess.check_output(args,text=True,stderr=subprocess.STDOUT)
def digest(path):
    with path.open('rb') as file:
        return hashlib.file_digest(file,'sha256').hexdigest()
assert digest(PACKAGE) == STATUS['sha256']
tree=json.loads(run('lsblk','-b','-J','-o','NAME,PATH,SIZE,SERIAL,TRAN,PARTUUID,MOUNTPOINTS','/dev/sda'))['blockdevices'][0]
assert tree['serial']=='REDACTED_EXTERNAL_SERIAL' and tree['tran']=='usb' and tree['size']==256060514304
assert tree['children'][0]['partuuid']=='3aa58d4a-c7b7-4134-9c20-39c175f39f30'
assert all(not any(p['mountpoints'] or []) for p in tree['children'])
out=ROOT/('gpu-package-delivery-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S'))
out.mkdir()
mount=out/'esp'
mount.mkdir()
mounted=False
def efi_hashes():
    return {str(p.relative_to(mount/'EFI')):digest(p) for p in sorted((mount/'EFI').rglob('*')) if p.is_file()}
try:
    run('mount','-t','vfat','-o','rw,nosuid,nodev,noexec','/dev/sda1',str(mount))
    mounted=True
    before=efi_hashes()
    assert before['OC/config.plist']=='ff453e2d08c8bad6ac74ad647b9063d191b4f119a1a31c0148f536e4d40ee4c6'
    free=os.statvfs(mount)
    assert free.f_bavail*free.f_frsize > PACKAGE.stat().st_size+16*1024*1024
    delivery_folder = 'YogaXeGPU' if NAME == 'YogaXeGPU-no-Xcode' else NAME
    dest=mount/delivery_folder
    assert not dest.exists(),'Existing delivery folder: refusing overwrite'
    dest.mkdir()
    with PACKAGE.open('rb') as source,(dest/PACKAGE.name).open('xb') as target:
        while chunk:=source.read(8*1024*1024):
            target.write(chunk)
        target.flush()
        os.fsync(target.fileno())
    assert digest(dest/PACKAGE.name)==STATUS['sha256']
    (dest/(PACKAGE.name+'.sha256')).write_text(STATUS['sha256']+'  '+PACKAGE.name+'\n')
    (dest/'READ-ME.txt').write_text(('''Targeted v3 HookCase repair for the specific v2 ___cxa_atexit failure.
Copy the archive into your macOS home folder, extract it, then open
Repair HookCase.command. Type REPAIR and authorize. No Xcode or compilation.
Replaces ONLY HookCase.kext, preserves the initial v2 backup, then runs kmutil.
No EFI or peripheral changes. No automatic reboot. See the archive README.
''' if 'HookCase-repair' in NAME else '''Yoga Iris Xe prebuilt package, for installed external Tahoe 25G241.

This folder is outside EFI/ and no boot configuration was changed.
The archive is only staged: GPU drivers are NOT yet installed or registered.
Do not extract the payload onto this FAT partition. Copy the archive to your
macOS home folder, extract it there on APFS, then open Install GPU.command.
No Xcode, developer tools, Homebrew or compilation is required.
The installer verifies signatures, backs up GPU files, and runs kmutil.
It will stop rather than ignore a failed signature or registration check.
It does not reboot or alter Wi-Fi, trackpad, sound, CPU, ACPI or EFI.
For v2: use this new archive rather than the old YogaXeGPU folder/archive.
Built-in Apple codesign re-signs temporary payload copies and verifies them
before installation. No Xcode or developer tools are required.

macOS normally hides the EFI partition. It must be mounted to access this
folder. Never assume macOS disk identifiers are the same as Linux /dev/sda.
Use the partition UUID 3AA58D4A-C7B7-4134-9C20-39C175F39F30 to identify it.
'''))
    os.sync()
    after=efi_hashes()
    assert before==after,'EFI payload changed unexpectedly'
    report=dict(external_serial=tree['serial'],destination='/dev/sda1:/'+delivery_folder+'/'+PACKAGE.name,
                archive_sha256=STATUS['sha256'],efi_files_unchanged=len(before),
                config_sha256=after['OC/config.plist'],gpu_installed=False,
                gpu_registered=False,apfs_writes=False,internal_disk_efi_writes=False)
    (out/'verification.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))
finally:
    if mounted:
        run('umount',str(mount))
    for p in [out,*out.rglob('*')]:
        os.chown(p,1000,1000)
