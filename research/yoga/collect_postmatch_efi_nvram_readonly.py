"""Read only the verified external ESP and panic-related EFI variables."""
from pathlib import Path
import datetime
import hashlib
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
parts = {p['name']: p for p in disk['children']}
assert parts['sda1']['partuuid'] == '3aa58d4a-c7b7-4134-9c20-39c175f39f30'
assert all(not any(p['mountpoints'] or []) for p in parts.values())
out = ROOT / ('postmatch-efi-nvram-readonly-' + datetime.datetime.now().strftime('%Y%m%d-%H%M%S'))
out.mkdir()
report = dict(external_writes=False, nvram_writes=False, files=[])
mount = out / 'esp-ro'
mount.mkdir()
mounted = False
try:
    run('mount', '-t', 'vfat', '-o', 'ro,nosuid,nodev,noexec', '/dev/sda1', str(mount))
    mounted = True
    paths = [mount / 'EFI/OC/config.plist', mount / 'EFI/OC/OpenCore.efi', mount / 'EFI/OC/Kexts/NootedGreen.kext/Contents/MacOS/NootedGreen']
    paths.extend(p for p in mount.iterdir() if p.is_file() and p.suffix.lower() in ('.txt', '.log', '.panic'))
    for p in paths:
        if p.is_file() and p.stat().st_size <= 30000000:
            dest = out / 'copies/external-esp' / p.relative_to(mount)
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(p, dest)
            report['files'].append(dict(path=str(dest.relative_to(out)), sha256=hashlib.sha256(dest.read_bytes()).hexdigest()))
    for p in Path('/sys/firmware/efi/efivars').iterdir():
        if p.name.lower().startswith(('aapl,panic', 'panic-', 'panicinfo', 'paniclog')):
            dest = out / 'copies/panic-efivars' / p.name
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_bytes(p.read_bytes())
            report['files'].append(dict(path=str(dest.relative_to(out)), sha256=hashlib.sha256(dest.read_bytes()).hexdigest()))
finally:
    if mounted:
        run('umount', str(mount))
    (out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    for p in [out, *out.rglob('*')]:
        os.chown(p, 1000, 1000)
print(json.dumps(dict(evidence=str(out), **report), indent=2))
