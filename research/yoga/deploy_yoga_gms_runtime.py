"""Default local staging; --apply writes only verified Samsung external ESP.

Existing NootedGreen, Lilu, ACPI, quirks and arguments are not changed.
APFS is mounted read-only solely to verify the exact driver/KC targets.
"""
from pathlib import Path
import copy
import datetime
import hashlib
import json
import os
import plistlib
import shutil
import subprocess
import sys
from yoga_fb_match_patch import ROOT, SNAP, NG_HASH, FB_HASH, sha
from audit_signature_pages import audit_file

EXPECTED = '831ad8977de389126abcaf6fa0134a0fa1c7d5ec7f2c7bf2e62201a30cf4cab2'
PLUGIN_HASH = '3928e4750df5f4987e744082a580ae39ece26fde4901192fb10c2994634dee0d'
STAGE = ROOT / 'cross-build/yoga-gms-runtime-v2'
PLUGIN = STAGE / 'YogaGMSFix.kext'
ACTIVE = ROOT / 'postmatch-efi-nvram-readonly-20261024-011020/copies/external-esp/EFI/OC/config.plist'


def run(*args):
    return subprocess.check_output(list(map(str, args)), text=True, stderr=subprocess.STDOUT)


def manifest(folder):
    return {str(p.relative_to(folder)): sha(p.read_bytes()) for p in folder.rglob('*') if p.is_file()}


def check_disk():
    disk = json.loads(run('lsblk', '-b', '-J', '-o', 'NAME,PATH,SERIAL,TRAN,SIZE,PARTUUID,MOUNTPOINTS', '/dev/sda'))['blockdevices'][0]
    assert disk['serial'] == 'REDACTED_EXTERNAL_SERIAL' and disk['tran'] == 'usb' and disk['size'] == 256060514304
    parts = {p['path']: p for p in disk['children']}
    assert parts['/dev/sda1']['partuuid'] == '3aa58d4a-c7b7-4134-9c20-39c175f39f30'
    assert parts['/dev/sda3']['partuuid'] == '7bb5254d-220e-4792-9421-1682c9f75718'
    assert all(not any(p['mountpoints'] or []) for p in parts.values())
    return disk


def build_config():
    original = ACTIVE.read_bytes()
    assert sha(original) == EXPECTED
    config = plistlib.loads(original)
    new = copy.deepcopy(config)
    assert not any(x['BundlePath'] == 'YogaGMSFix.kext' for x in new['Kernel']['Add'])
    at = next(i for i, x in enumerate(new['Kernel']['Add']) if x['BundlePath'] == 'NootedGreen.kext') + 1
    new['Kernel']['Add'].insert(at, dict(Arch='x86_64', BundlePath='YogaGMSFix.kext',
        Comment='Yoga FE GMS: exact HW+FB runtime correction for AuxKC drivers', Enabled=True,
        ExecutablePath='Contents/MacOS/YogaGMSFix', MaxKernel='25.99.99', MinKernel='25.0.0',
        PlistPath='Contents/Info.plist'))
    check = copy.deepcopy(new)
    del check['Kernel']['Add'][at]
    assert check == config, 'Only one Kernel/Add entry may change'
    return original, plistlib.dumps(new, sort_keys=False)


def durable(path, data):
    with path.open('xb') as stream:
        stream.write(data)
        stream.flush()
        os.fsync(stream.fileno())


def main():
    assert sys.argv[1:] in ([], ['--apply'])
    apply = bool(sys.argv[1:])
    if apply: assert os.geteuid() == 0
    original, payload = build_config()
    assert sha((PLUGIN / 'Contents/MacOS/YogaGMSFix').read_bytes()) == PLUGIN_HASH
    signed = audit_file(PLUGIN / 'Contents/MacOS/YogaGMSFix')
    assert all(d['page_shift'] == 12 and d['code_hashes_valid'] for s in signed['slices'] for d in s['directories'])
    tests = run(sys.executable, '-m', 'unittest', '-v', 'test_yoga_gms_runtime', 'test_yoga_fb_match_patch')
    stamp = datetime.datetime.now().strftime('%Y%m%d-%H%M%S')
    out = ROOT / ('yoga-gms-runtime-' + ('apply-' if apply else 'stage-') + stamp)
    out.mkdir()
    (out / 'config.original.plist').write_bytes(original)
    (out / 'config.plist').write_bytes(payload)
    (out / 'tests.txt').write_text(tests)
    (out / 'ocvalidate.txt').write_text(run('/path/to/local-workspace/nvidia-macos-driver/_oc_stage/rel/Utilities/ocvalidate/ocvalidate.linux', out / 'config.plist'))
    report = dict(applied=False, apfs_writes=False, internal_disk_writes=False,
                  original_config_sha256=EXPECTED, new_config_sha256=sha(payload),
                  plugin_sha256=PLUGIN_HASH, tests_passed=True,
                  native_runtime_validated=False, metal_confirmed=False, stable_display_confirmed=False)
    try:
        if not apply: return
        disk = check_disk()
        mount = out / 'data-ro'
        mount.mkdir()
        mounted = False
        try:
            run('apfs-fuse', '-v', '0', '-o', 'ro', '/dev/sda3', mount)
            mounted = True
            base = mount / 'root'
            assert sha((base / 'Library/KernelCollections/AuxiliaryKernelExtensions.kc').read_bytes()) == '018133be9c1b2c92fff6b51e3cae5f3324bbaa15ba3e896f7635fbc6444849e9'
            for name in ['AppleIntelTGLGraphics', 'AppleIntelTGLGraphicsFramebuffer']:
                path = Path('Library/Extensions') / (name + '.kext') / 'Contents/MacOS' / name
                expected = ROOT / 'installed-signatures-readonly-20261023-233323/copies/data' / path
                assert (base / path).read_bytes() == expected.read_bytes()
            report['live_auxkc_and_drivers_verified_readonly'] = True
        finally:
            if mounted: run('umount', mount)
        check_disk()
        mount = out / 'esp'
        mount.mkdir()
        mounted = False
        config_written = False
        try:
            run('mount', '-t', 'vfat', '-o', 'ro,nosuid,nodev,noexec', '/dev/sda1', mount)
            mounted = True
            target = mount / 'EFI/OC/config.plist'
            assert target.read_bytes() == original
            assert sha((mount / 'EFI/OC/Kexts/NootedGreen.kext/Contents/MacOS/NootedGreen').read_bytes()) == NG_HASH
            before = manifest(mount / 'EFI')
            expected = manifest(SNAP)
            expected['OC/config.plist'] = EXPECTED
            assert before == expected, 'Unexpected EFI difference; stop'
            assert not (mount / 'EFI/OC/Kexts/YogaGMSFix.kext').exists()
            (out / 'efi-before-sha256.json').write_text(json.dumps(before, indent=2) + '\n')
            run('mount', '-o', 'remount,rw', mount)
            backup = mount / 'YogaXeGPU-backups' / ('gms-runtime-' + stamp)
            backup.mkdir(parents=True)
            durable(backup / 'config.plist', original)
            assert (backup / 'config.plist').read_bytes() == original
            # Copy and verify complete plugin before enabling it in config.
            destination = mount / 'EFI/OC/Kexts/YogaGMSFix.kext'
            shutil.copytree(PLUGIN, destination)
            for p in destination.rglob('*'):
                if p.is_file():
                    with p.open('rb') as stream: os.fsync(stream.fileno())
            assert manifest(destination) == manifest(PLUGIN)
            tmp = target.with_name('config.yoga-gms.tmp')
            durable(tmp, payload)
            os.replace(tmp, target)
            config_written = True
            run('sync', '-f', mount)
            assert target.read_bytes() == payload
            after = manifest(mount / 'EFI')
            wanted = dict(before)
            wanted['OC/config.plist'] = sha(payload)
            wanted.update({'OC/Kexts/YogaGMSFix.kext/' + name: digest for name, digest in manifest(PLUGIN).items()})
            assert after == wanted, 'Only config + new plugin may change'
            report.update(applied=True, external_serial=disk['serial'],
                          backup_on_external=str(backup.relative_to(mount)),
                          unchanged_existing_efi_files=len(before)-1,
                          changed_efi_files=[p for p in sorted(after) if before.get(p) != after[p]])
            (out / 'efi-after-sha256.json').write_text(json.dumps(after, indent=2) + '\n')
        except Exception:
            if mounted and config_written:
                # Restore only our just-written config, never unfamiliar changes.
                if target.read_bytes() == payload:
                    tmp = target.with_name('config.yoga-gms-rollback.tmp')
                    durable(tmp, original)
                    os.replace(tmp, target)
                    run('sync', '-f', mount)
                    report['automatic_config_rollback'] = True
            raise
        finally:
            if mounted: run('umount', mount)
        run('mount', '-t', 'vfat', '-o', 'ro,nosuid,nodev,noexec', '/dev/sda1', mount)
        try:
            assert manifest(mount / 'EFI') == after
            assert (mount / report['backup_on_external'] / 'config.plist').read_bytes() == original
            report['independent_readback_passed'] = True
        finally:
            run('umount', mount)
    except Exception as error:
        report['error'] = repr(error)
        raise
    finally:
        (out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
        if os.geteuid() == 0:
            for p in [out, *out.rglob('*')]: os.chown(p, 1000, 1000)
        print(json.dumps(dict(output=str(out), **report), indent=2))


if __name__ == '__main__':
    os.chdir(ROOT)
    main()
