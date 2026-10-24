"""Read-only extraction of the backup's probePortMode for GNU objdump.

Outputs only to the audit directory; never loads the kext or touches disks.
"""
from pathlib import Path
import hashlib
import struct
import subprocess

ROOT = Path(__file__).resolve().parent
SOURCE = Path('/home/willy/Videos/EFI-backup-20261011-003339/OC/_ngreen/kexts/HookCase.kext/Contents/MacOS/HookCase')
data = SOURCE.read_bytes()
assert hashlib.sha256(data).hexdigest() == 'edb3729ed0e7f76b692e39df8093d78ddd8ac89294a0b2574f77393037436c84'
assert data[:4] == bytes.fromhex('cffaedfe')
offset = 32
start, end = 0x1410e, 0x14260
for _ in range(struct.unpack_from('<I', data, 16)[0]):
    command, size = struct.unpack_from('<II', data, offset)
    assert size >= 8
    if command == 0x19:  # LC_SEGMENT_64
        for i in range(struct.unpack_from('<I', data, offset + 64)[0]):
            section = offset + 72 + 80 * i
            name = data[section:section + 16].rstrip(b'\0')
            address, length, file_offset = struct.unpack_from('<QQI', data, section + 32)
            if name == b'__text' and address <= start < end <= address + length:
                raw = ROOT / 'hookcase-probePortMode.bin'
                raw.write_bytes(data[file_offset + start - address:file_offset + end - address])
                run = subprocess.run(
                    ['objdump', '-D', '-b', 'binary', '-m', 'i386:x86-64',
                     '--adjust-vma=' + hex(start), str(raw)],
                    capture_output=True, text=True, check=True)
                (ROOT / 'hookcase-probePortMode.disasm.txt').write_text(run.stdout)
                print('Saved targeted disassembly; source hash verified.')
                raise SystemExit(0)
    offset += size
raise RuntimeError('Expected code section not found')
