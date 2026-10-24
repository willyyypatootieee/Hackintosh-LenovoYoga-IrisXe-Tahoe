"""Guarded external-ESP-only in-memory framebuffer/matching repair.

Default: stage locally. --apply: verify USB identity, APFS files and AuxKC
read-only, then atomically update only the external EFI/OC/config.plist.
"""
from pathlib import Path
import datetime
import json
import os
import plistlib
import struct
import subprocess
import sys
from yoga_fb_match_patch import ROOT, SNAP, EXPECTED, FB_HASH, NG_HASH, PATCHES, build, sha


def run(*args):
    return subprocess.check_output(args, text=True, stderr=subprocess.STDOUT)


def manifest(folder):
    return {str(p.relative_to(folder)): sha(p.read_bytes()) for p in folder.rglob('*') if p.is_file()}


def check_disk():
    disk = json.loads(run('lsblk', '-b', '-J', '-o', 'NAME,PATH,SERIAL,TRAN,SIZE,PARTUUID,MOUNTPOINTS', '/dev/sda'))['blockdevices'][0]
    assert disk['serial'] == 'REDACTED_EXTERNAL_SERIAL' and disk['tran'] == 'usb' and disk['size'] == 256060514304
    parts = {p['path']: p for p in disk['children']}
    assert parts['/dev/sda1']['partuuid'] == '3aa58d4a-c7b7-4134-9c20-39c175f39f30'
    assert parts['/dev/sda3']['partuuid'] == '7bb5254d-220e-4792-9421-1682c9f75718'
    assert all(not any(p['mountpoints'] or []) for p in disk['children'])
    assert Path('/sys/bus/pci/devices/0000:00:02.0/vendor').read_text().strip() == '0x8086'
    assert Path('/sys/bus/pci/devices/0000:00:02.0/device').read_text().strip() == '0x46a6'
    assert int(run('setpci', '-s', '00:02.0', '50.w').strip(), 16) == 0xfec1
    resource = Path('/sys/bus/pci/devices/0000:00:02.0/resource').read_text().splitlines()[2].split()
    assert int(resource[1], 16) - int(resource[0], 16) + 1 == 256 << 20
    return disk


assert not sys.argv[1:] or sys.argv[1:] == ['--apply']
apply = bool(sys.argv[1:])
stamp = datetime.datetime.now().strftime('%Y%m%d-%H%M%S')
out = ROOT / ('yoga-fb-match-' + ('apply-' if apply else 'stage-') + stamp)
out.mkdir()
original, payload, fb, fixed_fb, ng, fixed_ng = build()
(out / 'config.original.plist').write_bytes(original)
(out / 'config.plist').write_bytes(payload)
for name, data in [('fb-original', fb), ('fb-patched', fixed_fb)]:
    binary = out / (name + '.bin')
    binary.write_bytes(data[0x5e17a:0x5e237])
    (out / (name + '.txt')).write_text(run('objdump', '-D', '-b', 'binary', '-m', 'i386:x86-64', '--adjust-vma=0x5e17a', str(binary)))
(out / 'ocvalidate.txt').write_text(run('/path/to/local-workspace/nvidia-macos-driver/_oc_stage/rel/Utilities/ocvalidate/ocvalidate.linux', str(out / 'config.plist')))
report = dict(applied=False, external_disk_writes=False, apfs_writes=False,
              original_config_sha256=EXPECTED, new_config_sha256=sha(payload),
              framebuffer_original_sha256=sha(fb), nootedgreen_original_sha256=sha(ng),
              patch_count=3, gms_mib_before=4032, gms_mib_after=60,
              matching_key='IOPCIMatch', matching_ids=['0x46a68086', '0x9a498086'],
              metal_runtime_confirmed=False, display_runtime_confirmed=False)
try:
    if apply:
        assert os.geteuid() == 0
        disk = check_disk()
        mount = out / 'data-ro'
        mount.mkdir()
        mounted = False
        try:
            run('apfs-fuse', '-v', '0', '-o', 'ro', '/dev/sda3', str(mount))
            mounted = True
            base = mount / 'root'
            current = base / 'Library/Extensions/AppleIntelTGLGraphicsFramebuffer.kext/Contents/MacOS/AppleIntelTGLGraphicsFramebuffer'
            assert sha(current.read_bytes()) == FB_HASH
            kc = (base / 'Library/KernelCollections/AuxiliaryKernelExtensions.kc').read_bytes()
            assert sha(kc) == '018133be9c1b2c92fff6b51e3cae5f3324bbaa15ba3e896f7635fbc6444849e9'
            assert len(kc) == 6586368 and kc.count(PATCHES[0]['Find']) == 1
            (out / 'AuxiliaryKernelExtensions.readonly.kc').write_bytes(kc)
            report['auxkc_verified_sha256'] = sha(kc)
            report['auxkc_original_decoder_file_offset'] = hex(kc.find(PATCHES[0]['Find']))
        finally:
            if mounted:
                run('umount', str(mount))
        check_disk()
        mount = out / 'esp'
        mount.mkdir()
        mounted = False
        try:
            run('mount', '-t', 'vfat', '-o', 'ro,nosuid,nodev,noexec', '/dev/sda1', str(mount))
            mounted = True
            target = mount / 'EFI/OC/config.plist'
            assert sha(target.read_bytes()) == EXPECTED
            assert sha((mount / 'EFI/OC/Kexts/NootedGreen.kext/Contents/MacOS/NootedGreen').read_bytes()) == NG_HASH
            before = manifest(mount / 'EFI')
            assert before == manifest(SNAP), 'Unexpected EFI change: stop, do not overwrite'
            (out / 'efi-before-sha256.json').write_text(json.dumps(before, indent=2) + '\n')
            run('mount', '-o', 'remount,rw', str(mount))
            backup = mount / 'YogaXeGPU-backups' / ('fb-match-' + stamp)
            backup.mkdir(parents=True)
            with (backup / 'config.plist').open('xb') as stream:
                stream.write(original)
                stream.flush()
                os.fsync(stream.fileno())
            assert sha((backup / 'config.plist').read_bytes()) == EXPECTED
            tmp = target.with_name('config.fb-match.tmp')
            with tmp.open('xb') as stream:
                stream.write(payload)
                stream.flush()
                os.fsync(stream.fileno())
            os.replace(tmp, target)
            with target.open('rb') as stream:
                os.fsync(stream.fileno())
            assert target.read_bytes() == payload
            after = manifest(mount / 'EFI')
            changed = [p for p in sorted(before.keys() | after.keys()) if before.get(p) != after.get(p)]
            assert changed == ['OC/config.plist'], changed
            run('sync', '-f', str(mount))
            report.update(applied=True, external_disk_writes=True,
                          external_serial=disk['serial'], changed_efi_files=changed,
                          efi_files_verified=len(after), backup_on_external=str(backup.relative_to(mount)))
        finally:
            if mounted:
                run('umount', str(mount))
        # Independent remount after flushing and unmounting, not page-cache-only readback.
        run('mount', '-t', 'vfat', '-o', 'ro,nosuid,nodev,noexec', '/dev/sda1', str(mount))
        try:
            assert (mount / 'EFI/OC/config.plist').read_bytes() == payload
            assert manifest(mount / 'EFI') == after
            assert (mount / report['backup_on_external'] / 'config.plist').read_bytes() == original
            report['independent_readback_passed'] = True
        finally:
            run('umount', str(mount))
except Exception as error:
    report['error'] = repr(error)
    raise
finally:
    (out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    if os.geteuid() == 0:
        for p in [out, *out.rglob('*')]:
            os.chown(p, 1000, 1000)
print(json.dumps(dict(evidence=str(out), **report), indent=2))
