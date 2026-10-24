"""Compile actual kernel translation unit against SDK and sanitizer IOKit doubles."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent


class NativeTypecTests(unittest.TestCase):
    def test_pinned_xnu_final_cache_options(self):
        from audit_typec_xnu_mapping_cache import audit
        report = audit()
        self.assertEqual(report['checked_sites'],4)
        self.assertFalse(report['unresolved_default_accepted'])
        self.assertFalse(report['runtime_validated'])

    def test_native_mapping_implementation_with_iokit_doubles(self):
        # Test the unmodified implementation. Override headers only, never swap
        # the implementation with a parallel Python model or fake success path.
        headers = [
            'IOKit/IOService.h','IOKit/IOMemoryDescriptor.h','IOKit/pci/IOPCIDevice.h',
            'libkern/OSAtomic.h','libkern/OSByteOrder.h','libkern/c++/OSMetaClass.h','kern/task.h','kern/clock.h','IOKit/IOLib.h',
        ]
        with tempfile.TemporaryDirectory(prefix='typec-native-iokit-',dir='/tmp/opencode') as tmp:
            tmp = Path(tmp)
            for name in headers:
                p = tmp / name
                p.parent.mkdir(parents=True,exist_ok=True)
                p.write_text('#include "'+str(ROOT/'yoga-typec/test_native_iokit_stubs.hpp')+'"\n')
            binary = tmp / 'native-test'
            command = ['clang++','-std=c++17','-Wall','-Wextra','-Werror',
                       '-fsanitize=address,undefined','-fno-omit-frame-pointer','-g',
                       '-I'+str(tmp),str(ROOT/'yoga-typec/test_native_iokit_mapping.cpp'),'-o',str(binary)]
            compiled = subprocess.run(command,capture_output=True,text=True,timeout=60)
            self.assertEqual(compiled.returncode,0,compiled.stderr)
            run = subprocess.run([str(binary)],capture_output=True,text=True,timeout=30)
            self.assertEqual(run.returncode,0,run.stderr)
            self.assertIn('PASS:',run.stdout)
            self.assertEqual(run.stderr,'')

    def test_native_mapping_kernel_sdk_compilation(self):
        from cross_build_typec_native import build
        report = build()
        self.assertEqual(len(report['objects']),3)
        self.assertFalse(report['loadable_kext'])
        self.assertFalse(report['native_hooks_registered'])
        self.assertFalse(report['device_access'])


if __name__ == '__main__':
    unittest.main()
