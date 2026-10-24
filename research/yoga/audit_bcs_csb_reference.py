"""Compare exact captured Apple CSB bounds with recovered Linux references."""
import hashlib
import json
from collections import Counter
from audit_cursor_diag_inputs import image
from analyze_fresh_bcs import ACCEL, CAPTURE, OUT, ROOT, raw_mmio


def main():
    data, at = image(ACCEL)
    assert hashlib.sha256(data).hexdigest() == '2df441e29032a68c0945942996f12337703689c18e3a447d4d15cfdab6858d5f'
    # Native function bounds the producer index to 0..5 and wraps reader at 5.
    assert at(0x3b282, 4).hex() == '4183fe05'
    assert at(0x3b2d4, 3).hex() == '83f805'
    assert at(0x3b27e, 4).hex() == '4183e60f'
    # Its CSB decoder already uses the Gen12 SW context-ID field; it is NOT
    # justified to replace this with an older-format decoder wholesale.
    assert at(0x3caec, 5).hex() == 'b80080ff03'
    ref = ROOT / 'cross-build/linux-v6.12-display-gt-reference-20261024'
    header = (ref / 'gt__intel_lrc_reg.h').read_text()
    source = (ref / 'gt__intel_execlists_submission.c').read_text()
    assert '#define GEN8_CSB_ENTRIES 6' in header
    assert '#define GEN11_CSB_ENTRIES 12' in header
    assert 'if (GRAPHICS_VER(i915) < 11)\n\t\texeclists->csb_size = GEN8_CSB_ENTRIES;\n\telse\n\t\texeclists->csb_size = GEN11_CSB_ENTRIES;' in source
    assert '_MASKED_BIT_ENABLE(GEN11_GFX_DISABLE_LEGACY_MODE)' in source
    reports = [raw_mmio(p.read_text(errors='replace')) for p in sorted(CAPTURE.glob('*.gpuRestart'))]
    assert len(reports) == 20
    modes = Counter(hex(m.get(0x2229c, -1)) for m in reports)
    pointers = Counter(hex(m.get(0x223a0, -1)) for m in reports)
    assert modes == {'0x0': 20}
    assert pointers == {'0x0': 17, '0x3003': 2, '0x2002': 1}
    result = dict(reports=20, apple_csb_slots=6, linux_gen11_plus_csb_slots=12,
                  apple_context_id_mask='0x03ff8000 (bits 25:15, already Gen12-style)',
                  bcs_gfx_mode_snapshot_counts=dict(modes), bcs_csb_ptr_snapshot_counts=dict(pointers),
                  finding='Different CSB sizing/setup is a concrete compatibility audit lead, not an established BCS failure cause',
                  limitations=['Snapshots may be after reset and do not establish initial submission mode',
                    'No captured pointer demonstrates an index outside Apple 0..5 bounds',
                    'Changing 5 to 11 alone would change bounds without proving buffer capacity, initialization or MMIO layout',
                    'NootedGreen has its own scheduler mode writes and six-slot diagnostic assumptions; whole path needs review'],
                  external_writes=False, runtime_validated=False)
    (OUT / 'bcs-csb-reference-audit.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
