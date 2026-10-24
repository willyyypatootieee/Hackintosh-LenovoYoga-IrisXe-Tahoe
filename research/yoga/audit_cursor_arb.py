"""Offline audit/generation only: captured regular files, no device access."""
from pathlib import Path
import hashlib
import json
import struct
import subprocess
from functools import cache
from audit_cursor_diag_inputs import FB, image

ROOT = Path(__file__).resolve().parent
OUT = ROOT / 'yoga-cursor-arb'
START, END = 0x7eb2c, 0x7ec18
SYMBOL = '__ZN24AppleIntelBaseController18updateCursorParamsEP21AppleIntelFramebufferP21AppleIntelDisplayPath'
SITES = ((0x7eb73, 0x23), (0x7eb8d, 0x27), (0x7eb9c, 0x22))


@cache
def captured_function():
    _, at = image(FB)
    return at(START, END - START)


def replacement(old):
    """Fail closed unless every byte of this function matches the capture."""
    if old != captured_function():
        return None
    new = bytearray(old)
    for address, mode in SITES:
        offset = address - START
        assert old[offset:offset + 6] == b'\x41\xbf' + struct.pack('<I', mode)
        new[offset + 2:offset + 6] = struct.pack('<I', mode | 0x10000000)
    return bytes(new)


def main():
    data, at = image(FB)
    digest = hashlib.sha256(data).hexdigest()
    assert digest == '3f2bb3336507242ddc3f69c5d7645d19ce1cd4d8748bdb4b4c8858c4b49103df'
    symbols = subprocess.check_output(['llvm-nm', '--defined-only', '--format=posix', str(FB)], text=True)
    addresses = {p[0]: int(p[2], 16) for line in symbols.splitlines() if len(p := line.split()) == 4}
    assert addresses[SYMBOL] == START
    assert addresses['__ZN24AppleIntelBaseController20hwUpdateCursorMemoryEP21AppleIntelFramebuffer'] == END
    old = at(START, END - START)
    new = replacement(old)
    changed = [i for i, (a, b) in enumerate(zip(old, new)) if a != b]
    assert changed == [address - START + 5 for address, _ in SITES]
    assert len(old) == len(new) == 236
    # Each immediate retains mode/pixel format, changing only arbitration bit 28.
    for address, mode in SITES:
        offset = address - START
        value = struct.unpack_from('<I', new, offset + 2)[0]
        assert value & 0x3f == mode
        assert (value & 0x70000000) >> 28 == 1
        assert value ^ mode == 0x10000000
    for address, expected in ((0x7eb7e, '4531ff'), (0x7ebba, '4531ff'),
                              (0x7ebe6, '4531ff'), (0x7ec06, '4589be70010000')):
        offset = address - START
        expected = bytes.fromhex(expected)
        assert new[offset:offset + len(expected)] == expected
    # Every single-byte mutation, at every function offset, must be rejected.
    for offset in range(len(old)):
        for bit in range(8):
            bad = bytearray(old)
            bad[offset] ^= 1 << bit
            assert replacement(bytes(bad)) is None
    assert replacement(new) is None  # handled separately as already present
    assert replacement(old[:-1]) is None
    assert replacement(old + b'\x00') is None
    source = (OUT / 'YogaCursorArb.cpp').read_text()
    for forbidden in ('routeMultiple', 'writeReg32', 'writePCIConfig', 'ml_nofault_copy', 'thread_call'):
        assert forbidden not in source
    assert 'checkKernelArgument("-yogacursorarb")' in source
    assert source.count('patcher.applyLookupPatch(') == 1
    header = ['// Generated from the hash-checked captured framebuffer by audit_cursor_arb.py.',
              f'static const char cursorArbSymbol[] = "{SYMBOL}";']
    for name, value in (('Old', old), ('New', new)):
        header.append(f'static const uint8_t cursorArb{name}[] = {{' + ','.join(f'0x{b:02x}' for b in value) + '};')
    (OUT / 'CursorArbGuard.hpp').write_text('\n'.join(header) + '\n')
    report = dict(framebuffer_sha256=digest, function_start=hex(START), length=len(old),
                  modified_virtual_addresses=[hex(START + i) for i in changed],
                  mutation_checks=len(old) * 8, disabled_mode_unchanged=True,
                  pixel_format_unchanged=True, external_writes=False,
                  runtime_validated=False, runtime_guard='ADL-P CPUID + IGPU/GGC + Tahoe + symbol/range + all 236 function bytes',
                  limitation='Not a runtime whole-file hash or proof of visual benefit')
    (OUT / 'input-audit.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
