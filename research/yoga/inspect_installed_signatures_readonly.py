"""Collect exact installed GPU signature evidence via read-only external APFS."""
from pathlib import Path
import datetime
import hashlib
import json
import os
import shutil
import subprocess
from audit_signature_pages import audit_file

ROOT = Path(__file__).resolve().parent


def run(*args):
    return subprocess.check_output(args, text=True, stderr=subprocess.STDOUT)


assert os.geteuid() == 0
disk = json.loads(run('lsblk', '-b', '-J', '-o', 'NAME,SERIAL,TRAN,SIZE,PARTUUID,MOUNTPOINTS', '/dev/sda'))['blockdevices'][0]
assert disk['serial'] == 'REDACTED_EXTERNAL_SERIAL' and disk['tran'] == 'usb' and disk['size'] == 256060514304
assert disk['children'][2]['partuuid'] == '7bb5254d-220e-4792-9421-1682c9f75718'
assert all(not any(x['mountpoints'] or []) for x in disk['children'])
out = ROOT / ('installed-signatures-readonly-' + datetime.datetime.now().strftime('%Y%m%d-%H%M%S'))
out.mkdir()
report = dict(disk=disk, external_writes=False, binaries=[], logs=[])
try:
    for name, index, targets in [
        ('data', 0, ['Library/Extensions', 'Library/GPUBundles', 'System/Library/Caches/xe.bundle']),
        ('system', 3, ['System/Library/Caches/xe.bundle', 'usr/bin/codesign']),
    ]:
        mount = out / (name + '-ro')
        mount.mkdir()
        mounted = False
        try:
            run('apfs-fuse', '-v', str(index), '-o', 'ro', '/dev/sda3', str(mount))
            mounted = True
            base = mount / 'root'
            assert base.is_dir()
            for target in targets:
                path = base / target
                if not path.exists():
                    continue
                for p in sorted(path.rglob('*')) if path.is_dir() else [path]:
                    if not p.is_file() or p.is_symlink():
                        continue
                    relative = p.relative_to(base)
                    try:
                        result = audit_file(p)
                        if result:
                            result['path'] = '/' + str(relative)
                            result['volume'] = name
                            report['binaries'].append(result)
                            dest = out / 'copies' / name / relative
                            dest.parent.mkdir(parents=True, exist_ok=True)
                            shutil.copyfile(p, dest)
                        elif p.name in ('Info.plist', 'CodeResources'):
                            dest = out / 'copies' / name / relative
                            dest.parent.mkdir(parents=True, exist_ok=True)
                            shutil.copyfile(p, dest)
                    except Exception as error:
                        report['binaries'].append(dict(path='/' + str(relative), volume=name, error=repr(error)))
            if name == 'data':
                support = base / 'Library/Application Support/YogaXeGPU'
                if support.is_dir():
                    for p in sorted(support.rglob('*')):
                        if p.is_file() and p.suffix in ('.log', '.txt', '.json') and p.stat().st_size < 10000000:
                            rel = p.relative_to(base)
                            dest = out / 'copies' / name / rel
                            dest.parent.mkdir(parents=True, exist_ok=True)
                            shutil.copyfile(p, dest)
                            report['logs'].append('/' + str(rel))
        finally:
            if mounted:
                run('umount', str(mount))
finally:
    (out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    for p in [out, *out.rglob('*')]:
        if not p.is_symlink():
            os.chown(p, 1000, 1000)
print(json.dumps(dict(evidence=str(out), external_writes=False, binaries=len(report['binaries']),
                     unsupported=[dict(path=b['path'], volume=b['volume'], shifts=[d['page_shift'] for s in b.get('slices', []) for d in s['directories'] if s['cpu'] == '0x1000007' and not d['x86_page_size_supported']]) for b in report['binaries'] if any(not d['x86_page_size_supported'] for s in b.get('slices', []) if s['cpu'] == '0x1000007' for d in s['directories'])]), indent=2))
