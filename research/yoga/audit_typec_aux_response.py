"""Captured native AUX response/ABI audit. No disk-device or hardware access."""
import hashlib
import json
import struct
import subprocess
from pathlib import Path

from audit_cursor_diag_inputs import FB, image
from audit_typec_native_integration import require
from yoga_fb_match_patch import symbol

ROOT = Path(__file__).resolve().parent
OUT = ROOT / 'cross-build/typec-aux-response-audit-20261024'


def audit():
    data, at = image(FB)
    require(hashlib.sha256(data).hexdigest() ==
            '3f2bb3336507242ddc3f69c5d7645d19ce1cd4d8748bdb4b4c8858c4b49103df', 'Wrong framebuffer')
    name = '__ZN17AppleIntelPortHAL13runAUXCommandEN14AppleIntelPort11AUX_CommandEPjjb'
    require(symbol(data, name) == 0x91b64, 'AUX original symbol mismatch')
    require(hashlib.sha256(at(0x91b64, 0x92608 - 0x91b64)).hexdigest() ==
            '9fce5ef02571c73e7e6bc665479eebb03e0f1a8a53891e761aaf812c8d2a8156', 'AUX body mismatch')
    checks = {
        0x91bdb: '48 8b b8 30 0c 00 00',  # shared native recursive lock
        0x91d51: '41 8b b6 94 05 00 00',  # AUX_CTL preflight
        0x91dc7: 'e8 d2 d6 f9 ff',        # first word written without byte swap
        0x91dd1: '83 e0 1f c1 e0 14',     # transmit count, NOT receive validation
        0x91e24: '41 83 fd 01',           # command1 writes payload
        0x91e57: '0f ca',                 # payload swapped from host-memory bytes
        0x91f13: '41 8b b6 94 05 00 00', # command launch uses AUX_CTL
        0x91f1f: 'e8 7a d5 f9 ff',
        0x91f4f: '41 8b b6 94 05 00 00', # completion status into r12d
        0x91f5e: '41 89 c4',
        0x91fc9: '41 83 ff 2a',          # 42 polls (10*IODelay(10) each)
        0x92047: '41 0f ba e4 1c',       # timeout bit28
        0x92055: '41 0f ba e4 1e',       # done bit30
        0x9210a: '48 8b b8 30 0c 00 00', # release SAME recursive lock
        0x92134: '41 0f ba e4 19',       # receive error bit25
        0x92146: '41 8b b6 98 05 00 00', # DATA1
        0x9215c: '0f c8 0f b6 d8',       # receive bytes in memory order
        0x92167: '83 f9 08',             # reply high nibble0..8
        0x92185: '83 7d c8 00',          # command0 read, otherwise ACK-only write
        0x9219a: '89 03',
        0x92221: '89 43 10',             # ALWAYS all five reply dwords on read ACK
        0x922a5: '41 bf f0 02 00 e0',    # native NACK
        0x9230d: '41 bf d8 02 00 e0',    # native DEFER
        0x9248d: '45 31 ff',             # success status zero, not byte count
        0x28f3c: '31 f6',                # high-level read command0
        0x28f41: 'b9 04 00 00 00',       # native read sends4 bytes
        0x28f49: 'ff 90 88 01 00 00',    # original vptr slot0x188
        0x29025: '48 8d 75 b1 8b 55 a0', # copy requested count, no actual length
        0x29369: 'be 01 00 00 00',       # high-level write command1
        0x29375: 'ff 93 88 01 00 00',
    }
    for address, expected in checks.items():
        raw = bytes.fromhex(expected)
        require(at(address, len(raw)) == raw, f'AUX opcode mismatch {address:#x}')
    targets = [0x925e4 + struct.unpack('<i', at(0x925e4 + 4*i, 4))[0] for i in range(9)]
    require(targets == [0x92180,0x9228f,0x922f7,0x92353,0x923ae,0x92353,0x92353,0x92353,0x92416],
            'AUX response jump table mismatch')
    vtable = symbol(data, '__ZTV17AppleIntelPortHAL')
    require(struct.unpack('<Q', at(vtable + 16 + 0x188, 8))[0] == 0x91b64, 'AUX vptr slot mismatch')
    refs = ROOT / 'cross-build/linux-v6.12-display-gt-reference-20261024'
    hashes = {
        'display__intel_dp_aux.c': 'a60058454ec869c4961a1a059b1f3d23999b15b46f304718c45ba8d85f45b4dd',
        'display__intel_dp_aux_regs.h': '694c67360055fba5668baff645f0d624645bd17aeb143d976d2e11b2fc3ce37d',
    }
    for filename, digest in hashes.items():
        require(hashlib.sha256((refs / filename).read_bytes()).hexdigest() == digest, filename)
    return dict(byte_checks=len(checks), framebuffer_sha256=hashlib.sha256(data).hexdigest(),
        original_body_sha256='9fce5ef02571c73e7e6bc665479eebb03e0f1a8a53891e761aaf812c8d2a8156',
        original='0x91b64', object_vptr_slot='0x188', original_read_command=0, original_write_command=1,
        native_reply_targets=[hex(t) for t in targets],
        receive_size_field={'register': '0x64510', 'mask': '0x1f00000', 'shift': 20},
        original_validates_receive_size=False, original_returns_actual_count=False,
        original_clears_completion_after_transaction=False,
        linux_reference_sha256=hashes,
        findings=[
            'runAUXCommand uses bits24:20 only for transmit size; response path checks timeout/done/receive-error, not response length.',
            'Native ACK0 on command0 copies all20 scratch bytes, even if the sink returned fewer. High-level readAUX then copies requested count.',
            'Native ACK0 on command1 skips response-buffer copying. A two-byte short-write reply must be read from actual DATA registers.',
            'Reply high-nibble1 maps to0xe00002f0;2 maps to0xe00002d8. I2C reply4/8 are separate paths, not native DPCD ACKs.',
            'Completion AUX_CTL is not cleared after polling. Recover count while the SAME outer recursive lock prevents the next AUX command.',
            'The original recursively takes controller+0xc30 and can poll for~4.2ms. Never hold it across training waits or retry delays.',
            'A read of AUX_CTL after an unlocked original call is racy and cannot establish this command\'s count.',
            'Original does not acquire core/DC6/AUX holds; validated outer native lifetime/power/admission and typed MMIO integration remain required.',
        ], native_backend_enabled=False, runtime_validated=False, device_access=False, external_writes=False)


def main():
    report = audit()
    OUT.mkdir(exist_ok=True)
    (OUT / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    for label, start, end in [('run-aux', 0x91b64, 0x92608), ('read-write-aux', 0x28d9e, 0x29470)]:
        text = subprocess.check_output(['llvm-objdump','-d','--x86-asm-syntax=intel',
            f'--start-address={start:#x}',f'--stop-address={end:#x}',str(FB)], text=True)
        (OUT / (label + '.disasm.txt')).write_text(text)
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
