"""Offline audit of captured drivers. Never opens a device or changes EFI."""
import hashlib
import json
import struct
import subprocess
from pathlib import Path

from audit_cursor_diag_inputs import FB, image

ROOT = Path(__file__).resolve().parent
HOOK = ROOT / 'installed-signatures-readonly-20261023-233323/copies/data/Library/Extensions/HookCase.kext/Contents/MacOS/HookCase'
OUT = ROOT / 'cross-build/cursor-mapping-typec-audit-20261024'


def main():
    if not __debug__:
        raise RuntimeError('Do not run with Python optimization')
    fb, at = image(FB)
    hook, hat = image(HOOK)
    assert hashlib.sha256(fb).hexdigest() == '3f2bb3336507242ddc3f69c5d7645d19ce1cd4d8748bdb4b4c8858c4b49103df'
    assert hashlib.sha256(hook).hexdigest() == 'bc81298766f1ba4e6631c0131a5654854babbc1e3d2cd8bbf1300d780c65275b'
    checks = {
        0x5fa81: '41 8b b6 c8 0d 00 00',  # allocation length = cursor length
        0x5fa92: 'bf 90 00 00 00',
        0x5fa9c: '49 89 86 d0 0c 00 00',  # save descriptor
        0x5fad2: '49 bd 00 f0 ff ff 7f 00 00 00',
        0x5fae9: '48 83 c9 07',  # physical page | PTE flags
        0x5faf4: '49 8b 96 c0 0d 00 00',  # GGTT virtual base
        0x5fafb: '48 01 da 48 c1 ea 09',  # (base+offset)/4096 * 8
        0x5fb0d: '49 01 c4 48 01 c3',  # advance physical and virtual page
    }
    for address, expected in checks.items():
        expected = bytes.fromhex(expected)
        assert at(address, len(expected)) == expected, hex(address)
    hook_checks = {
        0x1c0f9: 'be a0 38 16 00',
        0x1c21d: '25 00 00 00 40',
        0x1c233: '25 00 00 00 20',
    }
    for address, expected in hook_checks.items():
        expected = bytes.fromhex(expected)
        assert hat(address, len(expected)) == expected, hex(address)
    jump_base = 0x1c25c
    jump_targets = [jump_base + struct.unpack('<i', hat(jump_base + 4 * i, 4))[0]
                    for i in range(6)]
    assert jump_targets == [0x1c148, 0x1c148, 0x1c154, 0x1c190, 0x1c1d0, 0x1c20d]
    OUT.mkdir(exist_ok=True)
    for label, binary, start, stop in [
        ('cursor-system-pages', FB, 0x5f9b0, 0x5fb7a),
        ('cursor-aperture-map', FB, 0x5c650, 0x5c850),
        ('installed-hookcase-probe', HOOK, 0x1c0d0, 0x1c310),
    ]:
        text = subprocess.check_output([
            'llvm-objdump', '-d', '--x86-asm-syntax=intel',
            f'--start-address={start:#x}', f'--stop-address={stop:#x}', str(binary)
        ], text=True)
        (OUT / f'{label}.txt').write_text(text)
    report = {
        'framebuffer_sha256': hashlib.sha256(fb).hexdigest(),
        'hookcase_sha256': hashlib.sha256(hook).hexdigest(),
        'byte_checks': len(checks),
        'hookcase_byte_checks': len(hook_checks),
        'hookcase_jump_targets': [hex(a) for a in jump_targets],
        'ddi5_probe': 'Installed HookCase reads global 0x1638a0 bits 30/29; confirmed by byte checks and decoded jump table, not inferred from source or backup.',
        'cursor_mapping': 'Separately allocated descriptor pages are mapped into GGTT starting at controller+0xdc0. GGTT address above stolen size alone is NOT evidence of physical stolen-memory overflow.',
        'runtime_pte_contents_verified': False,
        'runtime_cursor_trial_verified': False,
        'device_access': False,
        'external_writes': False,
    }
    (OUT / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
