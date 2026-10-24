"""Read-only Mach-O signature inspection, including every fat-binary slice.

This checks CodeDirectory hashes, not AMFI policy or bundle resource sealing.
"""
from pathlib import Path
import hashlib
import json
import struct
import sys


def audit(data):
    magic = data[:4]
    if magic in (b'\xca\xfe\xba\xbe', b'\xca\xfe\xba\xbf', b'\xbe\xba\xfe\xca', b'\xbf\xba\xfe\xca'):
        endian = '>' if magic[:1] == b'\xca' else '<'
        wide = magic in (b'\xca\xfe\xba\xbf', b'\xbf\xba\xfe\xca')
        n = struct.unpack_from(endian + 'I', data, 4)[0]
        assert n <= 100
        slices = []
        for i in range(n):
            p = 8 + i * (32 if wide else 20)
            cpu, subtype = struct.unpack_from(endian + 'II', data, p)
            off, size = struct.unpack_from(endian + ('QQ' if wide else 'II'), data, p + 8)
            assert off + size <= len(data)
            slices.extend(audit(data[off:off + size]) or [])
        return slices
    if magic not in (b'\xcf\xfa\xed\xfe', b'\xce\xfa\xed\xfe'):
        return None
    cpu, subtype, filetype, count, commands_size = struct.unpack_from('<5I', data, 4)
    header = 32 if magic == b'\xcf\xfa\xed\xfe' else 28
    item = dict(cpu=hex(cpu), subtype=hex(subtype), filetype=filetype,
                bytes=len(data), directories=[])
    pos = header
    signature = None
    for _ in range(count):
        cmd, size = struct.unpack_from('<II', data, pos)
        assert size >= 8 and pos + size <= header + commands_size <= len(data)
        if cmd == 0x1d:
            assert signature is None
            signature = struct.unpack_from('<II', data, pos + 8)
        pos += size
    if signature is None:
        item['unsigned'] = True
        return [item]
    start, allocated = signature
    assert start + allocated <= len(data)
    magic, total, entries = struct.unpack_from('>III', data, start)
    assert magic == 0xfade0cc0 and 12 + entries * 8 <= total <= allocated
    item['signature_offset'] = start
    for i in range(entries):
        kind, offset = struct.unpack_from('>II', data, start + 12 + i * 8)
        substart = start + offset
        magic, size = struct.unpack_from('>II', data, substart)
        assert offset + size <= total
        if magic != 0xfade0c02:
            continue
        cd = data[substart:substart + size]
        _, _, version, flags, hash_offset, ident_offset, special, slots, limit = struct.unpack_from('>9I', cd)
        hash_size, hash_type, platform, shift = struct.unpack_from('>4B', cd, 36)
        if version >= 0x20300 and struct.unpack_from('>Q', cd, 56)[0]:
            limit = struct.unpack_from('>Q', cd, 56)[0]
        assert 0 < ident_offset < len(cd) and 0 <= shift <= 30
        identifier = cd[ident_offset:].split(b'\0')[0].decode(errors='replace')
        scatter = struct.unpack_from('>I', cd, 44)[0] if version >= 0x20100 else 0
        page = (1 << shift) if shift else limit
        assert hash_offset >= special * hash_size and hash_offset + slots * hash_size <= len(cd)
        directory = dict(slot=hex(kind), identifier=identifier, version=hex(version),
                         flags=hex(flags), page_shift=shift, page_bytes=page,
                         hash_type=hash_type, code_limit=limit, code_slots=slots,
                         scatter_offset=scatter, x86_page_size_supported=(shift == 12))
        if not scatter:
            assert limit <= start and slots == (limit + page - 1) // page
            algorithm = {1: 'sha1', 2: 'sha256', 3: 'sha256', 4: 'sha384'}[hash_type]
            bad = []
            for slot in range(slots):
                digest = hashlib.new(algorithm, data[slot * page:min((slot + 1) * page, limit)]).digest()[:hash_size]
                if digest != cd[hash_offset + slot * hash_size:hash_offset + (slot + 1) * hash_size]:
                    bad.append(slot)
            directory['bad_code_slots'] = bad
            directory['code_hashes_valid'] = not bad
        item['directories'].append(directory)
    assert item['directories']
    return [item]


def audit_file(path):
    with path.open('rb') as stream:
        magic = stream.read(4)
        if magic not in (b'\xcf\xfa\xed\xfe', b'\xce\xfa\xed\xfe', b'\xca\xfe\xba\xbe', b'\xca\xfe\xba\xbf', b'\xbe\xba\xfe\xca', b'\xbf\xba\xfe\xca'):
            return None
        stream.seek(0)
        data = stream.read()
    return dict(path=str(path), sha256=hashlib.sha256(data).hexdigest(), slices=audit(data))


if __name__ == '__main__':
    results = []
    for arg in sys.argv[1:]:
        path = Path(arg)
        for p in sorted(path.rglob('*')) if path.is_dir() else [path]:
            if p.is_file() and not p.is_symlink():
                try:
                    result = audit_file(p)
                    if result:
                        results.append(result)
                except Exception as error:
                    results.append(dict(path=str(p), error=repr(error)))
    print(json.dumps(results, indent=2))
