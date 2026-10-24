"""Decode the source RIDIFF10 PBZX stream, not a physical disk or live EFI.

The stream contains concatenated file data; this does not reconstruct APFS.
Header layout follows blacktop/ipsw pkg/ota/ridiff/raw.go.
"""
from pathlib import Path
import hashlib
import json
import lzma
import struct

ROOT = Path(__file__).resolve().parent
SOURCE = Path('/tmp/opencode/tahoe-gpu-payload/cryptex-system-x86_64')
OUT = ROOT / 'cross-build/cryptex-patchdata.decoded'

with SOURCE.open('rb') as src, OUT.open('xb') as dst:
    header = struct.unpack('<QHQQIQQQQ', src.read(62))
    magic, variants, flags, controls, excess, metadata, control, patch, total = header
    assert magic == 0x3031464649444952 and variants == 1
    assert total == SOURCE.stat().st_size
    src.seek(patch)
    assert src.read(4) == b'pbzx'
    block_size = struct.unpack('>Q', src.read(8))[0]
    digest = hashlib.sha256()
    chunks = []
    decoded = 0
    while src.tell() < total:
        offset = src.tell()
        raw_size, stored = struct.unpack('>QQ', src.read(16))
        data = src.read(stored)
        assert len(data) == stored
        if data.startswith(b'\xfd7zXZ'):
            data = lzma.decompress(data)
        assert len(data) == raw_size
        chunks.append(dict(source=offset, decoded=decoded, size=raw_size, stored=stored))
        dst.write(data)
        digest.update(data)
        decoded += raw_size
    report = dict(source=str(SOURCE), output=str(OUT), bytes=decoded,
                  sha256=digest.hexdigest(), block_size=block_size, chunks=chunks,
                  note='Concatenated file contents only; no APFS reconstruction or deployment')
    (ROOT / 'cross-build/cryptex-patchdata-index.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps({k:v for k,v in report.items() if k != 'chunks'}, indent=2))
