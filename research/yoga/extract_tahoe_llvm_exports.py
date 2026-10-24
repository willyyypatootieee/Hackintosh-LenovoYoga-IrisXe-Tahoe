"""Extract exact LLVM 32023 exports from source installer dyld cache data.

Read-only source inspection. Writes only a local linker stub and provenance.
"""
from pathlib import Path
import hashlib
import json
import mmap
import struct

ROOT = Path(__file__).resolve().parent
SOURCE = ROOT / 'cross-build/cryptex-patchdata.decoded'
OUT = ROOT / 'cross-build/userspace'
LLVM = '/System/Library/PrivateFrameworks/GPUCompiler.framework/Versions/32023/Libraries/libLLVM.dylib'

with SOURCE.open('rb') as file, mmap.mmap(file.fileno(), 0, access=mmap.ACCESS_READ) as data:
    mappings = []
    pos = 0
    while True:
        pos = data.find(b'dyld_v1 x86_64h\0', pos)
        if pos < 0:
            break
        offset, count = struct.unpack_from('<II', data, pos + 16)
        if offset == 552 and 0 < count < 10:
            for i in range(count):
                address, size, file_offset, _, _ = struct.unpack_from('<QQQII', data, pos + offset + i * 32)
                mappings.append((address, size, pos + file_offset, pos))
        pos += 16

    def vm(address):
        matches = [m for m in mappings if m[0] <= address < m[0] + m[1]]
        assert len(matches) == 1, (hex(address), matches)
        m = matches[0]
        return m[2] + address - m[0]

    images_offset, images_count = struct.unpack_from('<II', data, 448)
    found = []
    for i in range(images_count):
        address, _, _, path_offset, _ = struct.unpack_from('<QQQII', data, images_offset + i * 32)
        path = data[path_offset:data.find(b'\0', path_offset)].decode()
        if path == LLVM:
            found.append(address)
    assert len(found) == 1
    address = found[0]
    start = vm(address)
    assert data[start:start+4] == b'\xcf\xfa\xed\xfe'
    commands = struct.unpack_from('<I', data, start + 16)[0]
    pos = start + 32
    linkedit = None
    trie = None
    for _ in range(commands):
        cmd, size = struct.unpack_from('<II', data, pos)
        if cmd == 0x19 and data[pos+8:pos+24].split(b'\0')[0] == b'__LINKEDIT':
            linkedit = struct.unpack_from('<QQQQ', data, pos + 24)
        if cmd == 0x80000033:
            trie = struct.unpack_from('<II', data, pos + 8)
        pos += size
    assert trie and linkedit
    trie_start = vm(linkedit[0] + trie[0] - linkedit[2])
    blob = data[trie_start:trie_start + trie[1]]

def uleb(offset):
    value = shift = 0
    for i in range(10):
        b = blob[offset]
        offset += 1
        value |= (b & 127) << shift
        if not b & 128:
            return value, offset
        shift += 7
    raise ValueError('Oversized ULEB128')

exports = []
def walk(offset, prefix, ancestors):
    assert offset not in ancestors
    terminal_size, pos = uleb(offset)
    child_pos = pos + terminal_size
    if terminal_size:
        flags, value_pos = uleb(pos)
        value, _ = uleb(value_pos)
        exports.append(dict(symbol=prefix, flags=flags, value=value))
    count = blob[child_pos]
    pos = child_pos + 1
    for _ in range(count):
        end = blob.index(0, pos)
        edge = blob[pos:end].decode()
        child, pos = uleb(end + 1)
        walk(child, prefix + edge, ancestors | {offset})

walk(0, '', set())
symbols = sorted(x['symbol'] for x in exports)
assert len(symbols) == len(set(symbols)) and len(symbols) > 1000
assert '_LLVMContextCreate' in symbols and '_LLVMParseBitcodeInContext2' in symbols
stub = ('--- !tapi-tbd\ntbd-version: 4\ntargets: [ x86_64-macos ]\n'
        f"install-name: '{LLVM}'\ncurrent-version: 1\nexports:\n"
        '  - targets: [ x86_64-macos ]\n    symbols: [\n' +
        ',\n'.join('      '+json.dumps(s) for s in symbols) + '\n    ]\n...\n')
(OUT / 'libLLVM32023.tbd').write_text(stub)
(OUT / 'libLLVM32023.exports.json').write_text(json.dumps(exports, indent=2)+'\n')
report = dict(source=str(SOURCE), install_name=LLVM, symbols=len(symbols),
              image_address=hex(address), trie_bytes=len(blob),
              trie_sha256=hashlib.sha256(blob).hexdigest(),
              stub_sha256=hashlib.sha256(stub.encode()).hexdigest(),
              note='From full Tahoe source installer, not the SDK or another OS release')
(OUT / 'libLLVM32023.provenance.json').write_text(json.dumps(report, indent=2)+'\n')
print(json.dumps(report, indent=2))
