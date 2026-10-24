"""Authorized erase of ONLY the empty external Tahoe target partition.

Run through pkexec. No internal-disk paths or whole-disk formatting commands.
"""
from pathlib import Path
import datetime
import hashlib
import json
import os
import subprocess

DISK = '/dev/sda'
TARGET = '/dev/sda3'
PARTUUID = '7bb5254d-220e-4792-9421-1682c9f75718'
APFS_TYPE = '7c3457ef-0000-11aa-aa11-00306543ecac'
ROOT = Path(__file__).resolve().parent


def run(*args):
    return subprocess.check_output(args, text=True, stderr=subprocess.STDOUT)


assert os.geteuid() == 0, 'Administrator authentication required'
tree = json.loads(run('lsblk', '-b', '-J', '-o',
    'NAME,PATH,SIZE,MODEL,SERIAL,TRAN,FSTYPE,MOUNTPOINTS,PARTUUID', DISK))['blockdevices'][0]
assert tree['path'] == DISK and tree['tran'] == 'usb'
assert tree['model'].strip() == 'SAMSUNG MZVLB256HAHQ-000L7'
assert tree['serial'] == 'REDACTED_EXTERNAL_SERIAL'
assert tree['size'] == 256060514304
parts = tree['children']
assert [p['path'] for p in parts] == ['/dev/sda1', '/dev/sda2', TARGET]
assert [p['size'] for p in parts] == [1073741824, 31138512896, 223847194112]
assert all(not any(p.get('mountpoints') or []) for p in parts)
target = parts[2]
assert target['partuuid'] == PARTUUID and target['fstype'] == 'apfs'
before_table = json.loads(run('sfdisk', '--json', DISK))['partitiontable']
assert before_table['label'] == 'gpt' and before_table['sectorsize'] == 512
assert before_table['partitions'][2]['start'] * 512 == 32213303296
assert before_table['partitions'][2]['type'].lower() == '0fc63daf-8483-4772-8e79-3d69d8477de4'
inventory = run('apfsutil', TARGET)
assert inventory.count('Volume ') == 1
assert 'Tahoe macOS (Case-insensitive)' in inventory
assert 'Capacity Consumed:  20480 Bytes' in inventory
assert inventory.rstrip().endswith('Snapshots:'), 'Existing snapshots or unexpected data; stop'

stage = ROOT / ('external-target-format-' + datetime.datetime.now().strftime('%Y%m%d-%H%M%S'))
stage.mkdir()
(stage / 'partition-table.before.json').write_text(json.dumps(before_table, indent=2) + '\n')
(stage / 'partition-table.before.sfdisk').write_text(run('sfdisk', '--dump', DISK))
(stage / 'apfs.before.txt').write_text(inventory)
with open(DISK, 'rb', buffering=0) as f:
    first = f.read(1024 * 1024)
    f.seek(tree['size'] - 1024 * 1024)
    last = f.read(1024 * 1024)
(stage / 'disk-first-1MiB.before.bin').write_bytes(first)
(stage / 'disk-last-1MiB.before.bin').write_bytes(last)

# Metadata fingerprints of protected partitions, before and after. These are
# bounded read-only checks, not a claim to have hashed the full installer.
def protected_fingerprints():
    result = {}
    for part in parts[:2]:
        with open(part['path'], 'rb', buffering=0) as f:
            head = f.read(1024 * 1024)
            f.seek(part['size'] - 1024 * 1024)
            tail = f.read(1024 * 1024)
        result[part['path']] = hashlib.sha256(head + tail).hexdigest()
    return result

protected = protected_fingerprints()
(stage / 'protected-partition-fingerprints.before.json').write_text(json.dumps(protected, indent=2) + '\n')
print('Verified empty external target; formatting ONLY /dev/sda3.', flush=True)
(stage / 'mkfs.txt').write_text(run('mkfs.apfs', '-L', 'Tahoe macOS', TARGET))
(stage / 'partition-type-update.txt').write_text(run('sfdisk', '--part-type', DISK, '3', APFS_TYPE))
run('udevadm', 'settle')
after_table = json.loads(run('sfdisk', '--json', DISK))['partitiontable']
expected = json.loads(json.dumps(before_table))
expected['partitions'][2]['type'] = APFS_TYPE.upper()
assert json.dumps(expected, sort_keys=True).lower() == json.dumps(after_table, sort_keys=True).lower()
assert protected_fingerprints() == protected
(stage / 'apfsck.txt').write_text(run('apfsck', TARGET))
after_inventory = run('apfsutil', TARGET)
assert 'Tahoe macOS (Case-insensitive)' in after_inventory
(stage / 'apfs.after.txt').write_text(after_inventory)
(stage / 'partition-table.after.json').write_text(json.dumps(after_table, indent=2) + '\n')
report = dict(result='Formatted external partition 3 as case-insensitive APFS',
              target=TARGET, partuuid=PARTUUID, size_bytes=target['size'],
              label='Tahoe macOS', gpt_type=APFS_TYPE,
              apfsck='passed; experimental Linux checker, not Apple certification',
              formatter='apfsprogs 0.2.1 (experimental)',
              protected_partitions='sda1/sda2 entries and sampled metadata unchanged',
              internal_disk_writes=False, evidence=str(stage))
(stage / 'verification.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report, indent=2))
print(after_inventory)
