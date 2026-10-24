"""Linux-only local diagnostic build. No deployment, mounts, or device access."""
from pathlib import Path
from datetime import datetime, timezone
import hashlib
import json
import plistlib
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parent
SOURCE = ROOT / 'yoga-cursor-diag'
DEPS = Path('/path/to/local-workspace/hackintosh-yoga/xe-lab/NootedGreen')
SDK = DEPS / 'MacKernelSDK'
LILU = DEPS / 'Lilu.kext/Contents/Resources'
name = sys.argv[1] if len(sys.argv) == 2 else 'yoga-cursor-diag-' + datetime.now(timezone.utc).strftime('%Y%m%d-%H%M%S')
assert len(sys.argv) <= 2 and Path(name).name == name and name.startswith('yoga-cursor-diag-')
assert sys.platform.startswith('linux')
STAGE = ROOT / 'cross-build' / name
STAGE.mkdir(exist_ok=False)
commands = []


def run(command, label):
    command = list(map(str, command))
    commands.append(command)
    result = subprocess.run(command, text=True, capture_output=True)
    (STAGE / (label + '.log')).write_text(result.stdout + result.stderr)
    (STAGE / 'commands.json').write_text(json.dumps(commands, indent=2) + '\n')
    if result.returncode:
        raise RuntimeError(label + ': ' + result.stderr[-8000:])
    return result.stdout


run([sys.executable, ROOT / 'audit_cursor_diag_inputs.py'], 'input-audit')
for path in SOURCE.iterdir():
    if path.is_file():
        shutil.copy2(path, STAGE / path.name)
run(['clang++', '-std=c++17', '-fsanitize=undefined,address',
     '-fno-omit-frame-pointer', '-O1', '-g', STAGE / 'test_cursor_math.cpp', '-o', STAGE / 'test-cursor-math'], 'compile-host-tests')
run([STAGE / 'test-cursor-math'], 'host-tests')

out = STAGE / 'YogaCursorDiag.kext/Contents'
(out / 'MacOS').mkdir(parents=True)
info = dict(CFBundleDevelopmentRegion='en', CFBundleExecutable='YogaCursorDiag',
    CFBundleIdentifier='local.yoga.driver.YogaCursorDiag', CFBundleInfoDictionaryVersion='6.0',
    CFBundleName='YogaCursorDiag', CFBundlePackageType='KEXT', CFBundleShortVersionString='0.1.0',
    CFBundleVersion='0.1.0', OSBundleRequired='Root',
    OSBundleLibraries={'as.vit9696.Lilu': '1.7.0', 'com.StezzaPilot.NootedGreen': '1.0.0',
        **{'com.apple.kpi.' + k: '12.0.0' for k in ['bsd', 'dsep', 'iokit', 'libkern', 'mach', 'unsupported']}},
    IOKitPersonalities={'YogaCursorDiag': dict(CFBundleIdentifier='local.yoga.driver.YogaCursorDiag',
        IOClass='YogaCursorDiag', IOMatchCategory='YogaCursorDiag', IOProviderClass='IOResources',
        IOResourceMatch='IOKit', IOProbeScore=1000)})
(out / 'Info.plist').write_bytes(plistlib.dumps(info))
(STAGE / 'info.c').write_text('''#include <mach/mach_types.h>
extern kern_return_t YogaCursorDiag_kern_start(kmod_info_t *, void *);
extern kern_return_t YogaCursorDiag_kern_stop(kmod_info_t *, void *);
__attribute__((visibility("default"))) KMOD_EXPLICIT_DECL(local.yoga.driver.YogaCursorDiag, "0.1.0", YogaCursorDiag_kern_start, YogaCursorDiag_kern_stop)
__private_extern__ kmod_start_func_t *_realmain = 0;
__private_extern__ kmod_stop_func_t *_antimain = 0;
__private_extern__ int _kext_apple_cc = __APPLE_CC__;
''')
flags = ['-target', 'x86_64-apple-macos13', '-mkernel', '-nostdinc', '-fno-builtin',
    '-fno-common', '-fno-stack-protector', '-mmmx', '-msse', '-msse2',
    '-DKERNEL', '-DKERNEL_PRIVATE', '-DDRIVER_PRIVATE', '-DAPPLE', '-DNeXT', '-DDEBUG=1',
    '-DAPPLE_KEXT_ASSERTIONS=1', '-DPRODUCT_NAME=YogaCursorDiag', '-DMODULE_VERSION=0.1.0',
    '-D__null_terminated=', '-D__stateful_pure=', '-isystem', SDK / 'Headers',
    '-I' + str(LILU), '-O2', '-g', '-Wframe-larger-than=3072']
run(['clang', *flags, '-c', STAGE / 'info.c', '-o', STAGE / 'info.o'], 'compile-info')
cpp = ['-std=gnu++17', '-nostdinc++', '-fno-exceptions', '-fno-rtti', '-fapple-kext',
    '-mllvm', '-disable-atexit-based-global-dtor-lowering', '-Wno-deprecated-declarations',
    '-Wno-inconsistent-missing-override', '-Wno-unused-parameter']
for src, label in [(STAGE / 'YogaCursorDiag.cpp', 'YogaCursorDiag'), (LILU / 'Library/plugin_start.cpp', 'plugin_start')]:
    run(['clang++', *flags, *cpp, '-c', src, '-o', STAGE / (label + '.o')], 'compile-' + label)
binary = out / 'MacOS/YogaCursorDiag'
run([ROOT / 'cross-build/cctools-port/cctools/ld64/src/ld/ld', '-arch', 'x86_64', '-kext',
    '-export_dynamic', '-macosx_version_min', '13.0', STAGE / 'info.o', STAGE / 'YogaCursorDiag.o',
    STAGE / 'plugin_start.o', SDK / 'Library/x86_64/libkmod.a', '-o', binary], 'link')
undefined = run(['llvm-nm', '--undefined-only', binary], 'undefined-symbols')
for forbidden in ['atexit', '___atomic_', '_ml_nofault_copy', '_ml_vtophys', 'WriteRegister', 'pciConfigWrite']:
    assert forbidden not in undefined, forbidden
run(['llvm-nm', '--defined-only', binary], 'defined-symbols')
run(['llvm-objdump', '--macho', '--private-headers', binary], 'headers')
run(['llvm-objdump', '-d', '--x86-asm-syntax=intel', binary], 'disassembly')
signer = ROOT / 'cross-build/apple-codesign/apple-codesign-0.29.0-x86_64-unknown-linux-musl/rcodesign'
run([signer, 'sign', '--timestamp-url', 'none', out.parent], 'sign-new-plugin')
from audit_signature_pages import audit_file
audit = audit_file(binary)
assert audit['slices'] and all(s['directories'] for s in audit['slices'])
assert all(d['page_shift'] == 12 and d['code_hashes_valid'] for s in audit['slices'] for d in s['directories'])
(STAGE / 'signed-signature-audit.json').write_text(json.dumps(audit, indent=2) + '\n')
report = dict(binary=str(binary), sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),
    sources={p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in STAGE.iterdir() if p.suffix in ['.cpp', '.hpp']},
    external_writes=False, runtime_validated=False, deployable=False,
    reason='Local prototype only: kernel hook safety, physical Yoga identity and macOS loading not validated',
    activation_requires=['-yogacursordiag', '-yogacursor82ut'], disable_arg='-yogacursoroff',
    host_tests='ASan/UBSan, bounded arithmetic and conversion only', signed='ad-hoc, new diagnostic only')
(STAGE / 'build-report.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report, indent=2))
