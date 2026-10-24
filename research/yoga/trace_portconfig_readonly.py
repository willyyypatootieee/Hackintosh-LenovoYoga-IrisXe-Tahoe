"""Decode captured Mach-O data only; never opens devices or mounts volumes."""
from pathlib import Path
import hashlib
import json
import re
import struct
import subprocess

ROOT = Path(__file__).resolve().parent
BINARY = ROOT / 'installed-signatures-readonly-20261023-233323/copies/data/Library/Extensions/AppleIntelTGLGraphicsFramebuffer.kext/Contents/MacOS/AppleIntelTGLGraphicsFramebuffer'
OUTPUT = ROOT / 'cursor-typec-firmware-readonly-20261024-021325/portconfig-jumptable-analysis.txt'
data = BINARY.read_bytes()
assert hashlib.sha256(data).hexdigest() == '3f2bb3336507242ddc3f69c5d7645d19ce1cd4d8748bdb4b4c8858c4b49103df'
offset = 32
segments = []
for _ in range(struct.unpack_from('<I', data, 16)[0]):
    command, size = struct.unpack_from('<II', data, offset)
    if command == 0x19:
        name, address, length, file_offset, file_size = struct.unpack_from('<16sQQQQ', data, offset + 8)
        segments.append((address, file_offset, file_size))
    offset += size


def captured_bytes(address, size):
    for start, file_offset, file_size in segments:
        if start <= address and address + size <= start + file_size:
            return data[file_offset + address - start:file_offset + address - start + size]
    raise ValueError(hex(address))


lines = []
base = 0x8ed24
for index, relative in enumerate(struct.unpack('<9i', captured_bytes(base, 36))):
    address = base + relative
    lines.append(f'PortConfig first dword = {index}: case {address:#x}\n')
    lines.append(subprocess.check_output([
        'llvm-objdump', '-d', '--x86-asm-syntax=intel',
        f'--start-address={address}', f'--stop-address={address + 100}', str(BINARY)
    ], text=True))
OUTPUT.write_text('\n'.join(lines))
print(OUTPUT)

# gPlatformInformationList is zero-fill, initialized by the constructor at
# 0xbaf00. Its memcpy sources, not the zero-fill address, contain the tables.
for label, address in [('fallback', 0xf35b0), ('9a490000', 0xf3d48)]:
    lines.append(f'\nStock {label} PortConfig memcpy source at {address:#x}:\n')
    table = captured_bytes(address, 9 * 24)
    for port in range(9):
        fields = struct.unpack_from('<6I', table, port * 24)
        lines.append(f'  Port {port}: ' + ' '.join(f'{v:08x}' for v in fields))
OUTPUT.write_text('\n'.join(lines))

stock = [list(struct.unpack_from('<6I', captured_bytes(0xf3d48, 216), i * 24)) for i in range(9)]
assert stock[3] == [3, 9, 1, 1, 0x400, 0x201]
assert stock[5] == [5, 11, 1, 1, 0x400, 0x201]
assert struct.unpack('<9i', captured_bytes(base, 36))[5] + base == 0x8e9bb
assert captured_bytes(0x8e9e4, 10) == bytes.fromhex('48 b8 00 45 06 00 10 45 06 00')
assert captured_bytes(0x8faff, 4) == bytes.fromhex('83 78 0c 01')
assert captured_bytes(0x8fb76, 5) == bytes.fromhex('be a0 38 16 00')
assert captured_bytes(0x8fbe0, 3) == bytes.fromhex('c1 e8 10')
# Decode readiness/ownership switch tables for DDI 5 (TC3), not just DDI init.
ready_case = 0x8fab8 + struct.unpack('<4i', captured_bytes(0x8fab8, 16))[2]
owned_case = 0x8fac8 + struct.unpack('<6i', captured_bytes(0x8fac8, 24))[2]
assert ready_case == 0x8f772 and owned_case == 0x8f97f
assert captured_bytes(0x8f6e5, 5) == bytes.fromhex('be 90 38 16 00')
assert captured_bytes(ready_case + 7, 3) == bytes.fromhex('c0 e8 02')
assert captured_bytes(owned_case + 7, 4) == bytes.fromhex('41 83 ce 04')
assert captured_bytes(0x8f9bb, 5) == bytes.fromhex('be 94 38 16 00')

deployed = ROOT / 'postmatch-efi-nvram-readonly-20261024-021349/copies/external-esp/EFI/OC/Kexts/NootedGreen.kext/Contents/MacOS/NootedGreen'
assert hashlib.sha256(deployed.read_bytes()).hexdigest() == 'e89bb36d1a23559b8d60194e79e620addf7b82f98e2e37d282bad0d82978dd44'
kernel_log = ROOT / 'latest-unified-readonly-20261024-021429/latest-kernel-recovered.txt'
cursor_register_lines = [line for line in kernel_log.read_text().splitlines() if 'CUR CTL=' in line]
display_log = ROOT / 'latest-unified-readonly-20261024-021429/latest-display-targeted.txt'
enable_lines = sorted(line for line in display_log.read_text().splitlines()
                      if '/WindowServer Set cursor enable:' in line)
post_init_sample = next(line for line in cursor_register_lines if 'V205[c=30]:' in line)
sample_time = post_init_sample.split()[0]
before = max(line for line in enable_lines if line.split()[0] < sample_time)
after = min(line for line in enable_lines if line.split()[0] > sample_time)
assert '2026-10-23T19:02:17.365918208Z' == sample_time
assert 'enable:NO' in before and 'enable:YES' in after
assert 'CUR CTL=00000000 BASE=03c40000 POS=000a000a' in post_init_sample


def time_ns(line):
    match = re.match(r'.*T(\d+):(\d+):(\d+)\.(\d+)Z', line.split()[0])
    hour, minute, second = map(int, match.groups()[:3])
    return ((hour * 60 + minute) * 60 + second) * 10**9 + int(match[4].ljust(9, '0'))


report = {
    'scope': 'offline captured-file analysis only; no deployment',
    'framebuffer_sha256': hashlib.sha256(data).hexdigest(),
    'deployed_ngreen_sha256': hashlib.sha256(deployed.read_bytes()).hexdigest(),
    'stock_portconfig_rows': stock,
    'apple_tc3_ddi_value': 5,
    'apple_tc3_ddi_buf_ctl': '0x64500',
    'apple_tc3_aux_ctl': '0x64510',
    'apple_tc3_dekel_aperture_base': '0x16a000',
    'stock_tc3_row_not_a_deployment_recommendation': stock[5],
    'typec_gate': 'PortConfig +0x0c must equal 1; deployed external entry has 0',
    'apple_tc3_legacy_fia_path': {
        'lane_mask': '(read(0x1638a0) >> 16) & 0xf',
        'readiness': 'read(0x163890) bit 2; case 0x8f772',
        'ownership': 'read/modify/write 0x163894 bit 2; case 0x8f97f',
        'comparison': 'Linux v6.12 ADL-P uses modular FIA (TC3: instance index 1, slot 0), TCSS_DDI_STATUS readiness, DDI_BUF_CTL bit 6 ownership. This is not repaired by copying the stock TC3 PortConfig row.',
    },
    'remaining_typec_gates': [
        'Apple TGL FIA layout versus ADL-P modular FIA and TCSS ownership',
        'DDI/AUX power and interrupt handling',
        'framebuffer-to-port allocation with preserved eDP and three-path baseline',
        'native macOS hotplug, AUX and link-training validation',
    ],
    'latest_boot_cursor_register_samples': cursor_register_lines,
    'post_init_cursor_timeline': {
        'first_windowserver_enable_event': enable_lines[0],
        'preceding_enable_event': before,
        'pipe_a_sample': post_init_sample,
        'following_enable_event': after,
        'ms_after_disable': (time_ns(post_init_sample) - time_ns(before)) / 1e6,
        'ms_before_enable': (time_ns(after) - time_ns(post_init_sample)) / 1e6,
    },
    'cursor_limitation': (
        'V205[c=30] is after WindowServer cursor initialization, but lies between '
        'WindowServer enable:NO and enable:YES events. Pipe-A CTL=0 is consistent '
        'with that temporary disable; it does not prove persistent plane failure, '
        'software cursor fallback, another active pipe, or corrupt sprite/base/stride. '
        'No register sample is synchronized to the user-visible corrupted pointer.'
    ),
}
out = OUTPUT.with_name('portconfig-readonly-audit.json')
out.write_text(json.dumps(report, indent=2) + '\n')
print(out)
