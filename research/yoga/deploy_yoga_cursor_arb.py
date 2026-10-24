"""Stage by default. Explicit --apply writes only the identity-checked external ESP.

--rollback STATE restores only this trial's original config, retaining the inert
plugin and all evidence. No internal-disk writes or APFS writes in any mode.
"""
from pathlib import Path
import argparse
import copy
import datetime
import json
import os
import plistlib
import shutil
import sys
from deploy_yoga_gms_runtime import check_disk, durable, manifest, run
from yoga_fb_match_patch import ROOT, sha, NG_HASH, FB_HASH
from audit_signature_pages import audit_file

if not __debug__:
    raise RuntimeError('Safety checks require Python without -O or PYTHONOPTIMIZE.')

GUID = '7C436110-AB2A-4BBB-A880-FE41995C9F82'
EXPECTED = '4ad8d229a94e5bea9cbe6b4670076227e9de42d3c75cbf49d090ab8a50cff642'
PLUGIN_HASH = 'dc49684c764d11e4c703dd3108ce477c1a8db72f3723b3715f8bdfa363cd4a3f'
STAGE = ROOT / 'cross-build/yoga-cursor-arb-20261024-040003'
PLUGIN = STAGE / 'YogaCursorArb.kext'
BASE = ROOT / 'postmatch-efi-nvram-readonly-20261024-032122/copies/external-esp/EFI/OC/config.plist'
VALIDATOR = Path('/path/to/local-workspace/nvidia-macos-driver/_oc_stage/rel/Utilities/ocvalidate/ocvalidate.linux')
ARG = '-yogacursorarb'


def build_config():
    original = BASE.read_bytes()
    assert sha(original) == EXPECTED
    old = plistlib.loads(original)
    new = copy.deepcopy(old)
    entries = new['Kernel']['Add']
    assert not any('YogaCursor' in x['BundlePath'] for x in entries)
    for name in ('Lilu.kext', 'NootedGreen.kext', 'YogaGMSFix.kext'):
        assert sum(x['BundlePath'] == name and x['Enabled'] for x in entries) == 1
    at = next(i for i, x in enumerate(entries) if x['BundlePath'] == 'YogaGMSFix.kext') + 1
    entries.insert(at, dict(Arch='x86_64', BundlePath='YogaCursorArb.kext',
        Comment='EXPERIMENTAL ADL-P cursor arbitration Wa_22012358565; opt-in only', Enabled=True,
        ExecutablePath='Contents/MacOS/YogaCursorArb', MaxKernel='25.99.99',
        MinKernel='25.0.0', PlistPath='Contents/Info.plist'))
    args = old['NVRAM']['Add'][GUID]['boot-args']
    assert '-v' in args.split() and 'ngreenV142=3' in args.split()
    assert not any('yogacursor' in a for a in args.split())
    assert 'boot-args' in old['NVRAM']['Delete'][GUID]
    new['NVRAM']['Add'][GUID]['boot-args'] = args + ' ' + ARG
    check = copy.deepcopy(new)
    del check['Kernel']['Add'][at]
    check['NVRAM']['Add'][GUID]['boot-args'] = args
    assert check == old, 'Only the new entry and opt-in argument may change'
    return original, plistlib.dumps(new, sort_keys=False)


def expected_efi():
    result = json.loads((ROOT / 'quiet-audio-apply-20261024-015039/efi-after-sha256.json').read_text())
    result['OC/config.plist'] = EXPECTED
    return result


def identity(mount=None):
    # check_disk requires an unmounted disk; while mounted, recheck identity
    # independently and verify this mount's actual source and filesystem.
    if mount is None:
        disk = check_disk()
    else:
        # JSON includes children only with NAME selected or --tree explicit.
        disk = json.loads(run('lsblk', '--tree', '-b', '-J', '-o', 'PATH,SERIAL,TRAN,SIZE,PARTUUID', '/dev/sda'))['blockdevices'][0]
        assert disk['serial'] == 'REDACTED_EXTERNAL_SERIAL' and disk['tran'] == 'usb'
        assert disk['size'] == 256060514304
        assert disk.get('children'), 'lsblk did not report the external partition tree; refusing writes'
        parts = {p['path']: p for p in disk['children']}
        assert parts['/dev/sda1']['partuuid'] == '3aa58d4a-c7b7-4134-9c20-39c175f39f30'
        assert parts['/dev/sda3']['partuuid'] == '7bb5254d-220e-4792-9421-1682c9f75718'
        info = json.loads(run('findmnt', '-J', '-M', mount, '-o', 'SOURCE,FSTYPE'))['filesystems'][0]
        assert info['source'] == '/dev/sda1' and info['fstype'] == 'vfat'
    assert run('blkid', '-s', 'UUID', '-o', 'value', '/dev/sda1').strip() == '2719-4E8A'
    return disk


def verify_apfs_readonly(out):
    identity()
    mount = out / 'data-ro'
    mount.mkdir()
    run('apfs-fuse', '-v', '0', '-o', 'ro', '/dev/sda3', mount)
    try:
        base = mount / 'root'
        assert sha((base / 'Library/KernelCollections/AuxiliaryKernelExtensions.kc').read_bytes()) == '018133be9c1b2c92fff6b51e3cae5f3324bbaa15ba3e896f7635fbc6444849e9'
        for name, digest in (('AppleIntelTGLGraphicsFramebuffer', FB_HASH),
                             ('AppleIntelTGLGraphics', '2df441e29032a68c0945942996f12337703689c18e3a447d4d15cfdab6858d5f')):
            assert sha((base / ('Library/Extensions/' + name + '.kext/Contents/MacOS/' + name)).read_bytes()) == digest
    finally:
        run('umount', mount)


def replace_config(target, payload, label):
    temporary = target.with_name('config.cursor-arb-' + label + '.tmp')
    durable(temporary, payload)
    os.replace(temporary, target)


def apply(out, original, payload, report):
    verify_apfs_readonly(out)
    report['live_auxkc_and_drivers_verified_readonly'] = True
    disk = identity()
    mount = out / 'esp'
    mount.mkdir()
    run('mount', '-t', 'vfat', '-o', 'ro,nosuid,nodev,noexec', '/dev/sda1', mount)
    mounted = True
    written = False
    target = mount / 'EFI/OC/config.plist'
    try:
        before = manifest(mount / 'EFI')
        assert before == expected_efi(), 'Unfamiliar EFI changes; stop without writing'
        assert target.read_bytes() == original
        assert sha((mount / 'EFI/OC/Kexts/NootedGreen.kext/Contents/MacOS/NootedGreen').read_bytes()) == NG_HASH
        assert not (mount / 'EFI/OC/Kexts/YogaCursorArb.kext').exists()
        (out / 'efi-before-sha256.json').write_text(json.dumps(before, indent=2) + '\n')
        wanted = dict(before)
        wanted['OC/config.plist'] = sha(payload)
        wanted.update({'OC/Kexts/YogaCursorArb.kext/' + k: v for k, v in manifest(PLUGIN).items()})
        identity(mount)
        run('mount', '-o', 'remount,rw', mount)
        backup = Path('YogaXeGPU-backups') / out.name
        (mount / backup).mkdir(parents=True, exist_ok=False)
        durable(mount / backup / 'config.plist', original)
        assert (mount / backup / 'config.plist').read_bytes() == original
        run('sync', '-f', mount)
        destination = mount / 'EFI/OC/Kexts/YogaCursorArb.kext'
        shutil.copytree(PLUGIN, destination)
        for p in destination.rglob('*'):
            if p.is_file():
                with p.open('rb') as stream:
                    os.fsync(stream.fileno())
        assert manifest(destination) == manifest(PLUGIN)
        run('sync', '-f', mount)
        # Config activation is last, after durable backup and complete plugin.
        identity(mount)
        replace_config(target, payload, out.name)
        written = True
        run('sync', '-f', mount)
        assert target.read_bytes() == payload
        assert manifest(mount / 'EFI') == wanted
        run('umount', mount)
        mounted = False
        identity()
        run('mount', '-t', 'vfat', '-o', 'ro,nosuid,nodev,noexec', '/dev/sda1', mount)
        mounted = True
        assert manifest(mount / 'EFI') == wanted
        assert (mount / backup / 'config.plist').read_bytes() == original
        (out / 'efi-after-sha256.json').write_text(json.dumps(wanted, indent=2) + '\n')
        report.update(applied=True, external_serial=disk['serial'], backup_on_external=str(backup),
                      independent_readback_passed=True, unchanged_existing_efi_files=len(before) - 1)
    except Exception:
        if mounted and written and target.read_bytes() == payload:
            identity(mount)
            run('mount', '-o', 'remount,rw', mount)
            replace_config(target, original, 'automatic-rollback-' + out.name)
            run('sync', '-f', mount)
            assert target.read_bytes() == original
            report['automatic_config_rollback'] = True
        raise
    finally:
        if mounted:
            run('umount', mount)


def rollback(state):
    state = state.resolve()
    assert state.parent == ROOT and state.name.startswith('cursor-arb-apply-')
    original, payload = state.joinpath('config.original.plist').read_bytes(), state.joinpath('config.plist').read_bytes()
    assert (original, payload) == build_config()
    expected = expected_efi()
    expected.update({'OC/Kexts/YogaCursorArb.kext/' + k: v for k, v in manifest(PLUGIN).items()})
    enabled = dict(expected, **{'OC/config.plist': sha(payload)})
    identity()
    mount = state / ('rollback-esp-' + datetime.datetime.now().strftime('%Y%m%d-%H%M%S'))
    mount.mkdir()
    run('mount', '-t', 'vfat', '-o', 'ro,nosuid,nodev,noexec', '/dev/sda1', mount)
    try:
        current = manifest(mount / 'EFI')
        if current == expected:
            print('Already rolled back; plugin retained but disabled.')
            return
        assert current == enabled, 'Unfamiliar EFI changes; rollback refused'
        identity(mount)
        run('mount', '-o', 'remount,rw', mount)
        replace_config(mount / 'EFI/OC/config.plist', original, mount.name)
        run('sync', '-f', mount)
        assert manifest(mount / 'EFI') == expected
    finally:
        run('umount', mount)
    identity()
    run('mount', '-t', 'vfat', '-o', 'ro,nosuid,nodev,noexec', '/dev/sda1', mount)
    try:
        assert manifest(mount / 'EFI') == expected
    finally:
        run('umount', mount)
    durable(state / ('rollback-' + mount.name + '.json'), json.dumps(dict(rolled_back=True,
        independent_readback_passed=True, plugin_retained_inert=True, internal_disk_writes=False)).encode())
    print('Rollback verified: original config restored; candidate retained but disabled.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument('--apply', action='store_true')
    mode.add_argument('--rollback', type=Path)
    args = parser.parse_args()
    assert sys.platform.startswith('linux')
    if args.apply or args.rollback:
        if os.geteuid() != 0:
            raise SystemExit('Administrator privileges required. Run this script with sudo in your terminal.')
    if args.rollback:
        rollback(args.rollback)
        return
    original, payload = build_config()
    assert sha((PLUGIN / 'Contents/MacOS/YogaCursorArb').read_bytes()) == PLUGIN_HASH
    audit = audit_file(PLUGIN / 'Contents/MacOS/YogaCursorArb')
    assert audit['slices'] and all(s['directories'] for s in audit['slices'])
    assert all(d['page_shift'] == 12 and d['code_hashes_valid'] for s in audit['slices'] for d in s['directories'])
    tests = run(sys.executable, '-m', 'unittest', '-v', 'test_yoga_cursor_arb', 'test_deploy_yoga_cursor_arb')
    out = ROOT / ('cursor-arb-' + ('apply-' if args.apply else 'stage-') + datetime.datetime.now().strftime('%Y%m%d-%H%M%S'))
    out.mkdir()
    durable(out / 'config.original.plist', original)
    durable(out / 'config.plist', payload)
    (out / 'tests.txt').write_text(tests)
    (out / 'ocvalidate.txt').write_text(run(VALIDATOR, out / 'config.plist'))
    durable(out / 'ROLLBACK.txt', ('sudo python3 ' + repr(str(Path(__file__).resolve())) + ' --rollback ' + repr(str(out)) + '\n').encode())
    # Persist local recovery instructions before the first possible ESP write.
    run('sync', '-f', out)
    report = dict(applied=False, internal_disk_writes=False, apfs_writes=False,
                  original_config_sha256=sha(original), new_config_sha256=sha(payload),
                  plugin_sha256=PLUGIN_HASH, runtime_validated=False,
                  scope='Experimental cursor arbitration only; no USB-C or hang repair')
    try:
        if args.apply:
            apply(out, original, payload, report)
    except Exception as error:
        report['applied'] = False
        report['error'] = repr(error)
        raise
    finally:
        (out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
        if os.geteuid() == 0:
            # Never recurse into esp/data-ro: a failed unmount can leave live
            # external files there. Only local top-level evidence is ours.
            owner = ROOT.stat()
            os.chown(out, owner.st_uid, owner.st_gid)
            for p in out.iterdir():
                if p.is_file() and not p.is_symlink():
                    os.chown(p, owner.st_uid, owner.st_gid)
        print(json.dumps(dict(output=str(out), **report), indent=2))


if __name__ == '__main__':
    os.chdir(ROOT)
    main()
