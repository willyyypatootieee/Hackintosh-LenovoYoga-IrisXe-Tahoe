"""Pinned framebuffer Dekel addressing audit. Offline only; no device access."""
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
OUT = ROOT / 'cross-build/typec-dkl-access-audit-20261024'
REF = ROOT / 'cross-build/linux-v6.12-display-gt-reference-20261024'
REFERENCES = {
    'display__intel_dkl_phy_regs.h': 'c60cdded1a2c9e3ee85d85e87b0d2628f621913c7d7e08a82f2b98fda9785a8f',
    'display__intel_dkl_phy.c': 'e8f0e60581d913f467dcc9943ce2ae6a6934eb0a2258ef993389e202a1022218',
    'display__intel_ddi.c': '75403786004708ecb011a9be131c68b2a406b9e7e653c00a25d1ea1d8a475ac8',
    'display__intel_dpll_mgr.c': '80efd229c1d29cfbdd62e2b039e3181c6021d040922b19bc9b041010df18b749',
    'i915_reg.h': '87395a4ab0424c66f599e05e5c7bcaef6ba1c7e0803208bb2648866701193c78',
    'display__intel_display_reg_defs.h': '7dbebd08cf96dc463a715906c718f14d078bccb7d414db469c9be9ac097896bd',
    'i915_reg_defs.h': 'c24ce430f5fbb32b6ac74de61f495975532c982e0a8a1e7e02bb0ddfd78cef86',
    'display__intel_ddi_buf_trans.c': '4fe99978f5e41a06f09d1cf3d8f2a4809800de1869b1390e2cd2d7874c42ed0b',
    'display__intel_ddi_buf_trans.h': 'ec4433d9f675b37e769fc219c753ee57f7404742812dbf0fe39d4c494fbdd09c',
}


def direct_edges(text):
    """Instruction boundaries come from objdump, not byte-pattern scanning."""
    edges = {}
    owner = None
    for line in text.splitlines():
        match = re.match(r'^([0-9a-f]+) <([^>]+)>:', line)
        if match:
            owner = match[2]
        match = re.match(r'\s*([0-9a-f]+):.*\s(call|jmp)\s+0x([0-9a-f]+)\s', line)
        if match and int(match[3], 16) in (0x90234, 0x902ea):
            edges[int(match[1], 16)] = (int(match[3], 16), owner)
    return edges


def check_relative_branch(at, address, target):
    raw = at(address, 5)
    require(raw[0] in (0xe8, 0xe9), f'Not a direct branch: {address:#x}')
    require(address + 5 + struct.unpack('<i', raw[1:])[0] == target,
            f'Wrong direct branch target: {address:#x}')


def shared_gate_users(text, at):
    """Exact immediate-register users, not coincidental RIP counter addresses.

    This closes one known coverage gap, NOT arbitrary computed-address aliases,
    virtual dispatch or other kexts. Consequently it is not write authorization.
    """
    expected = {}
    groups = [
        ('__ZN17AppleIntelPortHAL18enableDklPhyClocksEv', [0x928f5, 0x92fff]),
        ('__ZN17AppleIntelPortHAL19disableDklPhyClocksEv', [0x930ee, 0x93139]),
        ('__ZN24AppleIntelBaseController12enableClocksEP14AppleIntelPortS1_P11ClockParams',
         [0xa5fc6, 0xa61b1, 0xa6340, 0xa6602, 0xa69d9, 0xa6cb3]),
        ('__ZN24AppleIntelBaseController13disableClocksEP14AppleIntelPortS1_',
         [0xa6fdc, 0xa7031, 0xa7123, 0xa7178]),
    ]
    for owner, sites in groups:
        for site in sites:
            require(at(site, 5) == bytes.fromhex('be 80 42 16 00'), f'Gate immediate {site:#x}')
            expected[site] = owner
    found = {}
    owner = None
    for line in text.splitlines():
        match = re.match(r'^([0-9a-f]+) <([^>]+)>:', line)
        if match:
            owner = match[2]
        match = re.match(r'\s*([0-9a-f]+):.*\bmov\s+esi, 0x164280\s*$', line)
        if match:
            found[int(match[1], 16)] = owner
    require(found == expected, 'Shared DPCLKA immediate caller coverage changed')
    return {hex(site): owner for site, owner in sorted(found.items())}


def controller_shared_gate_dispatch(at):
    """Pin reviewed merged write blocks; never infer next textual call after JMP."""
    checks = {
        0xa61b1: 'be 80 42 16 00 e8 41 72 fe ff',
        0xa6340: 'be 80 42 16 00 e8 b2 70 fe ff',
        0xa6602: 'be 80 42 16 00 8b 55 b0 e9 d2 03 00 00',
        0xa6cb3: 'be 80 42 16 00 8b 55 c4 e9 21 fd ff ff',
        0xa69cf: '49 8b bd 40 0c 00 00 48 8b 07 be 80 42 16 00 44 89 fa ff 90 30 01 00 00',
        0xa6fd5: '49 8b be 40 0c 00 00 be 80 42 16 00 e8 16 64 fe ff',
        0xa7015: '48 8d 0d 74 c5 04 00 48 ff 05 e5 a8 0b 00 44 0b 3c 81 49 8b be 40 0c 00 00 48 8b 07 be 80 42 16 00 44 89 fa ff 90 30 01 00 00',
        0xa7175: '48 8b 0f be 80 42 16 00 89 c2 ff 91 30 01 00 00',
    }
    for address, expected in checks.items():
        raw = bytes.fromhex(expected)
        require(at(address, len(raw)) == raw, f'Controller shared gate dispatch {address:#x}')
    check_relative_branch(at, 0xa660a, 0xa69e1)
    check_relative_branch(at, 0xa6cbb, 0xa69e1)
    require(struct.unpack('<Q', at(0x13cb40 + 16 + 0x130, 8))[0] == 0x8dce6,
            'Manager actual write virtual slot')
    return dict(byte_checks=len(checks), merged_enable_write='0xa69e1',
                merged_enable_branches=['0xa660a', '0xa6cbb'],
                disable_write_sites=['0xa7039', '0xa717f'],
                manager_write_vptr_slot='0x130', manager_write_target='0x8dce6',
                native_whole_rmw_exclusion_proven=False)


def audit():
    data, at = image(FB)
    digest = hashlib.sha256(data).hexdigest()
    require(digest == '3f2bb3336507242ddc3f69c5d7645d19ce1cd4d8748bdb4b4c8858c4b49103df', 'Wrong framebuffer')
    symbols = {
        '__ZN17AppleIntelPortHAL15ReadDekelPhyRegEj': 0x90234,
        '__ZN17AppleIntelPortHAL16WriteDekelPhyRegEjj': 0x902ea,
        '__ZN17AppleIntelPortHAL12setDklDPModeEv': 0x903a4,
        '__ZN17AppleIntelPortHAL18enableDklPhyClocksEv': 0x92608,
        '__ZN17AppleIntelPortHAL19disableDklPhyClocksEv': 0x9302c,
        '__ZN17AppleIntelPortHAL17setPhyClockGatingEb': 0x90128,
        '__ZN17AppleIntelPortHAL29setVoltageSwingAndPreEmphasisEjjP15PortDriveConfig': 0x90588,
        '__ZN24AppleIntelBaseController12enableClocksEP14AppleIntelPortS1_P11ClockParams': 0xa5dae,
        '__ZN24AppleIntelBaseController13disableClocksEP14AppleIntelPortS1_': 0xa6ed4,
        '__ZN24AppleIntelBaseController14doLinkTrainingEP18AGDCDPPortConfig_tS1_': 0xa2132,
        '__ZN14AppleIntelPort12linkTrainingEP18AGDCDPPortConfig_t': 0x26c54,
    }
    for name, address in symbols.items():
        require(symbol(data, name) == address, name)
    checks = {
        0x8ea2d: 'e9 5d 01 00 00', # DDI5 init joins common DKL table at 8eb8f
        0x90279: '8b 3a c1 e7 0c',
        0x90280: '8b b4 8b 80 05 00 00',
        0x90287: '89 f1 c1 e9 0e 83 e1 03 81 e6 ff 3f ff ff',
        0x90295: '44 8d bc 37 00 50 16 00',
        0x902a8: '83 3a 06 77 14',
        0x902ba: 'ba a0 10 10 00',
        0x902c7: 'ba a4 10 10 00',
        0x902cc: 'e8 09 f2 f9 ff',
        0x902e4: 'e9 d7 f1 f9 ff',
        0x90333: '8b 3a c1 e7 0c',
        0x9033a: '8b b4 8b 80 05 00 00',
        0x90341: '89 f1 c1 e9 0e 83 e1 03 81 e6 ff 3f ff ff',
        0x9034f: '44 8d a4 37 00 50 16 00',
        0x90362: '83 3a 06 77 14',
        0x90374: 'ba a0 10 10 00',
        0x90381: 'ba a4 10 10 00',
        0x90386: 'e8 4f f1 f9 ff',
        0x9039f: 'e9 36 f1 f9 ff',
        0x90406: 'be 48 00 00 00', # DP_MODE internal table index 0x48
        0x90416: 'be 49 00 00 00',
        0x92711: 'be 38 00 00 00', # clock helper feeds 0x46030 through DKL helper
        0x92716: 'e8 19 db ff ff',
        0x9271b: '0d 00 00 00 08', # PLL_POWER_ENABLE
        0x92748: '0f ba e0 1a', # PLL_POWER_STATE
        0x92f42: '81 ca 00 00 00 80', # PLL_ENABLE
        0x92f76: '0f ba e0 1e', # PLL_LOCK
        0x93152: '25 ff ff ff 7f', # clear PLL_ENABLE
        0x93182: '0f ba e0 1e', # wait unlock
        0x93239: '41 81 e7 ff ff ff f7', # clear PLL_POWER_ENABLE
        0x93269: '0f ba e0 1a', # wait power down
        # Reviewed receiver provenance in the controller's DKL clock path:
        # same port object r14; caller does NOT test the clock return before mode.
        0xa6264: '49 8b 06 4c 89 f7 ff 90 50 01 00 00',
        0xa6270: '49 8b 06 4c 89 f7 ff 90 98 01 00 00',
        # linkTraining reloads the same port object, then dispatches drive levels.
        0x2729a: '49 8b 04 24 4c 89 e7 44 89 fe 44 89 f2 31 c9 ff 90 40 01 00 00',
        # HAL clock mapping: direct selector read, shared gate read, DDI-3
        # indexed mask, then select-before-ungate after PLL lock succeeds.
        0x928e0: '41 8b b5 4c 06 00 00 4c 89 ef e8 95 cb f9 ff',
        0x928f2: '4c 89 ef be 80 42 16 00 e8 85 cb f9 ff',
        0x92906: '49 8b 8d 48 05 00 00 8b 09 83 c1 fd 83 f9 06',
        0x92934: '48 8d 0d 4d 0a 06 00 23 04 91',
        0x92fd5: '41 81 e4 ff ff ff 0f 41 81 cc 00 00 00 80',
        0x92fea: '41 8b b6 4c 06 00 00 4c 89 f7 44 89 e2 e8 a2 c4 f9 ff',
        0x92ffc: '4c 89 f7 be 80 42 16 00 44 89 fa e8 92 c4 f9 ff',
        # The controller releases DDI power BEFORE the later HAL disable.
        0xa6f86: '48 89 df e8 60 86 f8 ff',
        0xa6fc1: '83 bb 50 05 00 00 02',
        0xa7204: '48 8b 03 48 89 df ff 90 58 01 00 00',
        # Native AGDC caller does NOT test enableClocks status. In contrast it
        # preserves/tests the integer linkTraining status and returns that value.
        0xa23c2: '4c 89 ff 48 89 de 31 d2 31 c9 e8 dd 39 00 00',
        0xa23d1: '41 8a 44 24 13 3a 43 11',
        0xa246b: '48 89 df 4c 89 f6 e8 de 47 f8 ff 41 89 c7 85 c0 75 0e',
        0xa2496: '48 89 df be 02 02 00 00 4c 89 f2 b9 04 00 00 00 e8 f3 68 f8 ff',
        0xa24fd: '44 89 f8',
    }
    for address, expected in checks.items():
        raw = bytes.fromhex(expected)
        require(at(address, len(raw)) == raw, f'Byte mismatch {address:#x}')

    # Decode only exact movabs/store and immediate-store forms in this bounded
    # straight-line common table block. No unchecked disassembly text parsing.
    fields = {}
    cursor = 0x8eb8f
    while cursor < 0x8ec4f:
        if at(cursor, 2) == b'\x48\xb8':
            value = struct.unpack('<Q', at(cursor + 2, 8))[0]
            require(at(cursor + 10, 3) == bytes.fromhex('49 89 87'), 'Unexpected table store')
            offset = struct.unpack('<I', at(cursor + 13, 4))[0]
            fields[offset] = value & 0xffffffff
            fields[offset + 4] = value >> 32
            cursor += 17
        else:
            require(at(cursor, 3) == bytes.fromhex('41 c7 87'), 'Unexpected immediate table store')
            offset, value = struct.unpack('<II', at(cursor + 3, 8))
            fields[offset] = value
            cursor += 11
    require(cursor == 0x8ec4f, 'Table block end')
    expected_fields = {
        0x650: 0x20d4, 0x654: 0x20d8, 0x658: 0x2130, 0x65c: 0x212c,
        0x660: 0x46030, 0x664: 0x2200, 0x668: 0x2204, 0x66c: 0x2208,
        0x670: 0x168210, 0x674: 0x168214, 0x678: 0x168218,
        0x67c: 0x220c, 0x680: 0x2244, 0x684: 0x224c,
        0x688: 0x2c0, 0x68c: 0x2c4, 0x690: 0x2c8,
        0x694: 0x12c0, 0x698: 0x12c4, 0x69c: 0x12c8,
        0x6a0: 0xa0, 0x6a4: 0x10a0,
    }
    require(fields == expected_fields, 'Unexpected common table')
    for filename, sha in REFERENCES.items():
        require(hashlib.sha256((REF / filename).read_bytes()).hexdigest() == sha, filename)

    gate_off_bits = [12, 13, 14, 21, 22, 23]
    expected_gate_masks = [0xffffffff ^ (1 << bit) for bit in gate_off_bits]
    for base in [0xf3388, 0xf356c]:
        require(list(struct.unpack('<6I', at(base, 24))) == expected_gate_masks,
                f'Native TC gate clear masks {base:#x}')
    require(list(struct.unpack('<6I', at(0xf3590, 24))) == [1 << bit for bit in gate_off_bits],
            'Native TC gate set masks')

    # Enumerate all direct users, including the tail jump in clock gating. A
    # same-number virtual slot on another class is NOT a proven DKL caller.
    expected_edges = {}
    groups = [
        ('__ZN17AppleIntelPortHAL17setPhyClockGatingEb',
         [0x9019b, 0x901ab], [0x90210, 0x9022e]),
        ('__ZN17AppleIntelPortHAL12setDklDPModeEv',
         [0x9040b, 0x9041b], [0x9052e, 0x90546]),
        ('__ZN17AppleIntelPortHAL29setVoltageSwingAndPreEmphasisEjjP15PortDriveConfig',
         [0x909a8, 0x909c0, 0x90a09], [0x909f8, 0x90a18]),
        ('__ZN17AppleIntelPortHAL18enableDklPhyClocksEv',
         [0x92716, 0x92743, 0x928a5, 0x928b4, 0x928c4, 0x928d4, 0x92f71],
         [0x9272a, 0x92abf, 0x92acf, 0x92adf, 0x92efb, 0x92f0b, 0x92f1b,
          0x92f2b, 0x92f3a, 0x92f50]),
        ('__ZN17AppleIntelPortHAL19disableDklPhyClocksEv',
         [0x9314d, 0x9317a, 0x93264], [0x93161, 0x9324b]),
    ]
    for owner, reads, writes in groups:
        for target, sites in [(0x90234, reads), (0x902ea, writes)]:
            for address in sites:
                check_relative_branch(at, address, target)
                expected_edges[address] = (target, owner)
    text = subprocess.check_output(['llvm-objdump', '-d', '--x86-asm-syntax=intel', str(FB)], text=True)
    require(direct_edges(text) == expected_edges, 'Direct DKL caller coverage changed')
    gate_users = shared_gate_users(text, at)
    gate_dispatch = controller_shared_gate_dispatch(at)
    require(symbol(data, '__ZTV17AppleIntelPortHAL') == 0x13cda8, 'HAL vtable')
    slots = {}
    for slot, target in {0x140: 0x90588, 0x150: 0x92608, 0x158: 0x9302c,
                         0x198: 0x903a4, 0x1a0: 0x90128}.items():
        require(struct.unpack('<Q', at(0x13cda8 + 16 + slot, 8))[0] == target,
                f'HAL object vptr slot {slot:#x}')
        slots[hex(slot)] = hex(target)

    regs = (REF / 'i915_reg.h').read_text()
    for expression, label in [
        (r'#define _PORT_CLK_SEL_A\s+0x46100', 'DDI selector base'),
        (r'#define _PORT_CLK_SEL_B\s+0x46104', 'DDI selector stride'),
        (r'#define\s+DDI_CLK_SEL_MASK\s+REG_GENMASK\(31, 28\)', 'DDI selector mask'),
        (r'#define\s+DDI_CLK_SEL_MG\s+REG_FIELD_PREP\(DDI_CLK_SEL_MASK, 0x8\)', 'MG clock selection'),
        (r'#define ICL_DPCLKA_CFGCR0\s+_MMIO\(0x164280\)', 'Shared clock gate register'),
    ]:
        require(re.search(expression, regs) is not None, label)
    require('(tc_port) + 12 :' in regs and '(tc_port) - TC_PORT_4 + 21' in regs,
            'TC shared gate bit formula')
    require(re.search(r'#define PORTTC1_PLL_ENABLE\s+0x46038', regs) is not None, 'TC1 PLL macro')
    require(re.search(r'#define PORTTC2_PLL_ENABLE\s+0x46040', regs) is not None, 'TC2 PLL macro')
    require('ADLP_PORTTC_PLL_ENABLE(tc_port)' in regs, 'ADL-P PLL selection macro')
    require(re.search(r'#define ADLP_PORTTC_PLL_ENABLE\(tc_port\)\s+_MMIO_PORT\(\(tc_port\),\s*\\\s*PORTTC1_PLL_ENABLE,\s*\\\s*PORTTC2_PLL_ENABLE\)', regs) is not None,
            'ADL-P PLL macro expansion')
    display_defs = (REF / 'display__intel_display_reg_defs.h').read_text()
    require('#define _PORT(port, a, b)\t\t_PICK_EVEN(port, a, b)' in display_defs, 'Port stride expansion')
    require('#define _MMIO_PORT(port, a, b)\t\t_MMIO(_PORT(port, a, b))' in display_defs, 'MMIO port expansion')
    require('#define _PICK_EVEN(__index, __a, __b) ((__a) + (__index) * ((__b) - (__a)))'
            in (REF / 'i915_reg_defs.h').read_text(), 'Even stride formula')
    for name, bit in [('PLL_ENABLE', 31), ('PLL_LOCK', 30),
                      ('PLL_POWER_ENABLE', 27), ('PLL_POWER_STATE', 26)]:
        require(re.search(r'#define\s+' + name + r'\s+REG_BIT\(' + str(bit) + r'\)', regs) is not None,
                f'PLL bit {name}')
    pll_policy = (REF / 'display__intel_dpll_mgr.c').read_text()
    require('if (IS_ALDERLAKE_P(i915))\n\t\treturn ADLP_PORTTC_PLL_ENABLE(tc_port);' in pll_policy,
            'ADL-P platform policy')
    # _MMIO_PORT uses the first-two-register stride. This is a DIRECT display
    # MMIO register: no HIP selector and no PHY aperture addition.
    tc3_pll_enable = 0x46038 + 2 * (0x46040 - 0x46038)
    require(tc3_pll_enable == 0x46048, 'TC3 PLL enable address')

    examples = []
    for index, canonical in [(0x48, 0xa0), (0x49, 0x10a0), (0x34, 0x20d4),
                             (0x3c, 0x2210), (0x3d, 0x2214), (0x3e, 0x2218)]:
        entry = fields[0x580 + 4 * index]
        examples.append(dict(native_table_index=hex(index), native_table_value=hex(entry),
                             native_aperture=hex(0x165000 + 5 * 0x1000 + (entry & 0xffff3fff)),
                             native_selector_value=hex((entry >> 14) & 3),
                             canonical_internal_offset=hex(canonical),
                             adlp_tc3_aperture=hex(0x16a000 + (canonical & 0xfff)),
                             adlp_tc3_selector_value=hex((canonical >> 12) << 16)))
    return dict(framebuffer_sha256=digest, symbols={n: hex(a) for n, a in symbols.items()},
                byte_checks=len(checks), decoded_table_entries=len(fields),
                direct_dkl_calls={hex(a): dict(target=hex(t), owner=o)
                                  for a, (t, o) in sorted(expected_edges.items())},
                hal_object_vptr_slots=slots,
                shared_gate_immediate_users=gate_users,
                shared_gate_user_coverage_complete=False,
                controller_shared_gate_dispatch=gate_dispatch,
                adlp_tc3_pll_enable=dict(address=hex(tc3_pll_enable), access='direct MMIO, NOT DKL indexed',
                                         power_request_bit=27, power_state_bit=26,
                                          enable_bit=31, lock_bit=30),
                tc3_clock_mapping=dict(selector='0x46114', selector_mask='0xf0000000',
                    dp_alt_selection='0x80000000', shared_gate='0x164280', gate_bit=14,
                    native_clear_mask_tables=['0xf3388', '0xf356c'], native_set_mask_table='0xf3590',
                    mapping_order=['selector', 'ungate'], cleanup_order=['gate', 'clear selector'],
                    native_power_release='0xa6f89', native_later_hal_disable='0xa720a'),
                native_table={hex((off - 0x580)//4): hex(value) for off, value in sorted(fields.items())},
                linux_reference_sha256=REFERENCES, tc3_examples=examples,
                findings=[
                    'DDI5 base 0x165000 + DDI*0x1000 coincides with TC3 aperture base 0x16a000; this alone does not fix bank selection.',
                    'Native helpers extract bits15:14 and do not shift the selector into the TC3 byte. Linux extracts internal bank from bit12 and places it at HIP bits23:16.',
                    'Second lane-group offset 0x10a0 stays in native aperture address 0x16b0a0 rather than TC3 bank1/aperture0x16a0a0.',
                    'Common table PLL entries 0x3c..0x3e contain absolute values 0x168210/14/18; do not pass those as canonical internal offsets.',
                    'Native clock helper also sends index0x38 value0x46030 through the DKL helper; clock-policy/caller repair is required, not just bank-address normalization.',
                    'Controller enableClocks calls the port clock virtual at 0xa626a, then DP mode at 0xa6276 without testing clock success; replacing only the HAL clock body cannot provide failure admission.',
                    'Native linkTraining dispatches drive programming via port vptr+0x140 at 0x272a9; complete replacement needs active-link admission and training failure propagation.',
                    'Linux serializes selector and aperture access/RMW across ports. A new private lock that original Apple DKL helpers do not take is insufficient.',
                    'DKL DP_MODE pin assignment comes from DFLEXPA1, not the DFLEXDPSP lane-capacity nibble. Do not use either as the other.',
                    'Native HAL DP-alt mapping selects MG at port+0x64c, then clears the DDI-3 gate bit. The TC3 selector/gate agree with Linux (0x46114, shared0x164280 bit14); these are direct MMIO, not DKL banked offsets.',
                    'disableClocks unconditionally releases DDI power at 0xa6f89 before its mode branch can dispatch HAL disable at 0xa720a. A HAL-only replacement cannot establish retained-power teardown.',
                    'AGDC doLinkTraining ignores enableClocks status at 0xa23cc, then calls linkTraining at 0xa2471; linkTraining integer status is tested and preserved to return. Its optional four-byte DPCD0x202 read is not a verified source-PHY/PLL acknowledgement.',
                    'Shared DPCLKA0x164280 has14 literal ESI argument sites in four native bodies: HAL enable/disable AND controller enable/disable. Serializing only HAL helpers misses controller direct RMW paths, including non-TC3 users sharing the word. Literal enumeration does not rule out computed aliases or external writers.',
                    'Two controller enable paths jump to shared virtual write0xa69e1; manager vptr+0x130 resolves to WriteRegister32. Next textual call after a jump is not its consumer. Disable reads shared word then ORs native per-DDI mask before virtual write0xa7039, so locking just that final write cannot prevent stale-word lost updates.',
                ], unresolved=[
                    'Full native caller replacement, native lock-order coverage and mode-bound power/ownership authorization.',
                    'Platform PLL programming and native link training; local signal planning is not native wiring or write authorization.',
                ], runtime_validated=False, device_access=False, external_writes=False, patch_selected=False)


def main():
    report = audit()
    OUT.mkdir(exist_ok=True)
    for name, start, stop in [('native-dkl-access', 0x90234, 0x903a4),
                              ('native-dkl-mode', 0x903a4, 0x90588),
                              ('native-dkl-table', 0x8eb8f, 0x8ec4f),
                              ('native-dkl-clocks', 0x92608, 0x9302c),
                              ('native-dkl-clock-disable', 0x9302c, 0x9329a),
                              ('native-dkl-signal', 0x90588, 0x915fc),
                              ('controller-dkl-clock-dispatch', 0xa6253, 0xa627c),
                              ('training-dkl-drive-dispatch', 0x2729a, 0x272af),
                              ('native-clock-mapping', 0x928d9, 0x92941),
                              ('native-clock-map-publish', 0x92fce, 0x9300c),
                              ('controller-disable-power-order', 0xa6f86, 0xa720f),
                               ('agdc-training-clock-admission', 0xa23b2, 0xa250d),
                               ('controller-gate-merged-write-branch', 0xa65f8, 0xa660f),
                               ('controller-gate-merged-write', 0xa69cf, 0xa69ec),
                               ('controller-gate-merged-write-branch2', 0xa6cb3, 0xa6cc0),
                               ('controller-gate-disable-rmw', 0xa6fd5, 0xa703f)]:
        text = subprocess.check_output(['llvm-objdump', '-d', '--x86-asm-syntax=intel',
            f'--start-address={start:#x}', f'--stop-address={stop:#x}', str(FB)], text=True)
        (OUT / f'{name}.disasm.txt').write_text(text)
    (OUT / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
