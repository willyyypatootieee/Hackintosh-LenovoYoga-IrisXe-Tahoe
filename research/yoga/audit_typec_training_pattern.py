"""Exact captured source-pattern encoding audit; no hardware access."""
import hashlib
import json
import struct
import subprocess
from pathlib import Path
from audit_cursor_diag_inputs import FB, image
from audit_typec_native_integration import require
from yoga_fb_match_patch import symbol

ROOT = Path(__file__).resolve().parent
OUT = ROOT / 'cross-build/typec-training-pattern-audit-20261024'


def audit():
    data, at = image(FB)
    require(hashlib.sha256(data).hexdigest() ==
            '3f2bb3336507242ddc3f69c5d7645d19ce1cd4d8748bdb4b4c8858c4b49103df', 'Wrong framebuffer')
    name = '__ZN21AppleIntelDisplayPath22setLinkTrainingPatternE19LinkTrainingPattern'
    require(symbol(data, name) == 0xee060, 'Native pattern setter')
    checks = {
        0xee101: '49 8b 86 48 36 00 00 48 85 c0 74 4b',
        0xee114: '0f b6 90 17 01 00 00',
        0xee125: '83 fb 05 0f 87 5a 01 00 00',
        0xee12e: '83 ca f0 c1 e2 1b 89 d8 48 8d 0d ab 01 00 00 48 63 04 81 48 01 c8 ff e0',
        0xee14d: '81 ca 00 02 00 00',
        0xee1bb: '81 ca 00 01 00 00',
        0xee1ca: '81 ca 00 04 00 00',
        0xee1d9: '81 ca 00 05 00 00',
        0xee1e8: '81 ca 00 03 00 00',
        0xee1fc: '49 8b 86 b0 32 00 00 48 8b b8 40 4a 00 00',
        0xee211: '41 8b b6 40 36 00 00 c1 e6 0c 81 c6 40 05 06 00 48 8b 07 ff 90 30 01 00 00',
        0xee22a: '31 db',
        0xee27d: '89 d8',
    }
    for address, expected in checks.items():
        raw = bytes.fromhex(expected)
        require(at(address, len(raw)) == raw, f'Pattern byte mismatch {address:#x}')
    targets = [0xee2e8 + struct.unpack('<i', at(0xee2e8 + 4*i, 4))[0] for i in range(6)]
    require(targets == [0xee146,0xee1ab,0xee1b4,0xee1c3,0xee1d2,0xee1e1], 'Pattern switch targets')
    manager_write = struct.unpack('<Q', at(0x13cb40 + 16 + 0x130, 8))[0]
    require(manager_write == 0x8dce6, 'Manager write target')
    return dict(symbol=name, address='0xee060', byte_checks=len(checks),
        switch_targets=[hex(a) for a in targets],
        native_argument_to_source_bits={str(i): hex(v) for i,v in enumerate([0x200,0,0x100,0x400,0x500,0x300])},
        dpcd_to_native_argument={'0x21':1,'0x22':2,'0x23':3,'0x07':4,'0x00':5},
        native_argument_zero_is_idle_not_disable=True,
        native_write_address='0x60540 + (path_field_0x3640 << 12)',
        native_write_is_full_word=True, native_success_has_readback=False,
        findings=[
            'Apple pattern argument is an enum0..5, not DPCD byte encoding.',
            'Argument0 writes idle bits0x200; normal/disabled-training is argument5 bits0x300. TPS4 is argument4 bits0x500, not DPCD7.',
            'Path+0x3648 supplies a byte at+0x117; OR0xf0 then shift27 forms the upper source word. Setter does not read prior control before full-word write.',
            'Path+0x3640 selects a 0x1000-strided address based at0x60540; exact Yoga path identity and register generation mapping remain to prove.',
            'Native writer return is not tested; ebx is explicitly zeroed and returned as success, with no source-register readback in this body.',
            'Do not forward this setter or substitute DPCD values directly: enum translation, retained path/MMIO identity, reviewed mask preservation and actual readback are required.',
        ], native_backend_enabled=False, device_access=False, external_writes=False)


def main():
    report = audit()
    OUT.mkdir(exist_ok=True)
    (OUT / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    text = subprocess.check_output(['llvm-objdump','-d','--x86-asm-syntax=intel',
        '--start-address=0xee060','--stop-address=0xee300',str(FB)], text=True)
    (OUT / 'native-pattern-setter.disasm.txt').write_text(text)
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
