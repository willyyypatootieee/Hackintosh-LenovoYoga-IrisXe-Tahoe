"""No device access: deployment config, manifests and write scope tests."""
import copy
import plistlib
import unittest
import tempfile
from pathlib import Path
from unittest.mock import patch
import deploy_yoga_cursor_arb as d


class DeploymentTests(unittest.TestCase):
    def test_only_candidate_and_optin_added(self):
        original, payload = d.build_config()
        old, new = plistlib.loads(original), plistlib.loads(payload)
        i = next(i for i, x in enumerate(new['Kernel']['Add']) if x['BundlePath'] == 'YogaCursorArb.kext')
        entry = new['Kernel']['Add'].pop(i)
        self.assertTrue(entry['Enabled'])
        self.assertEqual(entry['MinKernel'], '25.0.0')
        self.assertEqual(entry['MaxKernel'], '25.99.99')
        self.assertEqual(new['Kernel']['Add'][i - 1]['BundlePath'], 'YogaGMSFix.kext')
        self.assertEqual(new['NVRAM']['Add'][d.GUID]['boot-args'], old['NVRAM']['Add'][d.GUID]['boot-args'] + ' ' + d.ARG)
        new['NVRAM']['Add'][d.GUID]['boot-args'] = old['NVRAM']['Add'][d.GUID]['boot-args']
        self.assertEqual(old, new)

    def test_baseline_manifest_preserves_deployed_plugins(self):
        before = d.expected_efi()
        self.assertEqual(before['OC/config.plist'], d.EXPECTED)
        self.assertEqual(before['OC/Kexts/NootedGreen.kext/Contents/MacOS/NootedGreen'], d.NG_HASH)
        self.assertEqual(before['OC/Kexts/YogaGMSFix.kext/Contents/MacOS/YogaGMSFix'], '3928e4750df5f4987e744082a580ae39ece26fde4901192fb10c2994634dee0d')
        self.assertIn('OC/Kexts/SimpleHDA.kext/Contents/MacOS/SimpleHDA', before)
        self.assertFalse(any('CursorArb' in p for p in before))

    def test_identity_rejects_wrong_partition_uuid_or_mount(self):
        import json
        disk = dict(serial='REDACTED_EXTERNAL_SERIAL', tran='usb', size=256060514304,
            children=[dict(path='/dev/sda1', partuuid='3aa58d4a-c7b7-4134-9c20-39c175f39f30'),
                      dict(path='/dev/sda3', partuuid='7bb5254d-220e-4792-9421-1682c9f75718')])
        def fake_run(*args):
            if args[0] == 'lsblk': return json.dumps(dict(blockdevices=[disk]))
            if args[0] == 'findmnt': return json.dumps(dict(filesystems=[dict(source='/dev/sda1', fstype='vfat')]))
            if args[0] == 'blkid': return '2719-4E8A\n'
            raise AssertionError(args)
        with patch.object(d, 'run', side_effect=fake_run):
            self.assertEqual(d.identity('/mock/esp')['serial'], disk['serial'])
            for field, value in (('serial', 'REDACTED_INTERNAL_SERIAL'), ('size', 1), ('tran', 'nvme')):
                old = disk[field]
                disk[field] = value
                with self.assertRaises(AssertionError): d.identity('/mock/esp')
                disk[field] = old
            disk['children'][0]['partuuid'] = 'wrong'
            with self.assertRaises(AssertionError): d.identity('/mock/esp')

    def test_changed_baseline_refused(self):
        from pathlib import Path
        with patch.object(Path, 'read_bytes', return_value=b'changed'):
            with self.assertRaises(AssertionError): d.build_config()

    def test_identity_explicitly_requests_lsblk_json_tree(self):
        import json
        disk = dict(path='/dev/sda', serial='REDACTED_EXTERNAL_SERIAL', tran='usb', size=256060514304)
        children = [dict(path='/dev/sda1', partuuid='3aa58d4a-c7b7-4134-9c20-39c175f39f30'),
                    dict(path='/dev/sda3', partuuid='7bb5254d-220e-4792-9421-1682c9f75718')]
        def fake_run(*args):
            if args[0] == 'lsblk':
                # util-linux omits children when neither NAME nor --tree is
                # requested. Model that behavior rather than always nesting.
                nested = '--tree' in args or 'NAME' in args[args.index('-o') + 1].split(',')
                devices = [dict(disk, children=children)] if nested else [disk, *children]
                return json.dumps(dict(blockdevices=devices))
            if args[0] == 'findmnt':
                return json.dumps(dict(filesystems=[dict(source='/dev/sda1', fstype='vfat')]))
            if args[0] == 'blkid': return '2719-4E8A\n'
            raise AssertionError(args)
        with patch.object(d, 'run', side_effect=fake_run):
            self.assertEqual(d.identity('/mock/esp')['serial'], disk['serial'])

    def test_identity_missing_tree_refuses_writes(self):
        import json
        disk = dict(serial='REDACTED_EXTERNAL_SERIAL', tran='usb', size=256060514304)
        with patch.object(d, 'run', return_value=json.dumps(dict(blockdevices=[disk]))) as run:
            with self.assertRaisesRegex(AssertionError, 'partition tree'):
                d.identity('/mock/esp')
            self.assertEqual(run.call_count, 1)

    def simulate_apply(self, fail_after_activation=False, fail_before_activation=False, rollback_after=False):
        # Real file copy, manifest, backup, config replacement and readback on
        # a temporary ordinary directory. Mount/device/APFS APIs are mocked.
        with tempfile.TemporaryDirectory(prefix='arb-deploy-test-', dir='/tmp/opencode') as tmp:
            out = Path(tmp) / 'cursor-arb-apply-test'
            out.mkdir()
            original, payload = d.build_config()
            (out / 'config.original.plist').write_bytes(original)
            (out / 'config.plist').write_bytes(payload)
            mount = out / 'esp'
            first_mount = True
            activated = False
            def fake_run(*args):
                nonlocal first_mount, activated
                if args[0] == 'mount' and '-t' in args and first_mount:
                    first_mount = False
                    oc = mount / 'EFI/OC'
                    (oc / 'Kexts/NootedGreen.kext/Contents/MacOS').mkdir(parents=True)
                    (oc / 'config.plist').write_bytes(original)
                    ng = d.ROOT / 'postmatch-efi-nvram-readonly-20261024-032122/copies/external-esp/EFI/OC/Kexts/NootedGreen.kext/Contents/MacOS/NootedGreen'
                    (oc / 'Kexts/NootedGreen.kext/Contents/MacOS/NootedGreen').write_bytes(ng.read_bytes())
                if args[0] == 'sync' and (mount / 'EFI/OC/config.plist').read_bytes() == payload:
                    if fail_after_activation and not activated:
                        activated = True
                        raise RuntimeError('simulated post-activation sync failure')
                if args[0] == 'sync' and fail_before_activation and (mount / 'EFI/OC/Kexts/YogaCursorArb.kext').exists():
                    raise RuntimeError('simulated pre-activation sync failure')
                return ''
            def before_manifest():
                return {'OC/config.plist': d.sha(original),
                        'OC/Kexts/NootedGreen.kext/Contents/MacOS/NootedGreen': d.NG_HASH}
            report = {}
            with patch.object(d, 'verify_apfs_readonly'), patch.object(d, 'identity', return_value={'serial': 'REDACTED_EXTERNAL_SERIAL'}), \
                 patch.object(d, 'run', side_effect=fake_run), patch.object(d, 'expected_efi', side_effect=before_manifest):
                if fail_after_activation or fail_before_activation:
                    with self.assertRaisesRegex(RuntimeError, 'simulated'):
                        d.apply(out, original, payload, report)
                    if fail_after_activation:
                        self.assertTrue(report['automatic_config_rollback'])
                    else:
                        self.assertNotIn('automatic_config_rollback', report)
                    self.assertEqual((mount / 'EFI/OC/config.plist').read_bytes(), original)
                else:
                    d.apply(out, original, payload, report)
                    self.assertTrue(report['applied'])
                    self.assertTrue(report['independent_readback_passed'])
                    self.assertEqual((mount / 'EFI/OC/config.plist').read_bytes(), payload)
                self.assertEqual((mount / 'YogaXeGPU-backups' / out.name / 'config.plist').read_bytes(), original)
                self.assertEqual(d.manifest(mount / 'EFI/OC/Kexts/YogaCursorArb.kext'), d.manifest(d.PLUGIN))
                if rollback_after:
                    # Mock mounts expose the same real test filesystem.
                    # rollback() creates its own mountpoint; map EFI access
                    # there to this already-populated ordinary directory.
                    real_mkdir = Path.mkdir
                    def mkdir(path, *args, **kwargs):
                        if path.name.startswith('rollback-esp-'):
                            path.symlink_to(mount, target_is_directory=True)
                        else:
                            real_mkdir(path, *args, **kwargs)
                    with patch.object(d, 'ROOT', Path(tmp)), patch.object(Path, 'mkdir', mkdir):
                        d.rollback(out)
                    self.assertEqual((mount / 'EFI/OC/config.plist').read_bytes(), original)
                    self.assertEqual(d.manifest(mount / 'EFI/OC/Kexts/YogaCursorArb.kext'), d.manifest(d.PLUGIN))
                    self.assertTrue(list(out.glob('rollback-rollback-esp-*.json')))

    def test_apply_file_flow_with_mocked_devices(self):
        self.simulate_apply()

    def test_post_activation_failure_restores_only_config(self):
        self.simulate_apply(fail_after_activation=True)

    def test_pre_activation_failure_keeps_original_config(self):
        self.simulate_apply(fail_before_activation=True)

    def test_explicit_rollback_restores_config_and_retains_plugin(self):
        self.simulate_apply(rollback_after=True)


if __name__ == '__main__':
    unittest.main()
