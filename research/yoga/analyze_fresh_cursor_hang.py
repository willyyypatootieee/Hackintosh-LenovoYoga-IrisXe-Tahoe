#!/usr/bin/env python3
"""Stream copied unified logs; retain literal arguments when format text is absent.

Only reads local evidence. Never mounts a disk or changes the EFI.
"""
import collections
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parent
OUT = ROOT / 'latest-unified-readonly-20261024-032122'
PATTERN = re.compile(
    r'XeNG|xedrv|NootedGreen|YogaGMS|GMSFix|IOFB|IntelAccelerator|IGAccel|'
    r'GPU (?:Panic|Hang|Reset)|hangcheck|GPURecovery|gpuRestart|'
    r'cursor|watchdog|boot.?arg|stolen|TC[1234]|Type.?C|DPCD|AUX.*(?:fail|error)', re.I)


def message(row):
    msg = row.get('message', '')
    strings = [str(x.get('message_strings', ''))
               for x in row.get('message_entries', []) if x.get('item') == 'String']
    if not msg or msg.startswith('Failed to get string message'):
        return '[UNRESOLVED FORMAT; LITERAL ARGUMENTS] ' + ' | '.join(strings)
    # Preserve arguments even if an incomplete parser message omitted them.
    missing = [s for s in strings if s not in msg]
    return msg + (' [LITERAL ARGUMENTS] ' + ' | '.join(missing) if missing else '')


def main():
    boots = {}
    selected = collections.defaultdict(list)
    with (OUT / 'parsed.jsonl').open() as source:
        for line in source:
            row = json.loads(line)
            boot = row['boot_uuid']
            timestamp = row['timestamp']
            stat = boots.setdefault(boot, dict(boot_uuid=boot, rows=0, first=timestamp,
                                              last=timestamp, recovered_rows=0))
            stat['rows'] += 1
            stat['first'] = min(stat['first'], timestamp)
            stat['last'] = max(stat['last'], timestamp)
            msg = message(row)
            stat['recovered_rows'] += msg.startswith('[UNRESOLVED FORMAT;')
            if PATTERN.search(msg) or 'WindowServer' in row.get('process', ''):
                selected[boot].append((row['time'], timestamp, row.get('pid'),
                                       row.get('thread_id'), row.get('process'), msg))
    summary = sorted(boots.values(), key=lambda x: x['last'])
    latest = summary[-1]['boot_uuid']
    for boot, entries in selected.items():
        if boot not in {s['boot_uuid'] for s in summary[-2:]}:
            continue
        prefix = 'fresh-latest' if boot == latest else 'fresh-previous'
        entries.sort(key=lambda x: x[0])
        with (OUT / (prefix + '-display-timeline.txt')).open('w') as dest:
            for _, ts, pid, tid, process, msg in entries:
                dest.write(f'{ts} pid={pid} tid={tid} {process} {msg.rstrip()}\n')
        with (OUT / (prefix + '-kernel-recovered.txt')).open('w') as dest:
            for _, ts, pid, tid, process, msg in entries:
                if pid == 0 or re.search(r'XeNG|NootedGreen|YogaGMS', msg):
                    dest.write(f'{ts} pid={pid} tid={tid} {msg.rstrip()}\n')
    result = dict(boots=summary, latest_boot_uuid=latest, external_writes=False)
    (OUT / 'fresh-boot-summary.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
