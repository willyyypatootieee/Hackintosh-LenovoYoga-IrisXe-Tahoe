"""Read existing crash/system logs from the verified external Data volume."""
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
out = ROOT / ('display-failures-readonly-' + datetime.datetime.now().strftime('%Y%m%d-%H%M%S'))
out.mkdir()
mount = out / 'data-ro'
mount.mkdir()
report = dict(external_writes=False, files=[], inventory=[])
mounted = False
try:
    run('apfs-fuse', '-v', '0', '-o', 'ro', '/dev/sda3', str(mount))
    mounted = True
    base = mount / 'root'
    roots = [base / 'Library/Logs/DiagnosticReports', base / 'private/var/log', base / 'private/var/db/PanicReporter']
    users = base / 'Users'
    if users.is_dir():
        roots.extend(p / 'Library/Logs/DiagnosticReports' for p in users.iterdir() if p.is_dir())
    for search in roots:
        if not search.is_dir():
            continue
        for folder, dirs, files in os.walk(search):
            dirs[:] = [d for d in dirs if not (Path(folder) / d).is_symlink()]
            if len(Path(folder).relative_to(search).parts) >= 5:
                dirs[:] = []
            for filename in sorted(files):
                p = Path(folder) / filename
                if p.is_symlink():
                    continue
                stat = p.stat()
                rel = str(p.relative_to(base))
                report['inventory'].append(dict(path=rel, bytes=stat.st_size, modified=stat.st_mtime))
                selected = (search.name in ('DiagnosticReports', 'PanicReporter') or filename.startswith(('system.log', 'kernel.log', 'panic', 'WindowServer')))
                if selected and stat.st_size <= 30000000:
                    dest = out / 'copies' / rel
                    dest.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copyfile(p, dest)
                    report['files'].append(rel)
    # Inventory persisted unified-log stores, without interpreting compressed data as text.
    for rel in ['private/var/db/diagnostics', 'private/var/db/uuidtext']:
        search = base / rel
        if search.is_dir():
            for folder, dirs, files in os.walk(search):
                if len(Path(folder).relative_to(search).parts) >= 2:
                    dirs[:] = []
                for filename in files:
                    p = Path(folder) / filename
                    if p.is_file() and not p.is_symlink():
                        st = p.stat()
                        report['inventory'].append(dict(path=str(p.relative_to(base)), bytes=st.st_size, modified=st.st_mtime))
finally:
    if mounted:
        run('umount', str(mount))
    (out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    for p in [out, *out.rglob('*')]:
        if not p.is_symlink():
            os.chown(p, 1000, 1000)
print(json.dumps(dict(evidence=str(out), copied=report['files'], inventory_entries=len(report['inventory']), external_writes=False), indent=2))
