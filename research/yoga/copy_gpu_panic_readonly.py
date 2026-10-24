"""Copy recent panic and OpenCore evidence from verified external disk, read-only."""
from pathlib import Path
import datetime
import json
import os
import subprocess

ROOT = Path(__file__).resolve().parent
assert os.geteuid() == 0
def run(*args):
    return subprocess.check_output(args, text=True, stderr=subprocess.STDOUT)
d = json.loads(run('lsblk','-b','-J','-o','NAME,SERIAL,TRAN,PARTUUID','/dev/sda'))['blockdevices'][0]
assert d['serial'] == 'REDACTED_EXTERNAL_SERIAL' and d['tran'] == 'usb'
assert d['children'][2]['partuuid'] == '7bb5254d-220e-4792-9421-1682c9f75718'
out = ROOT / ('gpu-panic-evidence-' + datetime.datetime.now().strftime('%Y%m%d-%H%M%S'))
out.mkdir()
copied = []
for name in ['data', 'esp']:
    mount = out / (name + '-ro')
    mount.mkdir()
    mounted = False
    try:
        if name == 'data':
            run('apfs-fuse','-v','0','-o','ro','/dev/sda3',str(mount))
            base = mount / 'root'
        else:
            run('mount','-t','vfat','-o','ro,nosuid,nodev,noexec','/dev/sda1',str(mount))
            base = mount
        mounted = True
        if name == 'data':
            roots = ['Library/Logs/DiagnosticReports', 'private/var/db/PanicReporter', 'private/var/log']
            paths = []
            for rel in roots:
                directory = base / rel
                if directory.is_dir():
                    for p in directory.rglob('*'):
                        if p.is_file() and p.stat().st_size < 16*1024*1024 and any(t in p.name.lower() for t in ['panic','gpu','kernel']):
                            paths.append(p)
        else:
            paths = [p for p in base.iterdir() if p.is_file() and p.suffix.lower() in ['.txt','.log','.panic']]
            nvram = base / 'nvram.plist'
            if nvram.is_file(): paths.append(nvram)
        for p in paths:
            dest = out / 'copies' / name / p.relative_to(base)
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_bytes(p.read_bytes())
            copied.append({'path':str(dest.relative_to(out)), 'bytes':dest.stat().st_size})
    finally:
        if mounted: run('umount',str(mount))
(out/'report.json').write_text(json.dumps(copied,indent=2)+'\n')
for p in [out,*out.rglob('*')]: os.chown(p,1000,1000)
print(json.dumps({'directory':str(out),'files':copied},indent=2))
