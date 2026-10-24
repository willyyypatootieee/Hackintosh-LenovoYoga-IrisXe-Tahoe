import plistlib
import unittest
from yoga_fb_match_patch import PATCHES, build, apply_to_binary, symbol, FB_BASE, NG_BASE


class YogaFramebufferMatchingTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.original, cls.payload, cls.fb, cls.fixed_fb, cls.ng, cls.fixed_ng = build()

    def test_config_preserves_every_other_setting(self):
        new = plistlib.loads(self.payload)
        self.assertEqual(new['Kernel']['Patch'][-3:], PATCHES)
        del new['Kernel']['Patch'][-3:]
        self.assertEqual(new, plistlib.loads(self.original))

    def test_fb_changes_only_decoder_not_relocation_or_return_value(self):
        self.assertEqual(self.fb[:0x5e227], self.fixed_fb[:0x5e227])
        self.assertEqual(self.fb[0x5e22f:], self.fixed_fb[0x5e22f:])
        self.assertEqual(symbol(self.fb, FB_BASE), 0x5e17a)
        self.assertEqual(self.fixed_fb[0x5e227:0x5e22f], bytes.fromhex('b8 00 00 c0 03 90 90 90'))

    def test_aperture_bounds_for_cursor_and_dsb(self):
        # FBMemMgr_Init sets cursor bytes = 0xddc << 19 and DSB=0x12000.
        # Even all nonzero byte-sized cursor counts fit with 60MiB in 256MiB.
        aperture = 256 << 20
        for count in range(1, 256):
            size = count << 19
            self.assertGreater((4032 << 20) + size, aperture)
            self.assertLess((60 << 20) + size + 0x12000, aperture)

    def test_only_exact_intel_ids_can_match(self):
        ids = self.fixed_ng[0x42ef3:].split(b'\0')[0].decode()
        allowed = {int(s, 16) for s in ids.split()}
        self.assertEqual(allowed, {0x46a68086, 0x9a498086})
        for primary, secondary in [(0x25a910de, 0x3b3917aa), (0xa80a144d, 0), (0x9a408086, 0)]:
            self.assertFalse(primary in allowed or secondary in allowed)
        self.assertEqual(self.fixed_ng[0x42f3c:0x42f4c], b'IOPCIMatch' + b'\0' * 6)

    def test_ng_instructions_symbols_and_layout_unchanged(self):
        changed = {i for i, (a, b) in enumerate(zip(self.ng, self.fixed_ng)) if a != b}
        permitted = set(range(0x42ef3, 0x42f09)) | set(range(0x42f3c, 0x42f4c))
        self.assertTrue(changed <= permitted)
        self.assertEqual(symbol(self.ng, NG_BASE), symbol(self.fixed_ng, NG_BASE))
        self.assertEqual(self.ng[:0x36d00], self.fixed_ng[:0x36d00])

    def test_fail_closed_on_unexpected_bytes(self):
        damaged = self.fb.replace(PATCHES[0]['Find'], b'\0' * len(PATCHES[0]['Find']))
        with self.assertRaises(AssertionError):
            apply_to_binary(damaged, PATCHES[0])


if __name__ == '__main__':
    unittest.main()
