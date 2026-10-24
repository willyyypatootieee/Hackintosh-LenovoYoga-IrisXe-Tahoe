"""Hash-checked offline Type-C switch/HPD/power-loop audit; no hardware access."""
import hashlib
import json
import re
import struct
import subprocess
from pathlib import Path
from audit_cursor_diag_inputs import FB, image

ROOT = Path(__file__).resolve().parent
OUT = ROOT / 'cross-build/typec-power-irq-audit-20261024'
REF = ROOT / 'cross-build/linux-v6.12-display-gt-reference-20261024'


def main():
    data, at = image(FB)
    digest = hashlib.sha256(data).hexdigest()
    assert digest == '3f2bb3336507242ddc3f69c5d7645d19ce1cd4d8748bdb4b4c8858c4b49103df'
    base = 0xb2978
    expected = [0xb2832, 0xb2849, 0xb2860, 0xb2874]
    mapping = {ddi: base + struct.unpack('<i', at(base + 4 * (ddi - 3), 4))[0] for ddi in range(3, 7)}
    assert list(mapping.values()) == expected
    # Exact binary evidence: DDI5 -> software cache masks, not raw HPD bits.
    assert at(0xb2867, 11).hex() == 'b9000c000041bf30000300'
    assert at(0xaed8a, 3).hex() == 'f6c304'  # TC3 TBT IIR bit 2
    assert at(0xaeda4, 7).hex() == '4181ce00000200'  # long -> software bit 17
    assert at(0xaedb9, 7).hex() == '4181ce00000100'  # short -> software bit 16
    assert at(0xaee4c, 4).hex() == '0fbae312'  # TC3 DP-alt IIR bit 18
    assert at(0xaee66, 4).hex() == '4183ce20'  # long -> software bit 5
    assert at(0xaee77, 4).hex() == '4183ce10'  # short -> software bit 4
    assert at(0x8b990, 6).hex() == '41bc14000000'  # 20 iterations
    assert at(0x8bce2, 5).hex() == 'bf01000000'  # delay argument 1
    relocs = subprocess.check_output(['llvm-objdump', '--macho', '--reloc', str(FB)], text=True)
    for offset, symbol in (('0008bce8', '_IODelay'), ('000ae910', '_OSBitOrAtomic'), ('000ae934', '_OSBitOrAtomic')):
        assert any(line.startswith(offset + ' ') and line.endswith(symbol) for line in relocs.splitlines())
    # Confirm no external relocation occurs inside the proposed whole-function
    # cursor patch; otherwise runtime bytes would not match the capture.
    offsets = [int(m.group(1), 16) for line in relocs.splitlines()
               if (m := re.match(r'^([0-9a-f]{8})\s+(?:True|False)\s+', line))]
    assert not any(0x7eb2c <= off < 0x7ec18 for off in offsets)
    power = (REF / 'display__intel_display_power_map.c').read_text()
    assert '.fixed_enable_delay = true,\n\t\t/* WA_14017248603: adlp */\n\t\t.enable_timeout = 500,' in power
    regs = (REF / 'i915_reg.h').read_text()
    assert '#define GEN11_DE_HPD_IIR\t\t_MMIO(0x44478)' in regs
    implementation = (REF / 'display__intel_display_power_well.c').read_text()
    assert 'if (IS_DG2(dev_priv) && power_well->desc->fixed_enable_delay)' in implementation
    assert 'HSW_PWR_WELL_CTL_STATE(pw_idx), timeout)' in implementation
    report = dict(framebuffer_sha256=digest, ddi_jump_table={str(k): hex(v) for k, v in mapping.items()},
        tc3=dict(ddi=5, raw_tbt_iir_mask=hex(1 << 2), raw_dpalt_iir_mask=hex(1 << 18),
                 software_cache_e0c_mask='0x30030', software_cache_e10_mask='0xc00',
                 result='TC3 DP-alt/TBT HPD events translate to the masks consumed by DDI5; no simple mask replacement justified'),
        aux_power=dict(apple_loop_iterations=20, delay_import='_IODelay', delay_argument_us=1,
                       minimum_explicit_delay_budget_us=20,
                       linux_adlp_ack_timeout_ms=500,
                       linux_fixed_delay_branch='DG2 only, not ADL-P; descriptor flag is not a 500ms forced sleep',
                       result='Large platform sequencing/timing difference, not proof that changing this shared loop fixes USB-C'),
        cursor_patch_external_relocations=0, external_writes=False, runtime_validated=False)
    (OUT / 'irq-power-findings.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
