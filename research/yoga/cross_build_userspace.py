"""Compile reference user-space adapters on Linux; never deploy partial output."""
from pathlib import Path
import hashlib
import json
import subprocess

ROOT = Path(__file__).resolve().parent
REF = Path('/home/willy/Downloads/Hackintosh-XPS13Plus-IrisXe-Tahoe')
SDK = ROOT / 'cross-build/MacOSX15.5.sdk'
OUT = ROOT / 'cross-build/userspace'
OUT.mkdir(parents=True, exist_ok=True)
commands = []


def run(command, log):
    command = [str(x) for x in command]
    commands.append(command)
    result = subprocess.run(command, capture_output=True, text=True)
    (OUT / log).write_text(result.stdout + result.stderr)
    (OUT / 'commands.json').write_text(json.dumps(commands, indent=2) + '\n')
    result.check_returncode()


flags = ['-target', 'x86_64-apple-macos15', '-isysroot', SDK, '-O2']
run(['clang', *flags, '-fno-objc-arc', '-c', REF / 'macos/src/libxedrv.m',
     '-o', OUT / 'libxedrv.o'], 'libxedrv.compile.log')
for name in ['compiler-adapter', 'llvm-bridge']:
    run(['clang++', *flags, '-stdlib=libc++', '-std=c++17', '-c',
         REF / ('macos/src/' + name + '.cpp'), '-o', OUT / (name + '.o')],
        name + '.compile.log')
run(['ld64.lld', '-arch', 'x86_64', '-dylib', '-platform_version', 'macos',
     '15.0', '15.5', '-syslibroot', SDK, '-L' + str(SDK / 'usr/lib'),
     '-F' + str(SDK / 'System/Library/Frameworks'), '-framework', 'Foundation',
     '-framework', 'Metal', '-framework', 'IOSurface', '-lobjc', '-lSystem',
     '-install_name', '/Library/GPUBundles/AppleIntelTGLGraphicsMTLDriver.bundle/Contents/MacOS/libxedrv.dylib',
     OUT / 'libxedrv.o', '-adhoc_codesign', '-o', OUT / 'libxedrv.dylib'],
    'libxedrv.link.log')
report = dict(result='Adapters compiled; only libxedrv linked; not deployed or runtime-tested',
              libxedrv_sha256=hashlib.sha256((OUT / 'libxedrv.dylib').read_bytes()).hexdigest(),
              sdk='MacOSX15.5 SDK, archive verified against publisher SHA256',
              remaining='Exact Tahoe libLLVM exports for reference reexport linkage; complete bundle signing and automatic installation')
(OUT / 'verification.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report, indent=2))
