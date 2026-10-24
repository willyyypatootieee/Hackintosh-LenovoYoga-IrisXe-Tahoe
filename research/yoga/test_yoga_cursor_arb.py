"""Offline tests. Mocked write APIs do not establish live kernel safety."""
from pathlib import Path
import ast
import hashlib
import json
import plistlib
import subprocess
import tempfile
import unittest
from audit_cursor_arb import ROOT, captured_function, replacement

SOURCE = ROOT / 'yoga-cursor-arb/YogaCursorArb.cpp'


def names(path, kind):
    output = subprocess.check_output(['llvm-nm', kind, str(path)], text=True)
    return {line.split()[-1] for line in output.splitlines() if line.split()}


class CursorArbTests(unittest.TestCase):
    def test_actual_callback_with_mocked_patcher(self):
        # Reuse only the basic SDK mock text, without executing the older test.
        tree = ast.parse((ROOT / 'test_yoga_gms_runtime.py').read_text())
        method = next(n for n in ast.walk(tree) if isinstance(n, ast.FunctionDef) and n.name == 'test_actual_source_with_mocked_runtime')
        assignment = next(n for n in method.body if isinstance(n, ast.Assign) and any(isinstance(t, ast.Name) and t.id == 'stub' for t in n.targets))
        stub = ast.literal_eval(assignment.value)
        stub += '\nstatic bool optIn = false; bool checkKernelArgument(const char *) {return optIn;}\n'
        harness = r'''
int main() {
KernelPatcher p;
eligible=true; checkHardware(nullptr,p); assert(!eligible); // opt-in mandatory
// The real hardware check executes CPUID. Callback tests set eligibility
// explicitly; they must not pretend this host is the Yoga at macOS runtime.
targets[0].loadIndex=6;
uint8_t memory[512];
const size_t n=sizeof(cursorArbOld);
auto reset=[&] {memset(memory,0xcc,sizeof(memory)); memcpy(memory+16,cursorArbOld,n);
 p.function=reinterpret_cast<uint64_t>(memory+16);p.error=KernelPatcher::Error::NoError;p.failWrite=false;eligible=true;};
auto base=reinterpret_cast<uint64_t>(memory);
reset(); int before=p.writes;
patchLoaded(nullptr,p,99,base,sizeof(memory));assert(p.writes==before);
targets[0].loadIndex=0;patchLoaded(nullptr,p,0,base,sizeof(memory));assert(p.writes==before);targets[0].loadIndex=6;
eligible=false;patchLoaded(nullptr,p,6,base,sizeof(memory));assert(p.writes==before);eligible=true;
p.function=0;patchLoaded(nullptr,p,6,base,sizeof(memory));assert(p.writes==before);reset();
p.function=base-1;patchLoaded(nullptr,p,6,base,sizeof(memory));assert(p.writes==before);reset();
p.function=base+sizeof(memory)-n;patchLoaded(nullptr,p,6,base,sizeof(memory));assert(p.writes==before);reset();
patchLoaded(nullptr,p,6,base,n);assert(p.writes==before);
for(size_t i=0;i<n;i++) for(unsigned bit=0;bit<8;bit++) {
 reset();memory[16+i]^=1u<<bit;patchLoaded(nullptr,p,6,base,sizeof(memory));assert(p.writes==before);
}
reset();p.failWrite=true;patchLoaded(nullptr,p,6,base,sizeof(memory));
assert(p.writes==before && !memcmp(memory+16,cursorArbOld,n));
reset();patchLoaded(nullptr,p,6,base,sizeof(memory));
assert(p.writes==before+1 && !memcmp(memory+16,cursorArbNew,n));
for(size_t i=0;i<sizeof(memory);i++) if(i<16||i>=16+n)assert(memory[i]==0xcc);
patchLoaded(nullptr,p,6,base,sizeof(memory));assert(p.writes==before+1);
}
'''
        with tempfile.TemporaryDirectory(prefix='cursor-arb-tests-', dir='/tmp/opencode') as tmp:
            tmp = Path(tmp)
            (tmp / 'Headers').mkdir()
            for name in ('kern_api.hpp', 'kern_devinfo.hpp', 'kern_iokit.hpp', 'plugin_start.hpp'):
                (tmp / 'Headers' / name).write_text('')
            cpp = tmp / 'test.cpp'
            cpp.write_text(stub + '\n#include "' + str(SOURCE) + '"\n' + harness)
            subprocess.run(['clang++', '-std=c++17', '-fsanitize=address,undefined',
                            '-fno-omit-frame-pointer', '-I' + str(tmp), str(cpp), '-o', str(tmp / 'test')],
                           check=True, capture_output=True)
            subprocess.run([str(tmp / 'test')], check=True, capture_output=True)

    def test_generated_arrays_and_mode_semantics(self):
        import re
        arrays = {name: bytes(int(x, 16) for x in body.split(',')) for name, body in
                  re.findall(r'uint8_t cursorArb(Old|New)\[\] = \{([^}]+)\}',
                             (SOURCE.parent / 'CursorArbGuard.hpp').read_text())}
        self.assertEqual(arrays['Old'], captured_function())
        self.assertEqual(arrays['New'], replacement(arrays['Old']))
        self.assertEqual(sum(a != b for a, b in zip(arrays['Old'], arrays['New'])), 3)

    def test_signed_build_and_imports(self):
        stage = max((ROOT / 'cross-build').glob('yoga-cursor-arb-*/build-report.json')).parent
        bundle = stage / 'YogaCursorArb.kext'
        binary = bundle / 'Contents/MacOS/YogaCursorArb'
        info = plistlib.loads((bundle / 'Contents/Info.plist').read_bytes())
        baseline = ROOT / 'postmatch-efi-nvram-readonly-20261024-032122/copies/external-esp/EFI/OC/Kexts'
        ng = baseline / 'NootedGreen.kext/Contents/MacOS/NootedGreen'
        imports = names(binary, '--undefined-only')
        self.assertFalse(imports - names(ng, '--undefined-only'))
        # Latest focused copy contains NootedGreen/config only. Use the older
        # full captured EFI for Lilu, not an imaginary freshly captured binary.
        lilu = names(ROOT / 'installed-external-readonly-20261023-231719/EFI/OC/Kexts/Lilu.kext/Contents/MacOS/Lilu', '--defined-only')
        api = {x for x in imports if any(s in x for s in ('KernelPatcher', 'DeviceInfo', 'LiluAPI', 'WIOKit'))}
        self.assertFalse(api - lilu)
        self.assertIn('com.StezzaPilot.NootedGreen', info['OSBundleLibraries'])
        for forbidden in ('atexit', 'ml_nofault_copy', 'pciConfigWrite', 'routeMultiple'):
            self.assertFalse(any(forbidden in s for s in imports))
        from audit_signature_pages import audit_file
        audit = audit_file(binary)
        self.assertTrue(audit['slices'])
        for entry in audit['slices']:
            self.assertEqual(entry['filetype'], 11)
            self.assertTrue(entry['directories'])
            for d in entry['directories']:
                self.assertEqual(d['page_shift'], 12)
                self.assertTrue(d['code_hashes_valid'])
        report = json.loads((stage / 'build-report.json').read_text())
        self.assertEqual(report['sha256'], hashlib.sha256(binary.read_bytes()).hexdigest())
        self.assertEqual(report['source_sha256'], hashlib.sha256(SOURCE.read_bytes()).hexdigest())
        self.assertFalse(report['external_writes'])
        self.assertFalse(report['runtime_validated'])


if __name__ == '__main__':
    unittest.main()
