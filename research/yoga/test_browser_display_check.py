"""Static safety checks; macOS runtime behavior must be verified on macOS."""
from pathlib import Path
import subprocess
import unittest


class BrowserCheck(unittest.TestCase):
    def setUp(self):
        self.path = Path(__file__).with_name('macos-browser-display-check-20261024.command')
        self.text = self.path.read_text()

    def test_bash_syntax(self):
        subprocess.run(['bash', '-n', str(self.path)], check=True)

    def test_no_privileged_mutation(self):
        for forbidden in ['sudo ', 'defaults write', 'nvram ', 'diskutil ', 'kmutil ',
                          'rm -', 'killall ', '--no-sandbox', 'security ']:
            self.assertNotIn(forbidden, self.text)

    def test_profiles_and_opt_in(self):
        self.assertIn('mktemp -d', self.text)
        self.assertIn('profile="$OUT/$name-test-profile"', self.text)
        self.assertIn('--user-data-dir="$profile"', self.text)
        self.assertIn('-no-remote -profile "$profile"', self.text)
        self.assertIn('if [ "$render_mode" = s ]; then', self.text)
        self.assertIn('"$bin" --user-data-dir="$profile" --no-first-run', self.text)
        self.assertIn('"$bin" --user-data-dir="$profile" --disable-gpu', self.text)

    def test_linux_refuses_before_output_creation(self):
        self.assertLess(self.text.index('!= Darwin'), self.text.index('mktemp -d'))
        result = subprocess.run(['bash', str(self.path)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 1)
        self.assertIn('Run this file in macOS', result.stdout)


if __name__ == '__main__':
    unittest.main()
