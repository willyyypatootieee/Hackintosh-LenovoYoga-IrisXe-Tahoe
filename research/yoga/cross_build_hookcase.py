"""Cross-build the reference's fixed HookCase on Linux; local staging only.

Adapts upstream build-hookcase.sh to cctools-port ld64, not ld64.lld.
No live EFI, installer, or target-volume writes.
"""
from pathlib import Path
import hashlib
import json
import plistlib
import subprocess
import sys

ROOT = Path(__file__).resolve().parent
REF = Path('/home/willy/Downloads/Hackintosh-XPS13Plus-IrisXe-Tahoe')
SRC = REF / 'src/HookCase'
SDK = Path('/path/to/local-workspace/hackintosh-yoga/xe-lab/NootedGreen/MacKernelSDK')
LD = ROOT / 'cross-build/cctools-port/cctools/ld64/src/ld/ld'
NAME = sys.argv[1] if len(sys.argv) > 1 else 'fixed-hookcase-kernel-dtors-v3'
assert Path(NAME).name == NAME and NAME.startswith('fixed-hookcase-')
STAGE = ROOT / 'cross-build' / NAME
STAGE.mkdir(parents=True, exist_ok=False)
OUT = STAGE / 'HookCase.kext/Contents'
(OUT / 'MacOS').mkdir(parents=True, exist_ok=True)
commands = []


def run(command, name):
    command = [str(x) for x in command]
    commands.append(command)
    result = subprocess.run(command, capture_output=True, text=True)
    (STAGE / (name + '.log')).write_text(result.stdout + result.stderr)
    (STAGE / 'commands.json').write_text(json.dumps(commands, indent=2) + '\n')
    if result.returncode:
        raise RuntimeError(name + ' failed: ' + result.stderr[-6000:])
    return result.stdout


info = plistlib.loads((SRC / 'Info.plist').read_bytes())
substitutions = {'$(EXECUTABLE_NAME)': 'HookCase',
                 '$(PRODUCT_BUNDLE_IDENTIFIER)': 'org.smichaud.HookCase',
                 '$(PRODUCT_NAME)': 'HookCase', '$(MODULE_VERSION)': '8.0.1',
                 '$(DEVELOPMENT_LANGUAGE)': 'en'}
for key, value in list(info.items()):
    if isinstance(value, str):
        info[key] = substitutions.get(value, value)
info.pop('IOKitPersonalities', None)
(OUT / 'Info.plist').write_bytes(plistlib.dumps(info))
(STAGE / 'info.c').write_text('''#include <mach/mach_types.h>
extern kern_return_t HookCase_kern_start(kmod_info_t *, void *);
extern kern_return_t HookCase_kern_stop(kmod_info_t *, void *);
__attribute__((visibility("default"))) KMOD_EXPLICIT_DECL(org.smichaud.HookCase, "8.0.1", HookCase_kern_start, HookCase_kern_stop)
__private_extern__ kmod_start_func_t *_realmain = 0;
__private_extern__ kmod_stop_func_t *_antimain = 0;
__private_extern__ int _kext_apple_cc = __APPLE_CC__;
''')
flags = ['-target', 'x86_64-apple-macos13', '-mkernel', '-nostdinc',
         '-fno-builtin', '-fno-common', '-fno-stack-protector', '-mmmx', '-msse',
         '-DKERNEL', '-DKERNEL_PRIVATE', '-DDRIVER_PRIVATE', '-DAPPLE', '-DNeXT',
         '-DDEBUG=1', '-D__null_terminated=', '-D__stateful_pure=',
         '-isystem', str(SDK / 'Headers'), '-I' + str(SRC), '-O0', '-g']
run(['clang', *flags, '-c', STAGE / 'info.c', '-o', STAGE / 'info.o'], 'compile-info')
run(['clang', *flags, '-c', SRC / 'HookCase.s', '-o', STAGE / 'HookCase_s.o'], 'compile-asm')
run(['clang++', *flags, '-std=gnu++14', '-nostdinc++', '-fno-exceptions',
     '-fno-rtti', '-fapple-kext',
     # Upstream LLVM 23 otherwise lowers llvm.global_dtors into a user-space
     # __cxa_atexit call despite -mkernel/-fapple-kext/-fno-use-cxa-atexit.
     # Keep real __mod_term_func entries for the kernel's C++ lifecycle.
     '-mllvm', '-disable-atexit-based-global-dtor-lowering',
     '-Wno-deprecated-declarations',
     '-Wno-inconsistent-missing-override', '-Wno-unused-parameter',
     '-c', SRC / 'HookCase.cpp', '-o', STAGE / 'HookCase.o'], 'compile-cpp')
binary = OUT / 'MacOS/HookCase'
run([LD, '-arch', 'x86_64', '-kext', '-export_dynamic', '-macosx_version_min',
     '13.0', STAGE / 'info.o', STAGE / 'HookCase_s.o', STAGE / 'HookCase.o',
     SDK / 'Library/x86_64/libkmod.a', '-o', binary], 'link')
symbols = run(['llvm-nm', '--defined-only', binary], 'defined-symbols')
assert any(line.endswith(' T _strnstr') for line in symbols.splitlines())
import struct
assert struct.unpack_from('<I', binary.read_bytes(), 12)[0] == 11  # MH_KEXT_BUNDLE
undefined = run(['llvm-nm', '--undefined-only', binary], 'undefined-symbols').split()
assert not any('atexit' in x for x in undefined)
headers = run(['llvm-objdump', '--section-headers', binary], 'section-headers')
assert '__mod_init_func' in headers and '__mod_term_func' in headers
assert '__GLOBAL__D_a' in symbols
run(['llvm-objdump', '-dr', STAGE / 'HookCase.o'], 'cpp-relocations')
report = dict(result='Cross-linked fixed HookCase, not runtime validated or deployed',
              sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),
              source_commit=run(['git', '-C', REF, 'rev-parse', 'HEAD'], 'source-commit').strip(),
              linker_commit=run(['git', '-C', LD.parents[4], 'rev-parse', 'HEAD'], 'linker-commit').strip(),
              filetype='MH_KEXT_BUNDLE', strnstr_exported=True,
              personality_removed=True, signed=False, live_disk_changes=False,
              undefined_imports=len(undefined), atexit_imports=False,
              kernel_destructor_section_preserved=True,
              native_kmutil_validated=False)
(STAGE / 'verification.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report, indent=2))
