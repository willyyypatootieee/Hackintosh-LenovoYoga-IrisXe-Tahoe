"""Make searchable text while preserving parser failures and literal log strings."""
from pathlib import Path
from collections import defaultdict
import json
import re

ROOT = Path(__file__).resolve().parent
OUT = ROOT / 'latest-unified-readonly-20261023-233849'
groups = defaultdict(list)
for line in (OUT / 'parsed.jsonl').open():
    row = json.loads(line)
    message = row['message']
    if message.startswith('Failed to get string message'):
        strings = [x['message_strings'] for x in row['message_entries'] if x['item'] == 'String']
        message = '[UNRESOLVED FORMAT; LITERAL ARGUMENTS] ' + ' | '.join(strings)
    groups[row['boot_uuid']].append((row['time'], row, message))
summary = []
for boot, entries in groups.items():
    entries.sort(key=lambda item: item[0])
    text = ''.join(f"{row['timestamp']} pid={row['pid']} tid={row['thread_id']} {row['process']} {message.rstrip()}\n" for _, row, message in entries)
    (OUT / (boot + '.txt')).write_text(text)
    selected = ''.join(f"{row['timestamp']} pid={row['pid']} tid={row['thread_id']} {message.rstrip()}\n" for _, row, message in entries
                       if re.search(r'XeNG|NootedGreen|IOFB|IntelAccelerator|IGAccel|stolen|GMS|cs_blob|code signature page|WindowServer|GPU Panic', message, re.I))
    (OUT / (boot + '-gpu.txt')).write_text(selected)
    summary.append(dict(boot_uuid=boot, rows=len(entries), first=entries[0][1]['timestamp'], last=entries[-1][1]['timestamp'],
                        signature_rejections=sum('unsupported code signature page shift' in msg for _, _, msg in entries)))
(OUT / 'boot-summary.json').write_text(json.dumps(summary, indent=2) + '\n')
print(json.dumps(summary, indent=2))
