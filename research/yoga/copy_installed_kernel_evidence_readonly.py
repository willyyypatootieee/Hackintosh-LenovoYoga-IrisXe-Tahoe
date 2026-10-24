"""Copy only installed kernel/KC and prior GPU logs using read-only APFS."""
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
tree = json.loads(run('lsblk', '-b', '-J', '-o', 'NAME,PATH,SERIAL,TRAN,PARTUUID', '/dev/sda'))['blockdevices'][0]
assert tree['serial'] == 'REDACTED_EXTERNAL_SERIAL' and tree['tran'] == 'usb'
assert tree['children'][2]['partuuid'] == '7bb5254d-220e-4792-9421-1682c9f75718'
out = ROOT / ('kernel-evidence-readonly-' + datetime.datetime.now().strftime('%Y%m%d-%H%M%S'))
out.mkdir()
report = {'external_serial': tree['serial'], 'apfs_access': 'read-only', 'files': []}
for name, index in [('system', 3), ('data', 0)]:
    mount = out / (name + '-ro')
    mount.mkdir()
    mounted = False
    try:
        run('apfs-fuse', '-v', str(index), '-o', 'ro', '/dev/sda3', str(mount))
        mounted = True
        base = mount / 'root'
        assert base.is_dir()
        if name == 'system':
            version = plistlib.loads((base / 'System/Library/CoreServices/SystemVersion.plist').read_bytes())
            assert version['ProductBuildVersion'] == '25G241'
            report['os_version'] = version
            paths = ['System/Library/Kernels/kernel', 'System/Library/KernelCollections/BootKernelExtensions.kc',
                     'System/Library/KernelCollections/SystemKernelExtensions.kc']
        else:
            backups = base / 'Library/Application Support/YogaXeGPU/backups'
            report['backup_directories'] = sorted(p.name for p in backups.iterdir()) if backups.is_dir() else []
            paths = []
            for backup in sorted(backups.iterdir()) if backups.is_dir() else []:
                for filename in ['install.log', 'INSTALL-INCOMPLETE', 'native-signing-v2.log', 'restore-paths.txt']:
                    p = backup / filename
                    if p.is_file():
                        paths.append(str(p.relative_to(base)))
        for rel in paths:
            source = base / rel
            if not source.is_file():
                report['files'].append({'source': name + '/' + rel, 'absent': True})
                continue
            dest = out / 'copies' / name / rel
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, dest)
            report['files'].append({'source': name + '/' + rel, 'local': str(dest.relative_to(out)),
                                    'bytes': dest.stat().st_size,
                                    'sha256': hashlib.file_digest(dest.open('rb'), 'sha256').hexdigest()})
    finally:
        if mounted:
            run('umount', str(mount))
(out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
for p in [out, *out.rglob('*')]:
    os.chown(p, 1000, 1000)
print(json.dumps({'evidence_directory': str(out), **report}, indent=2))
