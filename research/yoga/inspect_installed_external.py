"""Guarded read-only inspection of the user's external installed Tahoe disk."""
from pathlib import Path
import datetime
import hashlib
import json
import os
import plistlib
import shutil
import subprocess

ROOT = Path(__file__).resolve().parent
assert os.geteuid() == 0
def run(*args):
    return subprocess.check_output(args, text=True, stderr=subprocess.STDOUT)
tree = json.loads(run('lsblk', '-b', '-J', '-o', 'NAME,PATH,SIZE,MODEL,SERIAL,TRAN,PARTUUID,MOUNTPOINTS', '/dev/sda'))['blockdevices'][0]
assert tree['tran'] == 'usb' and tree['serial'] == 'REDACTED_EXTERNAL_SERIAL'
assert tree['size'] == 256060514304
assert tree['children'][2]['partuuid'] == '7bb5254d-220e-4792-9421-1682c9f75718'
assert all(not any(x['mountpoints'] or []) for x in tree['children'])
out = ROOT / ('installed-external-readonly-' + datetime.datetime.now().strftime('%Y%m%d-%H%M%S'))
out.mkdir()
(out/'disk.json').write_text(json.dumps(tree, indent=2)+'\n')
(out/'apfs-inventory.txt').write_text(run('apfsutil', '/dev/sda3'))
mount = out/'esp-ro'
mount.mkdir()
mounted = False
try:
    run('mount', '-t', 'vfat', '-o', 'ro,nosuid,nodev,noexec', '/dev/sda1', str(mount))
    mounted = True
    shutil.copytree(mount/'EFI', out/'EFI', symlinks=True)
    config = plistlib.loads((out/'EFI/OC/config.plist').read_bytes())
    hashes = {str(p.relative_to(out/'EFI')):hashlib.file_digest(p.open('rb'),'sha256').hexdigest()
              for p in sorted((out/'EFI').rglob('*')) if p.is_file()}
    (out/'efi-sha256.json').write_text(json.dumps(hashes, indent=2)+'\n')
    report = dict(snapshot=str(out), config_sha256=hashes['OC/config.plist'],
                  boot_args=config['NVRAM']['Add']['7C436110-AB2A-4BBB-A880-FE41995C9F82'].get('boot-args'),
                  csr_active_config=config['NVRAM']['Add']['7C436110-AB2A-4BBB-A880-FE41995C9F82'].get('csr-active-config',b'').hex(),
                  security=config['Misc']['Security'],
                  graphics_kexts=[x for x in config['Kernel']['Add'] if any(t in x['BundlePath'].lower() for t in ['green','hook','tgl'])],
                  enabled_acpi=[x['Path'] for x in config['ACPI']['Add'] if x['Enabled']],
                  writes_to_external=False)
    (out/'report.json').write_text(json.dumps(report, indent=2, default=str)+'\n')
    print(json.dumps(report, indent=2, default=str))
    print((out/'apfs-inventory.txt').read_text())
finally:
    if mounted:
        run('umount', str(mount))
    for path in [out, *out.rglob('*')]:
        if not path.is_symlink():
            os.chown(path, 1000, 1000)
