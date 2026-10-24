"""Exact captured-binary clock input/MMIO contract audit. No device accesses."""
import hashlib
import json
import re
import struct
import subprocess
from pathlib import Path

from audit_cursor_diag_inputs import FB, image
from audit_typec_native_integration import require
from yoga_fb_match_patch import symbol

ROOT = Path(__file__).resolve().parent
REF = ROOT / 'cross-build/linux-v6.12-display-gt-reference-20261024'
OUT = ROOT / 'cross-build/typec-clock-input-audit-20261024'
REFERENCES = {
    'display__intel_cdclk.c': '5eb7622044744fca815e2f40102ceac4831e1109cbc9301934e4a3dd12577736',
    'display__intel_bios.c': 'cd000ad0f1680141add3992032c9509a4bc0651f6d950e67e92b20b194d2067c',
    'display__intel_vbt_defs.h': 'd54b1337254b0dca02a1e8701cdd9aaf5128da9994b2aa8f64e343629e01f11d',
}


def audit():
    data, at = image(FB)
    require(hashlib.sha256(data).hexdigest() ==
            '3f2bb3336507242ddc3f69c5d7645d19ce1cd4d8748bdb4b4c8858c4b49103df', 'Wrong framebuffer')
    symbols = {
        '__ZN24AppleIntelBaseController11initCDClockEv': 0xa5192,
        '__ZN31AppleIntelRegisterAccessManager14ReadRegister32Em': 0x8d3fc,
        '__ZN31AppleIntelRegisterAccessManager15WriteRegister32Emj': 0x8dce6,
        '__ZN31AppleIntelRegisterAccessManager18isConflictRegisterEj': 0x8d1e4,
        '__ZN31AppleIntelRegisterAccessManager20conflictRegisterListE': 0xf3200,
    }
    for name, address in symbols.items():
        require(symbol(data, name) == address, name)
    checks = {
        0xa51a7: '48 8b bf 40 0c 00 00 be 04 10 05 00 e8 44 82 fe ff',
        0xa51d4: 'c1 eb 1d',
        0xa5207: 'c1 eb 1d 83 fb 03 73 61',
        0xa521f: '41 89 9e b0 0e 00 00',
        0x8d411: '8b 47 60 83 c0 fc 48 39 f0 76 11',
        0x8d41c: '48 8b 47 50',
        0x8d427: '44 8b 34 18',
        0x8d443: '45 31 f6', # invalid bounds => zero, not an error sentinel
        0x8d483: '44 89 f0',
        0x8dd04: '8b 47 60 83 c0 fc 48 39 f0',
        0x8dd18: '44 89 fe e8 c4 f4 ff ff 85 c0 0f 88 c4 00 00 00',
        0x8dd47: '49 8b 7e 58 e8 00 00 00 00',
        0x8dd8f: '49 8b 7e 58 e8 00 00 00 00',
        0x8ddf6: '49 8b 46 50',
        0x8de01: '42 89 1c 38', # non-conflict path writes with no manager lock
    }
    for address, expected in checks.items():
        raw = bytes.fromhex(expected)
        require(at(address, len(raw)) == raw, f'Byte mismatch {address:#x}')
    relocations = {}
    text = subprocess.check_output(['llvm-objdump', '--macho', '--reloc', str(FB)], text=True)
    for line in text.splitlines():
        match = re.match(r'^([0-9a-f]{8})\s+.*\s(\S+)$', line)
        if match:
            relocations[int(match[1], 16)] = match[2]
    for addr, name in {0x8dd4c: '_IORecursiveLockLock', 0x8dd94: '_IORecursiveLockUnlock'}.items():
        require(relocations.get(addr) == name, f'Relocation {addr:#x}')
    conflicts = list(struct.unpack('<16I', at(0xf3200, 64)))
    require(conflicts == [0x70080, 0, 0x71080, 1, 0x72080, 2, 0x73080, 3,
                          0x70084, 0, 0x71084, 1, 0x72084, 2, 0x73084, 3], 'Conflict list')
    for name, sha in REFERENCES.items():
        require(hashlib.sha256((REF / name).read_bytes()).hexdigest() == sha, name)
    vbt = ROOT / 'cursor-typec-firmware-readonly-20261024-021325/intel-display/i915_vbt'
    raw = vbt.read_bytes()
    require(hashlib.sha256(raw).hexdigest() ==
            'cb9e14dbab2646ac0e2427206ec354bab966d8f321bc6dd0cb4109539907c687', 'Captured Yoga VBT')
    bdb = struct.unpack_from('<I', raw, 28)[0]
    version = struct.unpack_from('<H', raw, bdb + 16)[0]
    require(version == 247, 'Yoga BDB version')
    return dict(symbols={n: hex(a) for n, a in symbols.items()}, byte_checks=len(checks),
                linux_reference_sha256=REFERENCES, native_reference_encoding_offset='0xeb0',
                actual_reference_register='0x51004', reference_encodings_khz=[24000, 19200, 38400],
                captured_vbt_bdb_version=version, captured_vbt_afc_override=False,
                register_manager=dict(mapping_offset='0x50', size_offset='0x60',
                    bounds_failure_read_value=0, conflict_registers=[hex(x) for x in conflicts[::2]],
                    lock_covers_nonconflict_registers=False),
                findings=[
                    'initCDClock reads actual DSSM bits31:29 and stores validated encoding0..2 at controller+0xeb0.',
                    'ReadRegister32 returns zero on a bounds failure: checking only all-ones is insufficient for DSSM because zero is valid24MHz.',
                    'WriteRegister32 only takes manager+0x58 recursive lock for the eight conflict-list registers; these exclude HIP, FIA, DDI selector and DPCLKA. It cannot supply shared-PHY/clock RMW serialization.',
                    'Captured Yoga BDB247 predates249+ AFC field. Follow Linux no-override policy; do not read a nonexistent byte or invent AFC7.',
                ], unresolved=[
                    'Retained native object/MMIO mapping provenance and length validation; no raw casts are authorized.',
                    'All native DKL/FIA/shared clock writers must participate in whole-RMW exclusion, not only individual MMIO writes.',
                    'Actual macOS DSSM value and retained IGPU-bound VBT retrieval still need runtime backend wiring.',
                ], runtime_validated=False, device_access=False, external_writes=False)


def main():
    report = audit()
    OUT.mkdir(exist_ok=True)
    for label, start, stop in [('native-reference-input', 0xa5192, 0xa5276),
                               ('native-register-read', 0x8d3fc, 0x8d490),
                               ('native-register-write', 0x8dce6, 0x8de14),
                               ('native-conflict-check', 0x8d1e4, 0x8d350)]:
        text = subprocess.check_output(['llvm-objdump', '-d', '--x86-asm-syntax=intel',
            f'--start-address={start:#x}', f'--stop-address={stop:#x}', str(FB)], text=True)
        (OUT / f'{label}.disasm.txt').write_text(text)
    (OUT / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
