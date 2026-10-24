from pathlib import Path
import struct
import unittest
from audit_signature_pages import audit

ROOT = Path(__file__).resolve().parent
COPIES = ROOT / 'installed-signatures-readonly-20261023-233323/copies'


class SignatureAuditTests(unittest.TestCase):
    def test_all_installed_x86_slices(self):
        paths = [p for p in COPIES.rglob('*') if p.is_file()]
        count = 0
        for path in paths:
            for entry in audit(path.read_bytes()) or []:
                if entry['cpu'] != '0x1000007':
                    continue
                count += 1
                self.assertTrue(entry['directories'])
                for cd in entry['directories']:
                    self.assertEqual(cd['page_shift'], 12, str(path))
                    self.assertTrue(cd['code_hashes_valid'], str(path))
        self.assertEqual(count, 32)

    def test_universal_architectures_not_conflated(self):
        result = audit((COPIES / 'system/usr/bin/codesign').read_bytes())
        self.assertEqual([(s['cpu'], [d['page_shift'] for d in s['directories']]) for s in result],
                         [('0x1000007', [12]), ('0x100000c', [14])])

    def test_corruption_detected(self):
        data = bytearray((COPIES / 'data/Library/Extensions/HookCase.kext/Contents/MacOS/HookCase').read_bytes())
        data[4096] ^= 1
        cd = audit(data)[0]['directories'][0]
        self.assertFalse(cd['code_hashes_valid'])
        self.assertIn(1, cd['bad_code_slots'])

    def test_non_macho_ignored(self):
        self.assertIsNone(audit(b'not executable'))


if __name__ == '__main__':
    unittest.main()
