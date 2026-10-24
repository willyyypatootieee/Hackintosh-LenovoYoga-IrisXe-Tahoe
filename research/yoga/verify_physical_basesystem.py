"""Compare only external installer BaseSystem with downloaded source; read-only."""
from pathlib import Path
import hashlib
import json
import subprocess
import inspect_source_hfs as hfs

ROOT = Path(__file__).resolve().parent
device = Path('/dev/disk/by-partuuid/e1886d74-dc4f-4429-8482-73a90ae91296')
assert device.resolve() == Path('/dev/sda2')
info = json.loads(subprocess.check_output(['lsblk', '-b', '-J', '-o', 'PATH,SIZE,MODEL,TRAN', '/dev/sda'], text=True))['blockdevices'][0]
assert info['tran'] == 'usb' and info['model'].strip() == 'SAMSUNG MZVLB256HAHQ-000L7'
hfs.IMAGE = device
hfs.OFFSET = 0
volume = hfs.Volume()
entry = next(e for e in volume.entries() if e['path'] == 'Install macOS Tahoe/BaseSystem/BaseSystem.dmg')
digest = hashlib.sha256()
for offset in range(0, entry['fork'][0], 8 * 1024 * 1024):
    digest.update(volume.read(entry['fork'], offset, 8 * 1024 * 1024))
volume.file.close()
hfs.IMAGE = Path('/home/willy/Downloads/Olarila Tahoe 26/Olarila Tahoe 26.7.1.raw')
hfs.OFFSET = 209735680
source = hfs.Volume()
source_entry = next(e for e in source.entries() if e['path'] == entry['path'])
assert source_entry['fork'][0] == entry['fork'][0]
source_hash = hashlib.sha256()
for offset in range(0, source_entry['fork'][0], 8 * 1024 * 1024):
    source_hash.update(source.read(source_entry['fork'], offset, 8 * 1024 * 1024))
source_digest = source_hash.hexdigest()
report = dict(device=str(device.resolve()), bytes=entry['fork'][0],
              physical_sha256=digest.hexdigest(), source_sha256=source_digest,
              matches=digest.hexdigest() == source_digest, writes=False)
(ROOT / 'physical-basesystem-verification.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report, indent=2))
assert report['matches'], 'Physical installer differs from source; no repair attempted'
