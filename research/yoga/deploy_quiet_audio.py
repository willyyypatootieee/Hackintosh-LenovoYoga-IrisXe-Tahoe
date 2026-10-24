"""Quiet boot + official RELEASE SimpleHDA; external ESP only, no APFS writes."""
from pathlib import Path
import copy
import datetime
import json
import os
import plistlib
import shutil
import sys
from deploy_yoga_gms_runtime import check_disk, durable, manifest, run
from audit_simplehda import verify, PLUGIN
from yoga_fb_match_patch import sha

ROOT = Path(__file__).resolve().parent
BASELINE = ROOT / 'yoga-gms-runtime-apply-20261024-012757'
EXPECTED = '4a59d566d54fbf4e623b0289fb1ddb67f3e6884664e02dd5dd53ab7f4eadc72c'
GUID = '7C436110-AB2A-4BBB-A880-FE41995C9F82'
REMOVE = {'-v', 'debug=0x100', 'pci_log=0x226', 'pci_log_mode=2', '-liludbg', 'io=0x6'}

def build_config():
    original = (BASELINE / 'config.plist').read_bytes()
    assert sha(original) == EXPECTED
    old = plistlib.loads(original)
    new = copy.deepcopy(old)
    args = old['NVRAM']['Add'][GUID]['boot-args'].split()
    assert REMOVE <= set(args)
    new['NVRAM']['Add'][GUID]['boot-args'] = ' '.join(a for a in args if a not in REMOVE) + ' shda-lang=en'
    new['Misc']['Debug']['AppleDebug'] = False
    new['Misc']['Debug']['Target'] = 0
    assert new['Misc']['Security']['SecureBootModel'] == 'Disabled'
    assert new['Kernel']['Quirks']['DisableLinkeditJettison']
    assert 'keepsyms=1' in new['NVRAM']['Add'][GUID]['boot-args'].split()
    assert 'boot-args' in new['NVRAM']['Delete'][GUID]
    entries = new['Kernel']['Add']
    assert not any(x['BundlePath'] == 'SimpleHDA.kext' for x in entries)
    at = next(i for i, x in enumerate(entries) if x['BundlePath'] == 'AppleALC.kext') + 1
    assert next(i for i, x in enumerate(entries) if x['BundlePath'] == 'YogaGMSFix.kext') < at
    sample = plistlib.loads((PLUGIN.parent / 'OpenCore-Kernel-Add.plist').read_bytes())['Kernel']['Add'][0]
    entries.insert(at, sample)
    # Reversing precisely the authorized changes must recover the baseline.
    check = copy.deepcopy(new)
    del check['Kernel']['Add'][at]
    check['NVRAM']['Add'][GUID]['boot-args'] = old['NVRAM']['Add'][GUID]['boot-args']
    check['Misc']['Debug'] = old['Misc']['Debug']
    assert check == old
    return original, plistlib.dumps(new, sort_keys=False)

def main():
    assert sys.argv[1:] in ([], ['--apply'])
    apply = bool(sys.argv[1:])
    if apply: assert os.geteuid() == 0
    audit = verify()
    original, payload = build_config()
    tests = run(sys.executable, '-m', 'unittest', '-v', 'test_quiet_audio')
    stamp = datetime.datetime.now().strftime('%Y%m%d-%H%M%S')
    out = ROOT / ('quiet-audio-' + ('apply-' if apply else 'stage-') + stamp)
    out.mkdir()
    (out / 'config.original.plist').write_bytes(original)
    (out / 'config.plist').write_bytes(payload)
    (out / 'tests.txt').write_text(tests)
    (out / 'audio-audit.json').write_text(json.dumps(audit, indent=2) + '\n')
    (out / 'ocvalidate.txt').write_text(run('/path/to/local-workspace/nvidia-macos-driver/_oc_stage/rel/Utilities/ocvalidate/ocvalidate.linux', out / 'config.plist'))
    report = dict(applied=False, apfs_writes=False, external_system_writes=False, internal_disk_writes=False,
                  original_config_sha256=EXPECTED, new_config_sha256=sha(payload), removed_arguments=sorted(REMOVE),
                  audio_playback_validated=False, native_display_change=False)
    try:
        if not apply: return
        disk = check_disk()
        mount = out / 'esp'
        mount.mkdir()
        mounted = written = False
        try:
            run('mount', '-t', 'vfat', '-o', 'ro,nosuid,nodev,noexec', '/dev/sda1', mount)
            mounted = True
            target = mount / 'EFI/OC/config.plist'
            assert target.read_bytes() == original
            before = manifest(mount / 'EFI')
            assert before == json.loads((BASELINE / 'efi-after-sha256.json').read_text()), 'Unfamiliar EFI changes; stop'
            destination = mount / 'EFI/OC/Kexts/SimpleHDA.kext'
            assert not destination.exists()
            (out / 'efi-before-sha256.json').write_text(json.dumps(before, indent=2) + '\n')
            run('mount', '-o', 'remount,rw', mount)
            backup = mount / 'YogaXeGPU-backups' / ('quiet-audio-' + stamp)
            backup.mkdir(parents=True)
            durable(backup / 'config.plist', original)
            assert (backup / 'config.plist').read_bytes() == original
            shutil.copytree(PLUGIN, destination)
            for p in destination.rglob('*'):
                if p.is_file():
                    with p.open('rb') as f: os.fsync(f.fileno())
            assert manifest(destination) == manifest(PLUGIN)
            tmp = target.with_name('config.quiet-audio.tmp')
            durable(tmp, payload)
            os.replace(tmp, target)
            written = True
            run('sync', '-f', mount)
            after = manifest(mount / 'EFI')
            wanted = dict(before)
            wanted['OC/config.plist'] = sha(payload)
            wanted.update({'OC/Kexts/SimpleHDA.kext/' + k: v for k, v in manifest(PLUGIN).items()})
            assert after == wanted
            report.update(applied=True, external_serial=disk['serial'], backup_on_external=str(backup.relative_to(mount)),
                          changed_efi_files=[k for k in sorted(after) if before.get(k) != after[k]],
                          unchanged_existing_efi_files=len(before) - 1)
            (out / 'efi-after-sha256.json').write_text(json.dumps(after, indent=2) + '\n')
        except Exception:
            if mounted and written and target.read_bytes() == payload:
                tmp = target.with_name('config.quiet-audio-rollback.tmp')
                durable(tmp, original)
                os.replace(tmp, target)
                run('sync', '-f', mount)
                report['automatic_config_rollback'] = True
            raise
        finally:
            if mounted: run('umount', mount)
        check_disk()
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
