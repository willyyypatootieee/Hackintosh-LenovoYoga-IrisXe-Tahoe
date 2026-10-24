"""Audit imports against exact installed kernel KPI sets and KC kext exports.

This is static evidence, not a replacement for native kmutil linking.
"""
from pathlib import Path
import hashlib
import json
import plistlib
import struct
import sys

ROOT = Path(__file__).resolve().parent
EVIDENCE = ROOT / 'kernel-evidence-readonly-20261023-221944'
NAME = sys.argv[1] if len(sys.argv) > 1 else 'fixed-hookcase-kernel-dtors-v3'
assert Path(NAME).name == NAME and NAME.startswith('fixed-hookcase')
STAGE = ROOT / 'cross-build' / NAME
STACK = ROOT / 'cross-build/gpu-stack-assembled'

def commands(data, base=0):
    assert struct.unpack_from('<I', data, base)[0] == 0xfeedfacf
    pos = base + 32
    for _ in range(struct.unpack_from('<I', data, base + 16)[0]):
        cmd, size = struct.unpack_from('<II', data, pos)
        assert size >= 8 and pos + size <= len(data)
        yield cmd, pos, size
        pos += size

def sections(data, base=0):
    for cmd, pos, _ in commands(data, base):
        if cmd == 0x19:
            for i in range(struct.unpack_from('<I', data, pos + 64)[0]):
                s = pos + 72 + i * 80
                name = data[s:s + 16].split(b'\0')[0].decode()
                seg = data[s + 16:s + 32].split(b'\0')[0].decode()
                size, offset = struct.unpack_from('<QI', data, s + 40)
                yield seg, name, offset, size

def plist_section(data, segment, section):
    off, size = next((off, size) for seg, sec, off, size in sections(data)
                     if seg == segment and sec == section)
    return plistlib.loads(data[off:off + size].rstrip(b'\0'))

def symbols(data, base=0):
    defined, undefined = set(), set()
    table = next((pos for cmd, pos, _ in commands(data, base) if cmd == 2), None)
    if table is None:
        return defined, undefined
    off, count, strings, stringsize = struct.unpack_from('<4I', data, table + 8)
    assert off + count * 16 <= len(data) and strings + stringsize <= len(data)
    for i in range(count):
        idx, kind, section, desc, value = struct.unpack_from('<IBBHQ', data, off + i * 16)
        if kind & 0xe0 or not kind & 1 or not idx:
            continue
        name = data[strings + idx:data.index(b'\0', strings + idx)].decode()
        if kind & 0xe == 0:
            undefined.add(name)
        elif not kind & 0x10:  # Exclude private externs from provider exports.
            defined.add(name)
    return defined, undefined

def symbol_records(data, base=0):
    pos = next(pos for cmd, pos, _ in commands(data, base) if cmd == 2)
    off, count, strings, _ = struct.unpack_from('<4I', data, pos + 8)
    result = []
    for i in range(count):
        idx, kind, section, desc, value = struct.unpack_from('<IBBHQ', data, off + i * 16)
        name = data[strings + idx:data.index(b'\0', strings + idx)].decode()
        result.append((name, kind, section, desc, value))
    return result

def file_offset(data, address, base=0):
    for cmd, pos, _ in commands(data, base):
        if cmd == 0x19:
            vm, size, offset, filesize = struct.unpack_from('<4Q', data, pos + 24)
            if vm <= address < vm + filesize:
                return offset + address - vm
    raise ValueError('Address not mapped: ' + hex(address))

def external_relocs(data):
    pos = next(pos for cmd, pos, _ in commands(data) if cmd == 0xb)
    offset, count = struct.unpack_from('<II', data, pos + 8 + 14 * 4)
    syms = symbol_records(data)
    result = []
    for i in range(count):
        address, bits = struct.unpack_from('<iI', data, offset + 8 * i)
        assert (bits >> 27) & 1
        result.append({'address': address, 'symbol': syms[bits & 0xffffff][0],
                       'pcrel': bool((bits >> 24) & 1), 'length': (bits >> 25) & 3,
                       'type': bits >> 28})
    return result

providers = {}
collection_images = {}
kernel_path = EVIDENCE / 'copies/system/System/Library/Kernels/kernel'
kernel = kernel_path.read_bytes()
symbolsets = plist_section(kernel, '__LINKINFO', '__symbolsets')
kernel_exports, _ = symbols(kernel)
for group in symbolsets['SymbolsSets']:
    names = {s['SymbolName'] for s in group['Symbols'] if 'SymbolName' in s}
    prefixes = tuple(s['SymbolPrefix'] for s in group['Symbols'] if 'SymbolPrefix' in s)
    names.update(s for s in kernel_exports if prefixes and s.startswith(prefixes))
    providers[group['CFBundleIdentifier']] = {
        'exports': names,
        'dependencies': {}, 'version': group['CFBundleVersion'], 'source': str(kernel_path),
    }
for path in sorted((EVIDENCE / 'copies/system/System/Library/KernelCollections').glob('*.kc')):
    data = path.read_bytes()
    info = plist_section(data, '__PRELINK_INFO', '__info')
    metadata = {p['CFBundleIdentifier']: p for p in info['_PrelinkInfoDictionary']}
    for cmd, pos, size in commands(data):
        if cmd != 0x80000035:
            continue
        vm, offset, stringoff, _ = struct.unpack_from('<QQII', data, pos + 8)
        name = data[pos + stringoff:pos + size].split(b'\0')[0].decode()
        if name == 'com.apple.kernel':
            continue  # Full kernel symbol table is NOT equivalent to KPI exports.
        exports, _ = symbols(data, offset)
        meta = metadata[name]
        providers[name] = {'exports': exports, 'dependencies': meta.get('OSBundleLibraries', {}),
                           'version': meta['CFBundleVersion'], 'source': str(path)}
        if name == 'com.apple.iokit.IOAcceleratorFamily2':
            collection_images[name] = (data, offset)

targets = []
for path in [STAGE / 'HookCase.kext',
             STACK / 'Library/Extensions/AppleIntelTGLGraphics.kext',
             STACK / 'Library/Extensions/AppleIntelTGLGraphicsFramebuffer.kext']:
    meta = plistlib.loads((path / 'Contents/Info.plist').read_bytes())
    binary = path / 'Contents/MacOS' / meta['CFBundleExecutable']
    exports, imports = symbols(binary.read_bytes())
    identifier = meta['CFBundleIdentifier']
    providers[identifier] = {'exports': exports, 'dependencies': meta.get('OSBundleLibraries', {}),
                            'version': meta['CFBundleVersion'], 'source': str(binary)}
    targets.append((identifier, binary, imports))

results = []
for identifier, binary, imports in targets:
    closure = set()
    absent = set()
    def visit(name):
        if name in closure or name == identifier:
            return
        closure.add(name)
        if name not in providers:
            absent.add(name)
            return
        for child in providers[name]['dependencies']:
            visit(child)
    for dependency in providers[identifier]['dependencies']:
        visit(dependency)
    resolved = {}
    unresolved = []
    for symbol in sorted(imports):
        matches = sorted(name for name in closure if symbol in providers.get(name, {}).get('exports', set()))
        if matches:
            resolved[symbol] = matches
        else:
            unresolved.append(symbol)
    # XNU's documented binary-compatibility mechanism patches inherited C++
    # vtable slots to the installed parent's slot. Do not equate missing legacy
    # method names with ordinary unresolved calls. Prove every use is a slot,
    # prove superclass metadata, then inspect the actual KC parent entry.
    vtable_evidence = []
    data = binary.read_bytes()
    records = symbol_records(data)
    relocations = external_relocs(data)
    by_address = {r['address']: r for r in relocations}
    vtables = sorted((r[4], r[0]) for r in records if r[0].startswith('__ZTV') and r[1] & 0xe)
    parent_data, parent_base = collection_images['com.apple.iokit.IOAcceleratorFamily2']
    parent_symbols = {r[0]: r[4] for r in symbol_records(parent_data, parent_base) if r[1] & 0xe}
    parent_reverse = {value: name for name, value in parent_symbols.items()}
    for symbol in unresolved:
        uses = [r for r in relocations if r['symbol'] == symbol]
        checked = []
        for use in uses:
            candidates = [v for v in vtables if v[0] <= use['address']]
            if not candidates or use['pcrel'] or use['length'] != 3 or use['type'] != 0:
                break
            start, vtable = candidates[-1]
            delta = use['address'] - start - 16
            if delta < 0 or delta % 8:
                break
            # The adjacent superClass symbol and its relocation must establish
            # the immediate parent; class-name guesses are not sufficient.
            class_part = vtable[len('__ZTV'):]
            super_name = '__ZN' + class_part + '10superClassE'
            super_record = next((r for r in records if r[0] == super_name), None)
            super_reloc = by_address.get(start - 8)
            if super_record is None or super_record[4] != start - 8 or super_reloc is None:
                break
            meta = super_reloc['symbol']
            if not meta.startswith('__ZN') or not meta.endswith('10gMetaClassE'):
                break
            parent_vtable = '__ZTV' + meta[len('__ZN'):-len('10gMetaClassE')]
            if parent_vtable not in parent_symbols:
                break
            slot = delta // 8
            ptr_address = parent_symbols[parent_vtable] + 16 + slot * 8
            packed = struct.unpack_from('<Q', parent_data, file_offset(parent_data, ptr_address, parent_base))[0]
            # Intel KC rebase pointers: dyld_chained_ptr_x86_64_kernel_cache_rebase
            # target:30/cacheLevel:2/diversity:16/addrDiv:1/key:2/next:12/isAuth:1.
            target, level, auth = packed & ((1 << 30) - 1), (packed >> 30) & 3, packed >> 63
            replacement = parent_reverse.get(target)
            if auth or level != 1 or replacement not in providers['com.apple.iokit.IOAcceleratorFamily2']['exports']:
                break
            if '_RESERVED' in replacement:
                break
            checked.append({'address': hex(use['address']), 'child_vtable': vtable,
                            'superclass_metadata': meta, 'parent_vtable': parent_vtable,
                            'slot': slot, 'parent_method': replacement,
                            'parent_address': hex(target), 'kc_cache_level': level})
        if uses and len(checked) == len(uses):
            vtable_evidence.append({'legacy_symbol': symbol, 'uses': checked})
    vtable_names = {item['legacy_symbol'] for item in vtable_evidence}
    remaining = sorted(set(unresolved) - vtable_names)
    results.append({'identifier': identifier, 'binary': str(binary),
                    'sha256': hashlib.file_digest(binary.open('rb'), 'sha256').hexdigest(),
                    'imports': len(imports), 'resolved_by_name': len(resolved),
                    'requires_native_vtable_patching': vtable_evidence, 'unresolved': remaining,
                    'missing_dependencies': sorted(absent), 'dependency_closure': sorted(closure),
                    'direct_dependencies': providers[identifier]['dependencies'],
                    'resolution': resolved, 'passed': not remaining and not absent})
report = {'method': 'KPI __LINKINFO,__symbolsets plus external non-private defined KC kext symbols; dependency closure enforced',
          'os_build': '25G241', 'native_kmutil_validated': False,
          'providers': {name: {'exports': len(p['exports']), 'version': p['version']} for name, p in providers.items()},
          'results': results, 'passed': all(r['passed'] for r in results)}
(STAGE / 'installed-tahoe-import-audit.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps({**{k: v for k, v in report.items() if k not in ['results', 'providers']},
                  'results': [{k: v for k, v in r.items() if k != 'resolution'} for r in results]}, indent=2))
sys.exit(0 if report['passed'] else 1)
