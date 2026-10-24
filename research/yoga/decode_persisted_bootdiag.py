"""Recover BOOTDIAG numeric arguments using formats in the exact active binary.

The third-party log parser failed to resolve these absolute format addresses;
its parsed message_entries still retain the typed string and numeric arguments.
"""
from pathlib import Path
from collections import defaultdict
import json
from yoga_fb_match_patch import ROOT, NG, NG_HASH, sha

binary = NG.read_bytes()
assert sha(binary) == NG_HASH
formats = [s.decode() for s in binary.split(b'\0') if s.startswith(b'XeNG BOOTDIAG %s ')]
assert any('PLANE_A CTL=' in s for s in formats)
fields = {
    9: ['uptime_seconds', 'uptime_milliseconds', 'console_valid', 'console_base',
        'row_bytes', 'width', 'height', 'depth', 'display'],
    10: ['PLANE_CTL', 'PLANE_STRIDE', 'PLANE_POS', 'PLANE_SIZE', 'PLANE_SURF',
         'PLANE_SURFLIVE', 'PLANE_OFFSET', 'PIPE_SRCSZ', 'CUR_CTL', 'CUR_BASE'],
    12: ['FBC_CTL', 'FBC_CFB', 'FBC_CHICKEN', 'FBC_STATUS0', 'FBC_STATUS1',
         'FBC_PAR1', 'REG_4208C', 'PSR', 'PSR2', 'DC', 'CDCLK_CTL', 'DE_PLL'],
    8: ['boothold_blc0', 'boothold_min_ms', 'boothold_max_ms', 'boothold_on',
        'boothold_holding', 'boothold_done', 'boothold_ctl', 'boothold_first'],
}
boots = defaultdict(list)
for line in (ROOT / 'latest-unified-readonly-20261023-233849/parsed.jsonl').open():
    row = json.loads(line)
    entries = row['message_entries']
    if not entries or entries[0]['item'] != 'String' or entries[0]['message_strings'] != 'post-FBstart':
        continue
    nums = entries[1:]
    assert len(nums) in fields and all(e['item'] == 'Number' for e in nums)
    values = {name: int(e['message_strings']) & ((1 << (e['item_type_size'] * 8)) - 1)
              for name, e in zip(fields[len(nums)], nums)}
    boots[row['boot_uuid']].append(dict(timestamp=row['timestamp'], tag='post-FBstart',
                                      values=values, hex_values={k: hex(v) for k, v in values.items()}))
out = ROOT / 'cross-build/task-startup-analysis/bootdiag-reconstructed.json'
out.write_text(json.dumps(dict(exact_binary_sha256=NG_HASH, format_strings=formats, boots=boots), indent=2) + '\n')
print(out)
