"""Linux entry point: verified external-only read-only collectors, no installation."""
from pathlib import Path
from datetime import datetime
import hashlib
import json
import os
import subprocess
import sys

ROOT = Path(__file__).resolve().parent


def main():
    if not sys.platform.startswith('linux') or os.geteuid() != 0:
        raise SystemExit('Run on Linux with sudo. No collection performed.')
    disk = json.loads(subprocess.check_output(['lsblk', '-b', '-J', '-o',
        'NAME,SERIAL,TRAN,SIZE,PARTUUID,MOUNTPOINTS', '/dev/sda'], text=True))['blockdevices'][0]
    assert disk['serial'] == 'REDACTED_EXTERNAL_SERIAL' and disk['tran'] == 'usb' and disk['size'] == 256060514304
    parts = {p['name']: p for p in disk['children']}
    assert parts['sda1']['partuuid'] == '3aa58d4a-c7b7-4134-9c20-39c175f39f30'
    assert parts['sda3']['partuuid'] == '7bb5254d-220e-4792-9421-1682c9f75718'
    assert all(not any(p['mountpoints'] or []) for p in parts.values())
    out = ROOT / ('cursor-hang-collection-' + datetime.now().strftime('%Y%m%d-%H%M%S'))
    out.mkdir(exist_ok=False)
    report = dict(external_writes=False, nvram_writes=False, internal_efi_access=False,
        identity=disk, collectors=[])
    try:
        for name in ['collect_postmatch_efi_nvram_readonly.py', 'collect_display_failures_readonly.py',
                     'collect_latest_unified_readonly.py']:
            script = ROOT / name
            print('Collecting:', name, flush=True)
            # Each collector verifies identity again, mounts read-only and unmounts
            # in finally. Do not kill a mount-using child with an arbitrary timeout.
            result = subprocess.run([sys.executable, str(script)], cwd=ROOT, text=True, capture_output=True)
            (out / (name + '.stdout.txt')).write_text(result.stdout)
            (out / (name + '.stderr.txt')).write_text(result.stderr)
            report['collectors'].append(dict(script=name, source_sha256=hashlib.sha256(script.read_bytes()).hexdigest(),
                returncode=result.returncode))
            if result.returncode:
                raise RuntimeError(name + ' failed; see ' + str(out))
    finally:
        (out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
        uid, gid = int(os.environ.get('SUDO_UID', '1000')), int(os.environ.get('SUDO_GID', '1000'))
        for path in [out, *out.iterdir()]:
            os.chown(path, uid, gid)
    print('Read-only collection complete:', out)
    print('Contains private logs/identifiers; no need to publish the complete archive.')


if __name__ == '__main__':
    main()
