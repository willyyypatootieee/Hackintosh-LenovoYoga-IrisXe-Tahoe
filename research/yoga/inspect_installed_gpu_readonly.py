"""Read installed OS build and GPU bundle presence via read-only apfs-fuse."""
from pathlib import Path
import datetime
import json
import os
import plistlib
import subprocess

ROOT = Path(__file__).resolve().parent
assert os.geteuid() == 0
def run(*args):
    return subprocess.check_output(args, text=True, stderr=subprocess.STDOUT)
tree = json.loads(run('lsblk','-b','-J','-o','NAME,PATH,SERIAL,TRAN,PARTUUID','/dev/sda'))['blockdevices'][0]
assert tree['serial'] == 'REDACTED_EXTERNAL_SERIAL' and tree['tran'] == 'usb'
assert tree['children'][2]['partuuid'] == '7bb5254d-220e-4792-9421-1682c9f75718'
out = ROOT / ('installed-gpu-readonly-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S'))
out.mkdir()
report = {}
for name, index in [('system',3),('data',0)]:
    mount = out / (name+'-ro')
    mount.mkdir()
    mounted = False
    try:
        run('apfs-fuse','-v',str(index),'-o','ro','/dev/sda3',str(mount))
        mounted = True
        report[name+'_root_entries'] = sorted(x.name for x in mount.iterdir())
        base = mount/'root' if (mount/'root').is_dir() else mount
        if name == 'system':
            p = base/'System/Library/CoreServices/SystemVersion.plist'
            report['os_version'] = plistlib.loads(p.read_bytes()) if p.exists() else None
            report['caches_symlink'] = os.readlink(base/'System/Library/Caches') if (base/'System/Library/Caches').is_symlink() else None
            for rel in ['usr/bin/codesign','usr/bin/kmutil','System/Library/PrivateFrameworks/GPUCompiler.framework/Versions/32023']:
                report[rel] = (base/rel).exists()
        else:
            paths=['Library/Extensions','Library/GPUBundles','System/Library/Caches','Users']
            report['data_directories'] = {p:sorted(x.name for x in (base/p).iterdir()) if (base/p).is_dir() else None for p in paths}
    finally:
        if mounted:
            run('umount',str(mount))
(out/'report.json').write_text(json.dumps(report,indent=2,default=str)+'\n')
for p in [out,*out.rglob('*')]:
    os.chown(p,1000,1000)
print(json.dumps(report,indent=2,default=str))
