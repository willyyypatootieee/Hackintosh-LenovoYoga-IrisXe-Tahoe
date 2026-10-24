"""Exact native mapping construction/teardown audit. Offline; no MMIO or devices."""
import hashlib
import json
import struct
import subprocess
from pathlib import Path

from audit_cursor_diag_inputs import FB, image
from audit_typec_native_integration import require
from yoga_fb_match_patch import symbol

ROOT = Path(__file__).resolve().parent
OUT = ROOT / 'cross-build/typec-mmio-mapping-audit-20261024'


def audit():
    data, at = image(FB)
    require(hashlib.sha256(data).hexdigest() ==
            '3f2bb3336507242ddc3f69c5d7645d19ce1cd4d8748bdb4b4c8858c4b49103df', 'Wrong framebuffer')
    symbols = {
        '__ZN24AppleIntelBaseController5startEP9IOService': 0x5a8a0,
        '__ZN24AppleIntelBaseController4stopEP9IOService': 0x60f42,
        '__ZN31AppleIntelRegisterAccessManager4initEPVj': 0x8d1c0,
        '__ZTV31AppleIntelRegisterAccessManager': 0x13cb40,
    }
    for name, address in symbols.items():
        require(symbol(data, name) == address, name)
    checks = {
        # provider+0xc60 -> map with register argument0x10 and options0
        0x5af7a: '49 8b bd 60 0c 00 00 48 8b 07 be 10 00 00 00 31 d2 ff 90 08 09 00 00',
        0x5af91: '48 85 c0 0f 84 80 09 00 00',
        0x5af9a: '49 89 c4',
        # store mapping; map vptr+0x118 -> native raw address+0x9b0
        0x5b010: '4d 89 a5 70 0c 00 00 49 8b 04 24 4c 89 e7 ff 90 18 01 00 00 49 89 85 b0 09 00 00',
        # allocate native manager from exact metaclass, not arbitrary OSObject
        0x5b02b: '48 8d 05 fe 1a 0e 00 48 8b 38 48 8b 07 ff 90 88 00 00 00',
        # init manager via its+0x118 vptr slot with same raw address
        0x5b03e: '49 89 85 40 0c 00 00 49 8b b5 b0 09 00 00 48 8b 08 48 89 c7 ff 91 18 01 00 00',
        # map+0x128 -> native32-bit length fields
        0x5b058: '49 8b bd 70 0c 00 00 48 8b 07 ff 90 28 01 00 00 41 89 85 78 0c 00 00 49 8b 8d 40 0c 00 00 89 41 60',
        0x8d1d0: '48 89 77 50 e8 00 00 00 00 48 89 43 58',
        # stop releases map through OSObject slot+0x28, then clears map only
        0x61a51: '49 8b bf 70 0c 00 00 48 85 ff 74 18',
        0x61a64: '48 8b 07 ff 50 28 49 c7 87 70 0c 00 00 00 00 00 00',
    }
    for address, expected in checks.items():
        raw = bytes.fromhex(expected)
        require(at(address, len(raw)) == raw, f'Byte mismatch {address:#x}')
    init_slot = struct.unpack('<Q', at(0x13cb40 + 16 + 0x118, 8))[0]
    require(init_slot == 0x8d1c0, 'Manager init slot')
    return dict(symbols={n: hex(a) for n, a in symbols.items()}, byte_checks=len(checks),
                manager_init_vptr_slot='0x118',
                controller_fields=dict(provider='0xc60', mapping='0xc70',
                    virtual_address='0x9b0', mapping_length_u32='0xc78', manager='0xc40'),
                manager_fields=dict(virtual_address='0x50', mapping_length_u32='0x60',
                    conflict_only_lock='0x58'),
                pci_mapping_register_argument='0x10', pci_mapping_options=0,
                map_virtual_slots=dict(address='0x118', length='0x128'),
                stop_map_release='0x61a67', stop_map_clear='0x61a6a',
                findings=[
                    'Controller startup stores provider-returned map at+0xc70, address at+0x9b0 and allocates native manager at+0xc40.',
                    'Manager object vptr slot+0x118 is exactly init(void volatile*); it stores the supplied raw address at manager+0x50.',
                    'The same map length return is truncated to32bits and stored in controller+0xc78 and manager+0x60. Future boundary refuses truncation and requires retained typed-map/descriptor/BAR agreement.',
                    'Stop releases then clears controller+0xc70. This bounded stop sequence does not clear raw controller+0x9b0 or manager aliases. Raw pointer/size alone do not prove live mapping.',
                    'Mapping API virtual-slot semantics still need exact runtime/IOKit provenance. These byte sites are not permission to call unknown virtuals or dereference raw offsets.',
                ], unresolved=[
                    'Native retained provider/descriptor/map provenance and lifecycle exclusion against stop/remap.',
                    'Exact PCI BAR physical base/length, cache/mapping attributes, mapping task and typed IOKit API validation.',
                    'Enforceable lock order across input/lifetime, mapping and all shared DKL/FIA/clock native users.',
                ], runtime_validated=False, device_access=False, external_writes=False)


def main():
    report = audit()
    OUT.mkdir(exist_ok=True)
    for label, start, stop in [('start-mapping-provenance', 0x5af7a, 0x5b079),
                               ('manager-init', 0x8d1c0, 0x8d1e4),
                               ('stop-mapping-release', 0x61a51, 0x61a75)]:
        text = subprocess.check_output(['llvm-objdump', '-d', '--x86-asm-syntax=intel',
            f'--start-address={start:#x}', f'--stop-address={stop:#x}', str(FB)], text=True)
        (OUT / f'{label}.disasm.txt').write_text(text)
    (OUT / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
