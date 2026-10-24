"""Linux-only local cross-build. Does not mount/write external volumes."""
from pathlib import Path
import hashlib
import json
import plistlib
import subprocess
import sys

ROOT = Path(__file__).resolve().parent
SOURCE = ROOT / 'yoga-gms-runtime/YogaGMSFix.cpp'
DEPS = Path('/path/to/local-workspace/hackintosh-yoga/xe-lab/NootedGreen')
SDK = DEPS / 'MacKernelSDK'
LILU = DEPS / 'Lilu.kext/Contents/Resources'
LD = ROOT / 'cross-build/cctools-port/cctools/ld64/src/ld/ld'
NAME = sys.argv[1] if len(sys.argv) > 1 else 'yoga-gms-runtime-v2'
assert Path(NAME).name == NAME and NAME.startswith('yoga-gms-runtime-')
STAGE = ROOT / 'cross-build' / NAME
STAGE.mkdir(exist_ok=False)
OUT = STAGE / 'YogaGMSFix.kext/Contents'
(OUT / 'MacOS').mkdir(parents=True)
commands = []


def run(command, name):
    command = list(map(str, command))
    commands.append(command)
    r = subprocess.run(command, text=True, capture_output=True)
    (STAGE / (name + '.log')).write_text(r.stdout + r.stderr)
    (STAGE / 'commands.json').write_text(json.dumps(commands, indent=2) + '\n')
    if r.returncode:
        raise RuntimeError(name + ': ' + r.stderr[-7000:])
    return r.stdout


info = dict(CFBundleDevelopmentRegion='en', CFBundleExecutable='YogaGMSFix',
            CFBundleIdentifier='local.yoga.driver.YogaGMSFix', CFBundleInfoDictionaryVersion='6.0',
            CFBundleName='YogaGMSFix', CFBundlePackageType='KEXT', CFBundleShortVersionString='1.0.0',
            CFBundleVersion='1.0.0', OSBundleRequired='Root',
            OSBundleLibraries={'as.vit9696.Lilu': '1.7.0', 'com.StezzaPilot.NootedGreen': '1.0.0',
                               'com.apple.kpi.bsd': '12.0.0', 'com.apple.kpi.dsep': '12.0.0',
                               'com.apple.kpi.iokit': '12.0.0',
                               'com.apple.kpi.libkern': '12.0.0', 'com.apple.kpi.mach': '12.0.0',
                               'com.apple.kpi.unsupported': '12.0.0'},
            IOKitPersonalities={'YogaGMSFix': dict(CFBundleIdentifier='local.yoga.driver.YogaGMSFix',
                IOClass='YogaGMSFix', IOMatchCategory='YogaGMSFix', IOProviderClass='IOResources',
                IOResourceMatch='IOKit', IOProbeScore=1000)})
(OUT / 'Info.plist').write_bytes(plistlib.dumps(info))
(STAGE / 'YogaGMSFix.cpp').write_bytes(SOURCE.read_bytes())
(STAGE / 'info.c').write_text('''#include <mach/mach_types.h>
extern kern_return_t YogaGMSFix_kern_start(kmod_info_t *, void *);
extern kern_return_t YogaGMSFix_kern_stop(kmod_info_t *, void *);
__attribute__((visibility("default"))) KMOD_EXPLICIT_DECL(local.yoga.driver.YogaGMSFix, "1.0.0", YogaGMSFix_kern_start, YogaGMSFix_kern_stop)
__private_extern__ kmod_start_func_t *_realmain = 0;
__private_extern__ kmod_stop_func_t *_antimain = 0;
__private_extern__ int _kext_apple_cc = __APPLE_CC__;
''')
flags = ['-target', 'x86_64-apple-macos13', '-mkernel', '-nostdinc', '-fno-builtin',
         '-fno-common', '-fno-stack-protector', '-mmmx', '-msse', '-msse2',
         '-DKERNEL', '-DKERNEL_PRIVATE', '-DDRIVER_PRIVATE', '-DAPPLE', '-DNeXT',
         '-DDEBUG=1', '-DAPPLE_KEXT_ASSERTIONS=1', '-DPRODUCT_NAME=YogaGMSFix',
         '-DMODULE_VERSION=1.0.0', '-D__null_terminated=', '-D__stateful_pure=',
         '-isystem', SDK / 'Headers', '-I' + str(LILU), '-O2', '-g']
run(['clang', *flags, '-c', STAGE / 'info.c', '-o', STAGE / 'info.o'], 'compile-info')
cppflags = ['-std=gnu++17', '-nostdinc++', '-fno-exceptions', '-fno-rtti', '-fapple-kext',
            '-mllvm', '-disable-atexit-based-global-dtor-lowering', '-Wno-deprecated-declarations',
            '-Wno-inconsistent-missing-override', '-Wno-unused-parameter']
for source, name in [(STAGE / 'YogaGMSFix.cpp', 'YogaGMSFix'), (LILU / 'Library/plugin_start.cpp', 'plugin_start')]:
    run(['clang++', *flags, *cppflags, '-c', source, '-o', STAGE / (name + '.o')], 'compile-' + name)
binary = OUT / 'MacOS/YogaGMSFix'
run([LD, '-arch', 'x86_64', '-kext', '-export_dynamic', '-macosx_version_min', '13.0',
     STAGE / 'info.o', STAGE / 'YogaGMSFix.o', STAGE / 'plugin_start.o',
     SDK / 'Library/x86_64/libkmod.a', '-o', binary], 'link')
undefined = run(['llvm-nm', '--undefined-only', binary], 'undefined-symbols')
assert 'atexit' not in undefined
run(['llvm-nm', '--defined-only', binary], 'defined-symbols')
run(['llvm-objdump', '--macho', '--private-headers', binary], 'headers')
signer = ROOT / 'cross-build/apple-codesign/apple-codesign-0.29.0-x86_64-unknown-linux-musl/rcodesign'
run([signer, 'sign', '--timestamp-url', 'none', OUT.parent], 'sign-new-plugin')
from audit_signature_pages import audit_file
audit = audit_file(binary)
assert all(d['page_shift'] == 12 and d['code_hashes_valid']
           for s in audit['slices'] for d in s['directories'])
(STAGE / 'signed-signature-audit.json').write_text(json.dumps(audit, indent=2) + '\n')
report = dict(binary=str(binary), sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),
              source_sha256=hashlib.sha256(SOURCE.read_bytes()).hexdigest(),
              external_writes=False, runtime_validated=False, signed='ad-hoc, new plugin only')
(STAGE / 'build-report.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report, indent=2))
