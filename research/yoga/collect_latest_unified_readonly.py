"""Copy already-persisted boot evidence; never writes external APFS."""
from pathlib import Path
import datetime
import json
import os
import shutil
import subprocess

ROOT = Path(__file__).resolve().parent
def run(*args):
    return subprocess.check_output(args, text=True, stderr=subprocess.STDOUT)

assert os.geteuid() == 0
disk = json.loads(run('lsblk', '-b', '-J', '-o', 'NAME,SERIAL,TRAN,SIZE,PARTUUID,MOUNTPOINTS', '/dev/sda'))['blockdevices'][0]
assert disk['serial'] == 'REDACTED_EXTERNAL_SERIAL' and disk['tran'] == 'usb' and disk['size'] == 256060514304
assert disk['children'][2]['partuuid'] == '7bb5254d-220e-4792-9421-1682c9f75718'
assert all(not any(x['mountpoints'] or []) for x in disk['children'])
out = ROOT / ('latest-unified-readonly-' + datetime.datetime.now().strftime('%Y%m%d-%H%M%S'))
out.mkdir()
mount = out / 'data-ro'
mount.mkdir()
archive = out / 'boot.logarchive'
archive.mkdir()
report = dict(external_writes=False, files=[])
mounted = False
try:
    run('apfs-fuse', '-v', '0', '-o', 'ro', '/dev/sda3', str(mount))
    mounted = True
    base = mount / 'root/private/var/db'
    cutoff = datetime.datetime(2026, 10, 23, 22, 45).timestamp()
    for source in [base / 'uuidtext', base / 'diagnostics']:
        for folder, dirs, files in os.walk(source):
            dirs[:] = [d for d in dirs if not (Path(folder) / d).is_symlink()]
            for name in files:
                p = Path(folder) / name
                if p.is_symlink() or not p.is_file():
                    continue
                stat = p.stat()
                if source.name == 'diagnostics' and p.suffix == '.tracev3' and stat.st_mtime < cutoff:
                    continue
                rel = p.relative_to(source)
                dest = archive / rel
                assert not dest.exists()
                dest.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(p, dest)
                report['files'].append(dict(source=str(p.relative_to(base)), archive_path=str(rel), bytes=stat.st_size))
finally:
    if mounted:
        run('umount', str(mount))
    (out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    for p in [out, *out.rglob('*')]:
        if not p.is_symlink():
            os.chown(p, 1000, 1000)
print(json.dumps(dict(evidence=str(out), files=len(report['files']), bytes=sum(p['bytes'] for p in report['files']), external_writes=False), indent=2))
