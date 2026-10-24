"""Static verification for official SimpleHDA EFI injection, not runtime proof."""
from pathlib import Path
import ast
import hashlib
import json
import plistlib
import re
import struct
import subprocess
from audit_signature_pages import audit_file

ROOT = Path(__file__).resolve().parent
AUDIT = ROOT / 'simplehda-v1.0.0-audit'
RELEASE = AUDIT / 'release/SimpleHDA-Tahoe-v1.0.0-RELEASE'
PLUGIN = RELEASE / 'SimpleHDA.kext'

def verify():
    sha = lambda b: hashlib.sha256(b).hexdigest()
    assert sha((AUDIT / 'SimpleHDA-Tahoe-v1.0.0-RELEASE.zip').read_bytes()) == '89198256664078c91155d673ee7cebb08134dad1b71d5e34b0aa4ef0d4877f8f'
    for name, digest in json.loads((RELEASE / 'SHA256.json').read_text()).items():
        assert sha((RELEASE / name).read_bytes()) == digest
    source = ast.parse((ROOT / 'audit_kernel_imports.py').read_text())
    code = ast.Module(body=[n for n in source.body if isinstance(n, ast.FunctionDef) and n.name in ['commands', 'sections', 'plist_section', 'symbols']], type_ignores=[])
    ns = dict(struct=struct, plistlib=plistlib)
    exec(compile(code, 'existing-MachO-readonly-helpers', 'exec'), ns)
    symbols, psc = ns['symbols'], ns['plist_section']
    evidence = ROOT / 'kernel-evidence-readonly-20261023-221944/copies/system'
    kernel = (evidence / 'System/Library/Kernels/kernel').read_bytes()
    exports, _ = symbols(kernel)
    providers = {}
    for group in psc(kernel, '__LINKINFO', '__symbolsets')['SymbolsSets']:
        names = {s['SymbolName'] for s in group['Symbols'] if 'SymbolName' in s}
        prefixes = tuple(s['SymbolPrefix'] for s in group['Symbols'] if 'SymbolPrefix' in s)
        names.update(s for s in exports if prefixes and s.startswith(prefixes))
        providers[group['CFBundleIdentifier']] = names
    lilu = ROOT / 'installed-external-readonly-20261023-231719/EFI/OC/Kexts/Lilu.kext/Contents/MacOS/Lilu'
    providers['as.vit9696.Lilu'] = {l.split()[-1] for l in subprocess.check_output(['llvm-nm', '--arch=x86_64', '--defined-only', str(lilu)], text=True).splitlines() if l.split()}
    binary = PLUGIN / 'Contents/MacOS/SimpleHDA'
    meta = plistlib.loads((PLUGIN / 'Contents/Info.plist').read_bytes())
    _, imports = symbols(binary.read_bytes())
    declared = set().union(*(providers[x] for x in meta['OSBundleLibraries']))
    kpi_only_missing = sorted(imports - declared)
    # OpenCore KC injection resolves kernel symbols directly, unlike native
    # kmutil linking through KPI export sets. Verify exact BootKC kernel too.
    kc = (evidence / 'System/Library/KernelCollections/BootKernelExtensions.kc').read_bytes()
    kernel_offset = None
    kc_ids = set()
    for cmd, pos, size in ns['commands'](kc):
        if cmd == 0x80000035:
            _, off, nameoff, _ = struct.unpack_from('<QQII', kc, pos + 8)
            name = kc[pos + nameoff:pos + size].split(b'\0')[0].decode()
            kc_ids.add(name)
            if name == 'com.apple.kernel': kernel_offset = off
    assert kernel_offset is not None
    boot_exports, _ = symbols(kc, kernel_offset)
    missing = sorted(imports - (declared | boot_exports))
    assert not missing, missing
    assert kpi_only_missing == ['_strnstr']
    dynamic = set(re.findall(r'"((?:__ZN|_PE_|_kext_)[^"]+)"', (AUDIT / 'source/Sources/kern_start.cpp').read_text()))
    assert dynamic and not dynamic - boot_exports, sorted(dynamic - boot_exports)
    assert not any('AppleHDA' in name or name.endswith('IOHDAFamily') for name in kc_ids)
    sig = audit_file(binary)
    assert len(sig['slices']) == 1 and sig['slices'][0]['cpu'] == '0x1000007' and sig['slices'][0]['filetype'] == 11
    # The official release is unsigned and kept byte-for-byte; existing OC
    # injection and SIP settings already permit external EFI kext injection.
    assert sig['slices'][0]['unsigned']
    return dict(package_manifest_verified=True, binary_sha256=sha(binary.read_bytes()), imports=len(imports),
                missing_with_kpi_only=kpi_only_missing, missing_with_oc_bootkc_kernel=[],
                dynamic_kernel_symbols_verified=sorted(dynamic), no_applehda_in_bootkc=True,
                signature=sig, native_runtime_validated=False)

if __name__ == '__main__':
    report = verify()
    (AUDIT / 'audit.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))
