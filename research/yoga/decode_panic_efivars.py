"""Decode copied x86 XNU packA panic variables; never accesses live NVRAM."""
from pathlib import Path
import hashlib
import json
import re
import sys


def unpack(data):
    if len(data) % 7:
        raise ValueError('packA payload must contain complete seven-byte groups')
    return bytes((int.from_bytes(data[i:i + 7], 'little') >> (7 * j)) & 127
                 for i in range(0, len(data), 7) for j in range(8))


def decode(folder):
    parts = sorted(folder.glob('AAPL,PanicInfo????-*'))
    if not parts:
        raise ValueError('No copied panic variables')
    payloads = []
    for i, part in enumerate(parts):
        assert part.name.startswith(f'AAPL,PanicInfo{i:04d}-'), 'Missing part'
        data = part.read_bytes()
        assert data[:4] == bytes.fromhex('07000000'), 'Unexpected EFI attributes'
        payloads.append(data[4:])
    packed = b''.join(payloads)
    text = unpack(packed).rstrip(b'\0').decode('ascii')
    assert text.startswith('panic(')
    uptime = int(re.search(r'System uptime in nanoseconds: (\d+)', text)[1])
    return text, dict(parts=len(parts), packed_bytes=len(packed), uptime_ns=uptime,
                      packed_sha256=hashlib.sha256(packed).hexdigest(),
                      source='Copied EFI variables, x86 XNU packA, 4-byte attributes excluded',
                      external_writes=False, nvram_writes=False)


if __name__ == '__main__':
    folder = Path(sys.argv[1])
    text, report = decode(folder)
    (folder.parent.parent / 'nvram-latest-decoded.panic').write_text(text + '\n')
    (folder.parent.parent / 'nvram-panic-decode.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))
