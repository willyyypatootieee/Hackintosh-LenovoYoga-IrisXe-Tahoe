"""Read-only AppleHDA/controller inventory on the authorized external Tahoe."""
from pathlib import Path
import datetime
import json
import os
import plistlib
import shutil
from deploy_yoga_gms_runtime import check_disk, run

ROOT = Path(__file__).resolve().parent
assert os.geteuid() == 0
check_disk()
out = ROOT / ('audio-inventory-readonly-' + datetime.datetime.now().strftime('%Y%m%d-%H%M%S'))
out.mkdir()
report = dict(external_writes=False, volumes={})
try:
    for index, label in [(3, 'system'), (0, 'data')]:
        mount = out / (label + '-ro')
        mount.mkdir()
        mounted = False
        try:
            run('apfs-fuse', '-v', str(index), '-o', 'ro', '/dev/sda3', mount)
            mounted = True
            root = mount / 'root'
            inventory = {}
            for rel in ['System/Library/Extensions', 'Library/Extensions', 'System/Library/KernelCollections', 'Library/KernelCollections']:
                folder = root / rel
                if not folder.exists():
                    inventory[rel] = None
                    continue
                inventory[rel] = sorted(p.name for p in folder.iterdir())
                for p in folder.iterdir():
                    if p.is_dir() and any(s in p.name.lower() for s in ['hda', 'alc', 'audio']):
                        for info in p.rglob('Info.plist'):
                            dest = out / 'copies' / label / info.relative_to(root)
                            dest.parent.mkdir(parents=True, exist_ok=True)
                            shutil.copyfile(info, dest)
            report['volumes'][label] = inventory
        finally:
            if mounted: run('umount', mount)
finally:
    (out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    for p in [out, *out.rglob('*')]: os.chown(p, 1000, 1000)
print(json.dumps(dict(output=str(out), **report), indent=2))
