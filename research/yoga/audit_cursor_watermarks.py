"""Offline exact-binary cursor WM evidence. No patching or device access."""
import hashlib
import json
import subprocess
from pathlib import Path
from audit_cursor_diag_inputs import FB, image
from yoga_fb_match_patch import symbol

ROOT = Path(__file__).resolve().parent
OUT = ROOT / 'cross-build/cursor-watermark-audit-20261024'
LINUX = ROOT / 'cross-build/linux-v6.12-display-gt-reference-20261024/display__skl_watermark.c'


def main():
    if not __debug__:
        raise RuntimeError('Do not run with Python optimization')
    data, at = image(FB)
    assert hashlib.sha256(data).hexdigest() == '3f2bb3336507242ddc3f69c5d7645d19ce1cd4d8748bdb4b4c8858c4b49103df'
    funcs = {
        '__ZN15AppleIntelPlane21setupCursorWatermarksEP21AppleIntelDisplayPath': 0x795a,
        '__ZN15AppleIntelPlane22enableCursorWatermarksEb': 0x7cea,
        '__ZN15AppleIntelPlane21computeWatermarkValueEP21AppleIntelDisplayPathyjjjjjj': 0x75c8,
        '__ZN15AppleIntelPlane20computeBlocksPerLineEjjjj': 0x78a4,
    }
    for name, address in funcs.items():
        assert symbol(data, name) == address
    checks = {
        0x7979: '44 8b b6 78 42 00 00', # same cursor dimension field used in image/scale path
        0x7aba: '44 69 84 98 68 19 00 00 e8 03 00 00', # latency table *1000
        0x7ad3: 'b9 04 00 00 00 45 89 f1', # cpp 4, cursor width
        0x7adb: '68 00 02 00 00 6a 28 6a 00', # block 512, allocation argument 40, linear
        0x7aed: '41 89 84 9d a4 01 00 00', # shadow WM[level]
        0x7af8: '48 83 fb 08', # eight levels
        0x7b25: 'ba 28 00 00 00', # transition-WM allocation argument 40
        0x77d7: '44 39 fb', # reject computed blocks >= allocation argument
        0x77dc: '41 83 fe 20', # reject lines >=32
        0x787d: '81 e3 ff 07 00 00 41 83 e6 1f 41 c1 e6 0e', # fields: blocks/lines
        0x7d57: '48 8b 80 d0 01 00 00',
        0x7d5e: '80 b8 13 1b 00 00 00', # special branch: not always all eight WMs
        0x7df4: 'ff 90 30 01 00 00', # register-access virtual write
        0x7e3e: '41 81 c4 84 00 07 00', # read CURBASE for trigger packet
        0x7e61: 'e8 aa 2b 0e 00', # PrepareTriggerPacket
        0x7e950: 'e8 05 90 f8 ff', # setup WM
        0x7e99d: 'e8 48 93 f8 ff', # enable WM
    }
    for address, expected in checks.items():
        pattern = bytes.fromhex(expected)
        assert at(address, len(pattern)) == pattern, hex(address)
    linux = LINUX.read_bytes()
    assert b'wp->dbuf_block_size = 512;' in linux
    assert b'return max(num_active == 1 ? 32 : 8, min_ddb_alloc);' in linux
    OUT.mkdir(exist_ok=True)
    text = subprocess.check_output(['llvm-objdump', '-d', '--x86-asm-syntax=intel',
        '--start-address=0x74c8', '--stop-address=0x7e94', str(FB)], text=True)
    (OUT / 'cursor-watermarks.disasm.txt').write_text(text)
    report = dict(
        framebuffer_sha256=hashlib.sha256(data).hexdigest(),
        linux_reference='Linux v6.12 drivers/gpu/drm/i915/display/skl_watermark.c',
        linux_reference_sha256=hashlib.sha256(linux).hexdigest(),
        checked_symbols={name: hex(address) for name, address in funcs.items()},
        byte_checks=len(checks),
        apple_call_arguments={'bytes_per_pixel': 4, 'dbuf_block_bytes': 512,
                              'allocation_argument_blocks': 40, 'levels': 8},
        ordering='hwSetCursorState calls setupCursorWatermarks then enableCursorWatermarks before CURCNTR/CURBASE programming. enableCursorWatermarks also prepares a CURBASE trigger packet.',
        limits=[
            'The constant 40 is an arithmetic/allocation-limit argument, NOT a readback of actual CUR_BUF_CFG allocation.',
            'Apple 512-byte linear cursor DBUF block size agrees with the Linux reference; changing it is not justified.',
            'Linux cursor DDB allocation is computed from worst-case 256px cursor and latency, with 32/8-block minimum depending on active CRTCs. Its difference from Apple 40 does not establish a Yoga bug.',
            'Apple has a controller+0x1b13 branch that writes level 0 and clears higher levels; do not assume all eight levels are enabled.',
            'Live DDB ranges, pixel data, PTE/coherency and trigger-latch timing remain unverified.',
        ], new_patch_selected=False, runtime_validated=False,
        device_access=False, external_writes=False)
    (OUT / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
