"""Assemble reference GPU bundles locally, never write to EFI/physical disks.

Uses verified donor copies and exact Tahoe export stub. Existing output is
never erased: choose a fresh output directory for each build.
"""
from pathlib import Path
import hashlib
import json
import plistlib
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parent
REF = Path('/home/willy/Downloads/Hackintosh-XPS13Plus-IrisXe-Tahoe')
DONOR = Path('/home/willy/Videos/EFI-backup-20261011-003339/OC/_ngreen/sle_Internal')
BUILD = ROOT / 'cross-build'
OUT = BUILD / (sys.argv[1] if len(sys.argv) > 1 else 'gpu-stack-assembled')
assert OUT.parent == BUILD and not OUT.exists(), 'Use a fresh local output name'
OUT.mkdir()
SIGN = next((BUILD / 'apple-codesign').rglob('rcodesign'))
SDK = BUILD / 'MacOSX15.5.sdk'
commands = []
patch_log = []

def run(args, label):
    args = [str(x) for x in args]
    commands.append(args)
    result = subprocess.run(args, capture_output=True, text=True)
    (OUT / (label+'.log')).write_text(result.stdout+result.stderr)
    (OUT / 'commands.json').write_text(json.dumps(commands, indent=2)+'\n')
    if result.returncode:
        raise RuntimeError(label+': '+result.stderr[-5000:])

def copy(source, dest):
    dest.parent.mkdir(parents=True, exist_ok=True)
    shutil.copytree(source, dest, symlinks=True,
                    ignore=shutil.ignore_patterns('._*', '.DS_Store'))

def patches(path, name):
    data = bytearray(path.read_bytes())
    for p in json.loads((REF / 'macos' / name).read_text()):
        offset, old, new = p['off'], bytes.fromhex(p['old']), bytes.fromhex(p['new'])
        assert data[offset:offset+len(old)] == old, (name, hex(offset))
        data[offset:offset+len(new)] = new
        patch_log.append(dict(file=str(path.relative_to(OUT)), offset=offset, old=old.hex(), new=new.hex()))
    path.write_bytes(data)

def sign(path, label):
    run([SIGN, 'sign', '-C', '/dev/null', '--timestamp-url', 'none', path], label)

def dylib(obj, dest, install_name, extra):
    run(['ld64.lld', '-arch', 'x86_64', '-dylib', '-platform_version', 'macos', '15.0', '15.5',
         '-syslibroot', SDK, '-L'+str(SDK/'usr/lib'), '-lc++', '-lSystem',
         '-install_name', install_name, obj, *extra, '-adhoc_codesign', '-o', dest], dest.stem+'.link')

le = OUT / 'Library/Extensions'
for name in ['AppleIntelTGLGraphics.kext', 'AppleIntelTGLGraphicsFramebuffer.kext']:
    copy(DONOR/'le'/name, le/name)
copy(BUILD/'fixed-hookcase/HookCase.kext', le/'HookCase.kext')
info_path = le/'AppleIntelTGLGraphics.kext/Contents/Info.plist'
info = plistlib.loads(info_path.read_bytes())
info['IOKitPersonalities']['Gen7']['IOPCIPrimaryMatch'] = '0x9a498086'
info['CFBundleVersion'] = '16.0.2'
info_path.write_bytes(plistlib.dumps(info))
for name in ['AppleIntelTGLGraphicsMTLDriver', 'AppleIntelTGLGraphicsGLDriver',
             'AppleIntelTGLGraphicsVADriver', 'AppleIntelTGLGraphicsVAME', 'AppleIntelGraphicsShared']:
    copy(DONOR/'sle'/(name+'.bundle'), le/(name+'.bundle'))

gpu = OUT/'Library/GPUBundles'
for name in ['AppleIntelTGLGraphicsMTLDriver', 'AppleIntelGraphicsShared']:
    copy(DONOR/'sle'/(name+'.bundle'), gpu/(name+'.bundle'))
mtl = gpu/'AppleIntelTGLGraphicsMTLDriver.bundle/Contents'
patches(mtl/'MacOS/AppleIntelTGLGraphicsMTLDriver', 'mtl17-patches.json')
info = plistlib.loads((mtl/'Info.plist').read_bytes())
info.setdefault('Development', {})['PoshDisable'] = 1
(mtl/'Info.plist').write_bytes(plistlib.dumps(info))
shutil.copy2(BUILD/'userspace/libxedrv.dylib', mtl/'MacOS/libxedrv.dylib')

xe = OUT/'System/Library/Caches/xe.bundle/Contents'
(xe/'MacOS').mkdir(parents=True)
(xe/'Resources').mkdir()
shared = DONOR/'sle/AppleIntelGraphicsShared.bundle/Contents'
for name in ['Info.plist', 'PkgInfo', 'version.plist']:
    shutil.copy2(shared/name, xe/name)
for path in (shared/'Resources').iterdir():
    if path.name.startswith('._'):
        continue
    if path.is_dir():
        copy(path, xe/'Resources'/path.name)
    else:
        shutil.copy2(path, xe/'Resources'/path.name)
shutil.copy2(DONOR/'sle/AppleIntelTGLGraphicsMTLDriver.bundle/Contents/Resources/runtime.gpuencode.split', xe/'Resources/runtime.gpuencode.split')
for name in ['libigc.dylib', 'libigcHSW.dylib', 'libIGIL-Metal.dylib', 'libISAProfilerDyn.dylib']:
    shutil.copy2(shared/'MacOS'/name, xe/'MacOS'/name)
dest = '/System/Library/Caches/xe.bundle/Contents/MacOS/'
stub = BUILD/'userspace/libLLVM32023.tbd'
dylib(BUILD/'userspace/llvm-bridge.o', xe/'MacOS/libLLVMbridge.dylib', dest+'libLLVMbridge.dylib',
      ['-compatibility_version', '1.0.0', '-current_version', '3902.30.1', '-reexport_library', stub])
plugin = xe/'MacOS/libMTLIGCCompilerPluginOld.dylib'
shutil.copy2(shared/'MacOS/libMTLIGCCompilerPlugin.dylib', plugin)
# Reference patches are file-offset anchored; rename load paths without moving sections.
patches(plugin, 'plugin-old-patches.json')
run(['llvm-install-name-tool', '-id', dest+'libMTLIGCCompilerPluginOld.dylib', '-change',
     '/System/Library/PrivateFrameworks/GPUCompiler.framework/Versions/3902/Libraries/libLLVM.dylib',
     dest+'libLLVMbridge.dylib', plugin], 'plugin-install-names')
dylib(BUILD/'userspace/compiler-adapter.o', xe/'MacOS/libMTLIGCCompilerPlugin.dylib', dest+'libMTLIGCCompilerPlugin.dylib',
      [stub, '-reexport_library', plugin])
for directory in [le, gpu]:
    for path in directory.iterdir():
        sign(path, 'sign-'+directory.name+'-'+path.name)
sign(xe.parent, 'sign-xe.bundle')
manifest = []
for path in sorted(OUT.rglob('*')):
    if path.is_file() and path.suffix != '.log' and path.name != 'commands.json':
        manifest.append(dict(path=str(path.relative_to(OUT)), bytes=path.stat().st_size,
                             sha256=hashlib.file_digest(path.open('rb'), 'sha256').hexdigest()))
(OUT/'patches-applied.json').write_text(json.dumps(patch_log, indent=2)+'\n')
(OUT/'manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')
print(json.dumps(dict(output=str(OUT), files=len(manifest), byte_patches=len(patch_log),
                      signed='ad-hoc using rcodesign; needs independent verification',
                      deployed=False, runtime_validated=False), indent=2))
