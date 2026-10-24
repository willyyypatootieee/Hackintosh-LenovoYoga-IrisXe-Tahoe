"""Reproducible captured-binary TC3 integration audit; no device or MMIO access."""
import hashlib
import json
import re
import struct
import subprocess
from pathlib import Path

from audit_cursor_diag_inputs import FB, image
from yoga_fb_match_patch import symbol

ROOT = Path(__file__).resolve().parent
OUT = ROOT / 'cross-build/typec-native-integration-audit-20261024'


def require(ok, message):
    if not ok:
        raise RuntimeError(message)


def audit():
    data, at = image(FB)
    digest = hashlib.sha256(data).hexdigest()
    require(digest == '3f2bb3336507242ddc3f69c5d7645d19ce1cd4d8748bdb4b4c8858c4b49103df', 'Wrong framebuffer')
    symbols = {
        '__ZN17AppleIntelPortHAL4initEP10PortConfig': 0x8e298,
        '__ZN17AppleIntelPortHAL11setPortModeEN14AppleIntelPort8PortModeE': 0x8f344,
        '__ZN17AppleIntelPortHAL26getDklPhyLaneConfigBitMaskEv': 0x8fae0,
        '__ZN17AppleIntelPortHAL29getConfiguredDklPhyLaneConfigEv': 0x8fc74,
        '__ZN17AppleIntelPortHAL15setFIALaneCountEj': 0x8fdc4,
        '__ZN17AppleIntelPortHAL15getFIALaneCountEv': 0x9329a,
        '__ZN14AppleIntelPort11setPortModeENS_8PortModeE': 0x262ea,
        '__ZN14AppleIntelPort18resetSoftwareStateEv': 0x2590c,
        '__ZN19AppleIntelPowerWell18enablePowerWellAuxEj': 0x8b0d6,
        '__ZN19AppleIntelPowerWell19disablePowerWellAuxEj': 0x88cf6,
        '__ZN19AppleIntelPowerWell22hwSetPowerWellStateAuxEbj': 0x8b7dc,
        '__ZN17AppleIntelPortHAL13runAUXCommandEN14AppleIntelPort11AUX_CommandEPjjb': 0x91b64,
        '__ZN24AppleIntelBaseController16hwSetCursorStateEP21AppleIntelFramebufferiibb': 0x7dff8,
    }
    for name, address in symbols.items():
        require(symbol(data, name) == address, name)
    checks = {
        0x8e327: '8b 00 48 83 f8 08',
        0x8e333: '48 8d 0d ea 09 00 00', # init table indexed directly by DDI
        0x8e9e4: '48 b8 00 45 06 00 10 45 06 00',
        0x8e9ee: '49 89 87 90 05 00 00', # DDI5 BUF_CTL = 0x64500
        0x8ea22: '41 c7 87 4c 06 00 00 14 61 04 00',
        0x8e0d9: '48 8d 05 d8 ec 0a 00', # object vptr = ZTV + 16
        0x8e0e0: '48 89 03',
        0x263b2: 'f6 40 14 08',
        0x263c5: 'ff 90 20 01 00 00', # object-relative slot, NOT raw ZTV offset
        0x263cb: '41 89 9e 50 05 00 00',
        0x2642a: '31 c0', # integer status zero, not bool true
        0x8f613: '4c 8b a0 48 01 00 00', # direct base-vtable mode setter
        0x8f620: '41 ff d4 41 89 c6', # publication precedes HAL acquisition
        0x8f6b7: 'e8 70 a0 f9 ff',
        0x8f6e5: 'be 90 38 16 00',
        0x8f779: 'c0 e8 02', # DDI5 readiness = old global bit 2
        0x8f449: 'be 94 38 16 00',
        0x8f4c9: '41 83 e6 fb', # DDI5 clears old global ownership bit 2
        0x8fb76: 'be a0 38 16 00',
        0x8fbd4: '83 3a 05',
        0x8fbe0: 'c1 e8 10', # DDI5 raw assignment bits 19:16
        0x8fc4a: '83 e0 0f',
        0x8fcc5: 'be c0 38 16 00',
        0x8fd17: '41 c1 ee 08', # DDI5 configured bits 11:8
        0x8fe5e: 'e8 7d fc ff ff',
        0x8ffa2: 'be c0 38 16 00',
        0x9005c: '41 c1 e4 08',
        0x90060: '41 81 e6 ff f0 ff ff',
        0x900bf: 'be c0 38 16 00',
        0x8b17e: '41 ff 44 9e 54',
        0x8b1d5: '41 83 7c 9e 54 02',
        0x8b222: '41 80 7e 18 00',
        0x91d9c: '41 8b b6 94 05 00 00', # AUX_CTL is +0x594, not +0x584
        0x91da9: 'e8 f0 d6 f9 ff',
        0x928e0: '41 8b b5 4c 06 00 00', # DDI clock selector field
        0x91bdb: '48 8b b8 30 0c 00 00', # AUX controller recursive lock
        0x9210a: '48 8b b8 30 0c 00 00',
        0x7e572: '49 8b bd 30 0c 00 00', # cursor shares this lock (try-lock)
        0x7e565: '41 bc 03 00 00 00',
        0x7e57e: '85 c0 75 1b',
        0x7e582: 'bf 03 00 00 00',
        0x7e58c: '41 ff cc 75 da',
        0x7e5d8: '49 8b bc 24 30 0c 00 00',
        0x7eb13: '49 8b bc 24 30 0c 00 00',
        0x88da0: '85 c0', # disable guards zero before decrement
        0x88da8: 'ff c8 41 89 44 9e 54',
        0x88e01: '41 83 7c 9e 54 00', # other users retain hardware
        0x8b421: 'c1 e3 0b', # DDI5 DP AUX request bit 11
        0x8b678: '41 c1 e5 17', # DDI5 TBT request bit 23
        0x8b7bb: 'e8 1c 00 00 00',
        0x8b899: 'be 40 54 04 00',
        0x8b8b4: 'be 44 54 04 00',
        0x8b990: '41 bc 14 00 00 00', # 20 bounded polling iterations
        0x8b9b2: 'be 44 54 04 00',
        0x8b9c2: '80 b9 18 09 00 00 00', # emulated state condition
        0x8ba6d: '48 83 b9 f0 1b 00 00 00', # second emulated state condition
        0x8bbe7: 'c1 e8 0b', # TC3 request/state pair
        0x8bbec: 'c1 e9 0a',
        0x8bce2: 'bf 01 00 00 00',
        0x8bcec: '41 ff cc 0f 85 ab fc ff ff',
    }
    for address, expected in checks.items():
        pattern = bytes.fromhex(expected)
        require(at(address, len(pattern)) == pattern, f'Byte mismatch {address:#x}')
    relocation_text = subprocess.check_output(
        ['llvm-objdump', '--macho', '--reloc', str(FB)], text=True)
    relocations = {}
    for line in relocation_text.splitlines():
        match = re.match(r'^([0-9a-f]{8})\s+.*\s(\S+)$', line)
        if match:
            relocations[int(match[1], 16)] = match[2]
    expected_relocations = {
        0x91be3: '_IORecursiveLockLock', 0x92112: '_IORecursiveLockUnlock',
        0x7e57a: '_IORecursiveLockTryLock', 0x7e588: '_IOSleep',
        0x7e5e1: '_IORecursiveLockUnlock', 0x7eb1c: '_IORecursiveLockUnlock',
        0x8bce8: '_IODelay', 0x5aca3: '_IORecursiveLockAlloc',
        0x617ce: '_IORecursiveLockFree',
    }
    for address, name in expected_relocations.items():
        require(relocations.get(address) == name, f'Relocation {address:#x}: {name}')
    tables = {}
    expected_tables = {
        0x8ed24: [0x8e343, 0x8e55b, 0x8e710, 0x8e8cd, 0x8e944, 0x8e9bb, 0x8ea32, 0x8eaa9, 0x8eb1d],
        0x8faa0: [0x8f482, 0x8f4b2, 0x8f4c2, 0x8f4cf, 0x8f4dc, 0x8f4e9],
        0x8fab8: [0x8f710, 0x8f763, 0x8f772, 0x8f77e],
        0x8fac8: [0x8f900, 0x8f972, 0x8f97f, 0x8f98c, 0x8f999, 0x8f9a6],
        0x8fdac: [0x8fcfa, 0x8fd03, 0x8fd10, 0x8fd1d, 0x8fd2a, 0x8fd37],
        0x900d4: [0x8fe7b, 0x8fe7b, 0x8ff47, 0x8fe7b, 0x8fe9d, 0x8fe9d, 0x8fe9d, 0x8fe7b, 0x8fe9d, 0x8fe9d, 0x8fe9d, 0x8fef8, 0x8fe9d, 0x8fe9d, 0x8ff1f],
        0x90110: [0x8ffd7, 0x90039, 0x90051, 0x90069, 0x90084, 0x9009c],
    }
    for base, targets in expected_tables.items():
        decoded = [base + struct.unpack('<i', at(base + 4*i, 4))[0] for i in range(len(targets))]
        require(decoded == targets, f'Jump table {base:#x}')
        tables[hex(base)] = [hex(x) for x in decoded]
    slots = {}
    for name, base, expected in [
        ('__ZTV14AppleIntelPort', 0x139550, {0x130: 0x2590c, 0x138: 0x26086, 0x140: 0x2618a, 0x148: 0x262ea}),
        ('__ZTV17AppleIntelPortHAL', 0x13cda8, {0x130: 0x2590c, 0x138: 0x8edf8, 0x140: 0x8f1f4, 0x148: 0x8f344, 0x1b8: 0x9329a, 0x1c0: 0x8fdc4, 0x1c8: 0x8fc74}),
    ]:
        require(symbol(data, name) == base, name)
        slots[name] = []
        for raw, target in expected.items():
            require(struct.unpack('<Q', at(base + raw, 8))[0] == target, f'{name}+{raw:#x}')
            slots[name].append(dict(raw_offset=hex(raw), object_vptr_offset=hex(raw-16), target=hex(target)))
    linux = ROOT / 'cross-build/linux-v6.12-display-gt-reference-20261024'
    references = {}
    for filename in ['display__intel_tc.c', 'display__intel_mg_phy_regs.h', 'i915_reg.h']:
        references[filename] = hashlib.sha256((linux / filename).read_bytes()).hexdigest()
    return dict(framebuffer_sha256=digest, checked_symbols={n: hex(a) for n, a in symbols.items()},
                byte_checks=len(checks), jump_tables=tables, vtable_slots=slots,
                checked_relocations={hex(a): n for a, n in expected_relocations.items()},
                linux_reference_sha256=references,
                tc3=dict(linux_tc_index=2, apple_config_ddi=5, ddi_buf_ctl='0x64500',
                         native_field_0x584_value='0x644f4', native_ddi_clk_sel='0x46114',
                          native_aux_ctl_field_value='0x64510',
                          aux_driver_power=dict(register='0x45444', request_bit=11, state_bit=10),
                          ddi_driver_power=dict(register='0x45454', request_bit=11, state_bit=10),
                          native_aux_reference_offset='0x54 + 4*DDI',
                          controller_recursive_lock_offset='0xc30',
                         apple_assignment=dict(register='0x1638a0', shift=16),
                         adlp_assignment=dict(register='0x16e8a0', shift=0),
                         apple_lane_enable=dict(register='0x1638c0', shift=8),
                         adlp_lane_enable=dict(register='0x16e8c0', shift=0)),
                findings=[
                    'Init DDI5 jump table selects the block that stores DDI_BUF_CTL 0x64500 at port+0x590; not inferred from adjacency.',
                    'Object vptr is ZTV+16: object callback +0x120 selects raw +0x130 = resetSoftwareState, not raw +0x120 (zero).',
                    'Base setPortMode publishes port+0x550 and returns integer zero; HAL calls it before AUX acquisition/readiness. Do not treat return as boolean success.',
                    'HAL mode setter uses old global readiness and ownership. It cannot be called unchanged beside an ADL-P ownership transaction.',
                    'Native lane assignment and lane-enable helpers also use global FIA addressing; a mode-probe-only repair leaves these incompatible.',
                    'Lane setter dispatch depends on raw assignment; local DP-alt plan instead follows Linux non-legacy ML0/ML1_0/ML3_0 selection.',
                    'AUX enable increments a reference before hardware work, skipping hardware for existing users or override. A native adapter must verify actual powered state and balance only its own reference.',
                    'FIA local ports share the lane-enable register: per-port locking alone cannot protect RMW against sibling-port updates.',
                    'runAUXCommand locks controller+0xc30. Cursor state try-locks the same recursive lock three times with IOSleep(3 ms); neither fact proves all mode/power callers share its coverage.',
                    'Native AUX polling reads AUX2, but controller+0x918 or +0x1bf0 can synthesize state bits from request bits. Its apparent completion is not independent hardware acknowledgement.',
                    'TC3 DP AUX request/state is bits11/10; TBT uses a different well (request bit23), not the DP-alt lease.',
                    'Native AUX decrement is consumed before hardware cleanup. Failed request readback cannot authorize a second decrement.',
                ], unresolved=[
                    'Native AUX identity validation, DKL indexed-register programming, PLL/training and downstream bridge behavior.',
                    'Native synchronization, shared-FIA serialization, retained lifetime, preexisting ownership, active AUX/link users, sleep/detach and cleanup guarantees.',
                    'Runtime hook coverage, dynamic route installation and output validation; no live backend is implemented.',
                ], runtime_validated=False, device_access=False, external_writes=False, patch_selected=False)


def main():
    report = audit()
    OUT.mkdir(exist_ok=True)
    for name, start, stop in [('native-lanes', 0x8fae0, 0x90128), ('native-mode', 0x8f344, 0x8faa0),
                              ('base-mode', 0x262ea, 0x26436), ('native-init', 0x8e298, 0x8ed48),
                              ('native-aux-enable', 0x8b0d6, 0x8b7dc),
                              ('native-aux-state', 0x8b7dc, 0x8c0db),
                              ('native-aux-disable', 0x88cf6, 0x89400),
                              ('native-aux-command', 0x91b64, 0x92138),
                              ('cursor-shared-lock', 0x7e562, 0x7e5ea)]:
        text = subprocess.check_output(['llvm-objdump', '-d', '--x86-asm-syntax=intel',
            f'--start-address={start:#x}', f'--stop-address={stop:#x}', str(FB)], text=True)
        (OUT / f'{name}.disasm.txt').write_text(text)
    (OUT / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
