import copy
import plistlib
import unittest
from deploy_quiet_audio import build_config, GUID, REMOVE
from audit_simplehda import verify

class QuietAudioTests(unittest.TestCase):
    def setUp(self):
        original, new = build_config()
        self.old, self.new = plistlib.loads(original), plistlib.loads(new)

    def test_only_debug_arguments_removed(self):
        old = self.old['NVRAM']['Add'][GUID]['boot-args'].split()
        new = self.new['NVRAM']['Add'][GUID]['boot-args'].split()
        self.assertEqual(new, [x for x in old if x not in REMOVE] + ['shda-lang=en'])
        self.assertIn('keepsyms=1', new)
        self.assertIn('alcid=11', new)

    def test_gpu_and_all_existing_kexts_unchanged(self):
        new = copy.deepcopy(self.new['Kernel']['Add'])
        entry = next(x for x in new if x['BundlePath'] == 'SimpleHDA.kext')
        self.assertTrue(entry['Enabled'])
        self.assertEqual(entry['MinKernel'], '25.0.0')
        names = [x['BundlePath'] for x in new]
        self.assertLess(names.index('NootedGreen.kext'), names.index('SimpleHDA.kext'))
        self.assertLess(names.index('YogaGMSFix.kext'), names.index('SimpleHDA.kext'))
        self.assertLess(names.index('AppleALC.kext'), names.index('SimpleHDA.kext'))
        new.remove(entry)
        self.assertEqual(new, self.old['Kernel']['Add'])
        for key in ['ACPI', 'DeviceProperties', 'UEFI', 'Booter', 'PlatformInfo']:
            self.assertEqual(self.new[key], self.old[key])
        self.assertEqual(self.new['Kernel']['Patch'], self.old['Kernel']['Patch'])
        self.assertEqual(self.new['Kernel']['Quirks'], self.old['Kernel']['Quirks'])

    def test_efi_logging_disabled_without_picker_or_security_changes(self):
        self.assertFalse(self.new['Misc']['Debug']['AppleDebug'])
        self.assertEqual(self.new['Misc']['Debug']['Target'], 0)
        for key in ['Boot', 'Security', 'Tools', 'Entries']:
            self.assertEqual(self.new['Misc'][key], self.old['Misc'][key])

    def test_official_release_and_kernel_symbols(self):
        report = verify()
        self.assertTrue(report['package_manifest_verified'])
        self.assertEqual(report['missing_with_oc_bootkc_kernel'], [])
        self.assertFalse(report['native_runtime_validated'])

if __name__ == '__main__': unittest.main()
