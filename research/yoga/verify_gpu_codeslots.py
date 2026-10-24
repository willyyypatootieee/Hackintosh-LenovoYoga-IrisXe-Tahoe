"""Independently verify embedded ad-hoc CodeDirectory page/special hashes.

No CMS is expected for ad-hoc signatures. This is structural/hash validation,
not proof that macOS AMFI or kmutil will accept the bundles.
"""
from pathlib import Path
import hashlib
import json
import plistlib
import struct

ROOT = Path(__file__).resolve().parent
STACK = ROOT / 'cross-build/gpu-stack-assembled'

def check_binary(path):
    data = path.read_bytes()
    if data[:4] != b'\xcf\xfa\xed\xfe':
        return None
    count = struct.unpack_from('<I', data, 16)[0]
    pos = 32
    signature = None
    for _ in range(count):
        cmd, size = struct.unpack_from('<II', data, pos)
        if cmd == 0x1d:
            signature = struct.unpack_from('<II', data, pos + 8)
        pos += size
    assert signature is not None, str(path)
    start, length = signature
    magic, total, entries = struct.unpack_from('>III', data, start)
    assert magic == 0xfade0cc0 and total <= length
    directories = []
    for i in range(entries):
        kind, offset = struct.unpack_from('>II', data, start + 12 + 8 * i)
        substart = start + offset
        magic, size = struct.unpack_from('>II', data, substart)
        if magic != 0xfade0c02:
            continue
        cd = data[substart:substart+size]
        _, _, version, flags, hash_offset, ident_offset, special, slots, limit = struct.unpack_from('>9I', cd)
        hash_size, hash_type, platform, page_log = struct.unpack_from('>4B', cd, 36)
        # Valid page hashes alone do not establish x86_64 kernel compatibility.
        cpu = struct.unpack_from('<I', data, 4)[0]
        assert cpu != 0x1000007 or page_log == 12, (str(path), 'Unsupported x86_64 signature page shift', page_log)
        assert flags & 2, 'Not ad-hoc'
        algorithm = {1:'sha1', 2:'sha256', 3:'sha256', 4:'sha384'}[hash_type]
        page = 1 << page_log
        if version >= 0x20300:
            limit64 = struct.unpack_from('>Q', cd, 56)[0]
            if limit64:
                limit = limit64
        assert limit <= start and slots == (limit + page - 1) // page
        for slot in range(slots):
            value = hashlib.new(algorithm, data[slot*page:min((slot+1)*page,limit)]).digest()[:hash_size]
            assert value == cd[hash_offset+slot*hash_size:hash_offset+(slot+1)*hash_size], (str(path), slot)
        checked_special = []
        contents = path.parent.parent
        if contents.name == 'Contents':
            for slot, candidate in [(1,contents/'Info.plist'),(3,contents/'_CodeSignature/CodeResources')]:
                if special < slot:
                    continue
                expected = cd[hash_offset-slot*hash_size:hash_offset-(slot-1)*hash_size]
                if expected != bytes(hash_size):
                    assert candidate.is_file(), str(candidate)
                    assert hashlib.new(algorithm,candidate.read_bytes()).digest()[:hash_size] == expected, str(candidate)
                    checked_special.append(slot)
        directories.append(dict(algorithm=algorithm, code_slots=slots, code_limit=limit,
                                special_slots_checked=checked_special, flags=flags,
                                page_shift=page_log, page_bytes=page,
                                x86_page_size_supported=(page_log == 12)))
    assert directories
    return dict(path=str(path.relative_to(STACK)), directories=directories, passed=True)

results = []
for path in sorted(STACK.rglob('*')):
    if path.is_file():
        result = check_binary(path)
        if result:
            results.append(result)
(STACK/'independent-codeslot-verification.json').write_text(json.dumps(results,indent=2)+'\n')
print(json.dumps(dict(binaries=len(results), passed=all(x['passed'] for x in results),
                      note='Page hashes and present Info.plist/CodeResources hashes only; not Apple runtime acceptance'),indent=2))
