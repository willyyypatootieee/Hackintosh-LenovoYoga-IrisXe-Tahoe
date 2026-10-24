"""Read-only evidence of the working boot before quieting logging."""
from pathlib import Path
import datetime
import json
import os
import runpy
import shutil
import subprocess

ROOT = Path(__file__).resolve().parent
assert os.geteuid() == 0
os.chdir(ROOT)
runpy.run_path(str(ROOT / 'collect_postmatch_efi_nvram_readonly.py'), run_name='__main__')
runpy.run_path(str(ROOT / 'collect_latest_unified_readonly.py'), run_name='__main__')
out = ROOT / ('working-audio-display-readonly-' + datetime.datetime.now().strftime('%Y%m%d-%H%M%S'))
out.mkdir()
mount = out / 'data-ro'
mount.mkdir()
def run(*args): return subprocess.check_output(args, text=True, stderr=subprocess.STDOUT)
disk = json.loads(run('lsblk', '-b', '-J', '-o', 'NAME,SERIAL,TRAN,SIZE,PARTUUID,MOUNTPOINTS', '/dev/sda'))['blockdevices'][0]
assert disk['serial'] == 'REDACTED_EXTERNAL_SERIAL' and disk['tran'] == 'usb' and disk['size'] == 256060514304
parts = {p['name']: p for p in disk['children']}
assert parts['sda3']['partuuid'] == '7bb5254d-220e-4792-9421-1682c9f75718'
assert not any(any(p['mountpoints'] or []) for p in parts.values())
mounted = False
files = []
try:
    run('apfs-fuse', '-v', '0', '-o', 'ro', '/dev/sda3', str(mount))
    mounted = True
    base = mount / 'root'
    paths = []
    for folder in [base / 'Library/Preferences', *base.glob('Users/*/Library/Preferences')]:
        for pattern in ['com.apple.windowserver*', 'com.apple.audio*', 'Audio/*.plist', 'ByHost/com.apple.windowserver*']:
            paths.extend(folder.glob(pattern))
    for folder in [base / 'Library/Logs', base / 'private/var/log']:
        for pattern in ['*Lilu*', '*lilu*', '*Nooted*', 'system.log', 'system.log.0.gz', 'gpu*', 'Yoga*']:
            paths.extend(folder.glob(pattern))
    for p in paths:
        if p.is_file() and not p.is_symlink() and p.stat().st_size < 50000000:
            dest = out / 'copies' / p.relative_to(base)
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(p, dest)
            files.append(str(dest.relative_to(out)))
finally:
    if mounted: run('umount', str(mount))
    (out / 'report.json').write_text(json.dumps(dict(files=files, external_writes=False), indent=2) + '\n')
    for p in [out, *out.rglob('*')]: os.chown(p, 1000, 1000)
print(json.dumps(dict(output=str(out), files=files, external_writes=False), indent=2))
