#!/usr/bin/env python3
"""Reproduce BCS findings from local copied evidence; never open devices.

Raw register snapshots can include reset preparation. Decode bits, but do not
promote a snapshot or an instruction boundary to a proven initiating cause.
"""
import hashlib
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parent
OUT = ROOT / 'cross-build/fresh-bcs-analysis-20261024'
CAPTURE = ROOT / 'display-failures-readonly-20261024-032122/copies/Library/Logs/DiagnosticReports'
LOGS = ROOT / 'latest-unified-readonly-20261024-032122'
ACCEL = ROOT / ('installed-signatures-readonly-20261023-233323/copies/data/'
                'Library/Extensions/AppleIntelTGLGraphics.kext/Contents/MacOS/AppleIntelTGLGraphics')


def raw_mmio(text):
    """Read raw rows only, avoiding human-readable mislabeled register fields."""
    result = {}
    for address, words in re.findall(r'^0x([0-9a-fA-F]{8})\s+:\s+((?:0x[0-9a-fA-F]{8}\s*)+)$', text, re.M):
        start = int(address, 16)
        for i, word in enumerate(words.split()):
            result[start + 4*i] = int(word, 16)
    return result


def report(path):
    text = path.read_text(errors='replace')
    mmio = raw_mmio(text)
    bcs = re.search(r'-- BCS Ring --(.*?)-- VCS0 Ring --', text, re.S)
    registers = dict((name.lower(), int(value, 16)) for name, value in re.findall(
        r'RING_(HEAD|TAIL|START|CTRL)\s*=\s*(0x[0-9a-fA-F]+)', bcs.group(1)))
    instruction = re.search(r'IPEHR=(0x[0-9a-fA-F]+)', text)
    context = re.search(r'Processing hang for hardware context ID\[(\d+)\]', text)
    selected = {hex(a): hex(mmio[a]) for a in (0x22068, 0x22074, 0x22078,
                0x22080, 0x220d0, 0x2220c, 0x2229c) if a in mmio}
    reset = mmio.get(0x220d0)
    return dict(file=path.name, sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                context=int(context[1]) if context else None,
                bcs_ring={k: hex(v) for k, v in registers.items()},
                ipehr=instruction[1] if instruction else None, raw_mmio=selected,
                reset_ctl_bits=None if reset is None else dict(
                    catastrophic_error=bool(reset & 4), ready_to_reset=bool(reset & 2),
                    request_reset=bool(reset & 1)))


def main():
    assert hashlib.sha256(ACCEL.read_bytes()).hexdigest() == '2df441e29032a68c0945942996f12337703689c18e3a447d4d15cfdab6858d5f'
    reports = [report(p) for p in sorted(CAPTURE.glob('*.gpuRestart'))]
    # Exact epilogues preserve upper r14 bits; the original callers test AL.
    for name in ('engine-start', 'accelerator-start'):
        text = (OUT / (name + '.txt')).read_text()
        assert re.search(r'mov\s+r14b, 0x1', text)
        assert re.search(r'mov\s+eax, r14d', text)
    sample = CAPTURE / 'Kernel_2026-10-24-030636_willys-MacBook-Pro.gpuRestart'
    text = sample.read_text()
    assert '0x000000d4 : 0x03800000' in text
    assert '0x000000e4 : 0x00000000' in text
    mmio = raw_mmio(text)
    assert mmio[0x2220c] == 0x03800000
    assert mmio[0x220d0] == 5
    assert mmio[0x22034] == 0xd4 and mmio[0x22030] == 0xd8
    submit = (OUT / 'report-head.txt').read_text()
    assert re.search(r'4335d:.*mov\s+esi, 0x3800000', submit)
    timelines = {}
    pattern = re.compile(r'V221: stamp|V221: GFX start|V163:.*ret=|V142:|V186:|'
                         r'timeout waiting|GPU hang:|V152\[|V509: DW1=00000000')
    for boot in ('previous', 'latest'):
        lines = (LOGS / f'fresh-{boot}-kernel-recovered.txt').read_text().splitlines()
        timelines[boot] = [line for line in lines if pattern.search(line)]
    result = dict(external_writes=False, repair_deployed=False, reports=reports,
                  timelines=timelines, conclusions=dict(
                      startup_timeout_channel=8, reported_bcs_stamp_index=32,
                      same_channel_proven=False,
                      startup_returns_are_bool_not_ior_return=True,
                      bcs_current_command='MI_REPORT_HEAD (raw CSCMDOP 0x2220c)',
                      bcs_last_ipehr='MI_FLUSH_DW (0x13004103)',
                      hardware_tail='0xd8; later captured words are outside this submitted tail',
                      initial_blit_batch='0x417b8000 is referenced but its contents are not in this ring excerpt',
                      initiating_fault_proven=False,
                      reset_ctl_caveat='0x5 decodes CAT_ERROR|REQUEST_RESET, but capture may follow reset preparation',
                      recovery_scope='XE-GPUREC accepts render mask 2 and RCS/CCS lists; Apple BCS mask is 4'))
    (OUT / 'fresh-bcs-findings.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(dict(reports=len(reports), assertions='passed',
                          external_writes=False, output=str(OUT / 'fresh-bcs-findings.json')), indent=2))


if __name__ == '__main__':
    main()
