"""Build experimental, opt-in cursor candidate locally on Linux; no deployment."""
from pathlib import Path
import hashlib
import json
import plistlib
import subprocess
import sys
from datetime import datetime

ROOT = Path(__file__).resolve().parent
DEPS = Path('/path/to/local-workspace/hackintosh-yoga/xe-lab/NootedGreen')
SDK = DEPS / 'MacKernelSDK'
LILU = DEPS / 'Lilu.kext/Contents/Resources'
LD = ROOT / 'cross-build/cctools-port/cctools/ld64/src/ld/ld'
PRODUCT = 'YogaCursorArb'
NAME = 'yoga-cursor-arb-' + datetime.now().strftime('%Y%m%d-%H%M%S')
STAGE = ROOT / 'cross-build' / NAME


def main():
    subprocess.run([sys.executable, str(ROOT / 'audit_cursor_arb.py')], check=True)
    STAGE.mkdir(exist_ok=False)
    out = STAGE / (PRODUCT + '.kext/Contents')
    (out / 'MacOS').mkdir(parents=True)
    commands = []

    def run(command, name):
        command = list(map(str, command))
        commands.append(command)
        result = subprocess.run(command, text=True, capture_output=True)
        (STAGE / (name + '.log')).write_text(result.stdout + result.stderr)
        (STAGE / 'commands.json').write_text(json.dumps(commands, indent=2) + '\n')
        if result.returncode:
            raise RuntimeError(name + ': ' + result.stderr[-7000:])
        return result.stdout

    info = dict(CFBundleDevelopmentRegion='en', CFBundleExecutable=PRODUCT,
                CFBundleIdentifier='local.yoga.driver.' + PRODUCT, CFBundleInfoDictionaryVersion='6.0',
                CFBundleName=PRODUCT, CFBundlePackageType='KEXT', CFBundleShortVersionString='0.1.0',
                CFBundleVersion='0.1.0', OSBundleRequired='Root',
                OSBundleLibraries={'as.vit9696.Lilu': '1.7.0', 'com.StezzaPilot.NootedGreen': '1.0.0',
                    **{'com.apple.kpi.' + k: '12.0.0' for k in ('bsd', 'dsep', 'iokit', 'libkern', 'mach', 'unsupported')}},
                IOKitPersonalities={PRODUCT: dict(CFBundleIdentifier='local.yoga.driver.' + PRODUCT,
                    IOClass=PRODUCT, IOMatchCategory=PRODUCT, IOProviderClass='IOResources',
                    IOResourceMatch='IOKit', IOProbeScore=1000)})
    (out / 'Info.plist').write_bytes(plistlib.dumps(info))
    for name in (PRODUCT + '.cpp', 'CursorArbGuard.hpp', 'input-audit.json'):
        (STAGE / name).write_bytes((ROOT / 'yoga-cursor-arb' / name).read_bytes())
    (STAGE / 'info.c').write_text('''#include <mach/mach_types.h>
extern kern_return_t YogaCursorArb_kern_start(kmod_info_t *, void *);
extern kern_return_t YogaCursorArb_kern_stop(kmod_info_t *, void *);
__attribute__((visibility("default"))) KMOD_EXPLICIT_DECL(local.yoga.driver.YogaCursorArb, "0.1.0", YogaCursorArb_kern_start, YogaCursorArb_kern_stop)
__private_extern__ kmod_start_func_t *_realmain = 0;
__private_extern__ kmod_stop_func_t *_antimain = 0;
__private_extern__ int _kext_apple_cc = __APPLE_CC__;
''')
    flags = ['-target', 'x86_64-apple-macos13', '-mkernel', '-nostdinc', '-fno-builtin',
             '-fno-common', '-fno-stack-protector', '-mmmx', '-msse', '-msse2',
             '-DKERNEL', '-DKERNEL_PRIVATE', '-DDRIVER_PRIVATE', '-DAPPLE', '-DNeXT',
             '-DDEBUG=1', '-DAPPLE_KEXT_ASSERTIONS=1', '-DPRODUCT_NAME=' + PRODUCT,
             '-DMODULE_VERSION=0.1.0', '-D__null_terminated=', '-D__stateful_pure=',
             '-isystem', SDK / 'Headers', '-I' + str(LILU), '-O2', '-g']
    run(['clang', *flags, '-c', STAGE / 'info.c', '-o', STAGE / 'info.o'], 'compile-info')
    cppflags = ['-std=gnu++17', '-nostdinc++', '-fno-exceptions', '-fno-rtti', '-fapple-kext',
                '-mllvm', '-disable-atexit-based-global-dtor-lowering', '-Wno-deprecated-declarations',
                '-Wno-inconsistent-missing-override', '-Wno-unused-parameter']
    for source, name in ((STAGE / (PRODUCT + '.cpp'), PRODUCT), (LILU / 'Library/plugin_start.cpp', 'plugin_start')):
        run(['clang++', *flags, *cppflags, '-c', source, '-o', STAGE / (name + '.o')], 'compile-' + name)
    binary = out / ('MacOS/' + PRODUCT)
    run([LD, '-arch', 'x86_64', '-kext', '-export_dynamic', '-macosx_version_min', '13.0',
         STAGE / 'info.o', STAGE / (PRODUCT + '.o'), STAGE / 'plugin_start.o',
         SDK / 'Library/x86_64/libkmod.a', '-o', binary], 'link')
    undefined = run(['llvm-nm', '--undefined-only', binary], 'undefined-symbols')
    assert 'atexit' not in undefined
    run(['llvm-nm', '--defined-only', binary], 'defined-symbols')
    run(['llvm-objdump', '-dr', '--x86-asm-syntax=intel', binary], 'disassembly')
    run(['llvm-objdump', '--macho', '--private-headers', binary], 'headers')
    signer = ROOT / 'cross-build/apple-codesign/apple-codesign-0.29.0-x86_64-unknown-linux-musl/rcodesign'
    run([signer, 'sign', '--timestamp-url', 'none', out.parent], 'sign-new-plugin')
    from audit_signature_pages import audit_file
    audit = audit_file(binary)
    assert all(d['page_shift'] == 12 and d['code_hashes_valid'] for s in audit['slices'] for d in s['directories'])
    (STAGE / 'signed-signature-audit.json').write_text(json.dumps(audit, indent=2) + '\n')
    report = dict(binary=str(binary), sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),
                  source_sha256=hashlib.sha256((STAGE / (PRODUCT + '.cpp')).read_bytes()).hexdigest(),
                  status='experimental opt-in candidate; NOT deployed', external_writes=False,
                  runtime_validated=False, signed='ad-hoc, new plugin only')
    (STAGE / 'build-report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
