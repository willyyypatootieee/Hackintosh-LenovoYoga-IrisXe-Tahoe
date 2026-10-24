"""Restore only verbose boot on the verified external Samsung ESP."""
from pathlib import Path
import copy
import datetime
import json
import os
import plistlib
from deploy_yoga_gms_runtime import check_disk, durable, manifest, run
from yoga_fb_match_patch import sha

assert os.geteuid() == 0
root = Path(__file__).resolve().parent
expected = '368ca8b0c1357938af993ff8cd4d744e9b7c0d5d1491e15eacc0e0c23cc53b81'
stamp = datetime.datetime.now().strftime('%Y%m%d-%H%M%S')
out = root / ('verbose-only-apply-' + stamp)
out.mkdir()
mount = out / 'esp'
mount.mkdir()
check_disk()
mounted = False
try:
    run('mount', '-t', 'vfat', '-o', 'ro,nosuid,nodev,noexec', '/dev/sda1', mount)
    mounted = True
    target = mount / 'EFI/OC/config.plist'
    original = target.read_bytes()
    assert sha(original) == expected, 'Unexpected current config; stop without writing'
    old = plistlib.loads(original)
    new = copy.deepcopy(old)
    guid = '7C436110-AB2A-4BBB-A880-FE41995C9F82'
    args = new['NVRAM']['Add'][guid]['boot-args']
    assert '-v' not in args.split()
    new['NVRAM']['Add'][guid]['boot-args'] = '-v ' + args
    check = copy.deepcopy(new)
    check['NVRAM']['Add'][guid]['boot-args'] = args
    assert check == old
    payload = plistlib.dumps(new, sort_keys=False)
    (out / 'config.original.plist').write_bytes(original)
    (out / 'config.plist').write_bytes(payload)
    (out / 'ocvalidate.txt').write_text(run('/path/to/local-workspace/nvidia-macos-driver/_oc_stage/rel/Utilities/ocvalidate/ocvalidate.linux', out / 'config.plist'))
    before = manifest(mount / 'EFI')
    run('mount', '-o', 'remount,rw', mount)
    backup = mount / 'YogaXeGPU-backups' / ('verbose-only-' + stamp)
    backup.mkdir(parents=True)
    durable(backup / 'config.plist', original)
    assert (backup / 'config.plist').read_bytes() == original
    tmp = target.with_name('config.verbose-only.tmp')
    durable(tmp, payload)
    os.replace(tmp, target)
    run('sync', '-f', mount)
    wanted = dict(before)
    wanted['OC/config.plist'] = sha(payload)
    assert manifest(mount / 'EFI') == wanted
    run('umount', mount)
    mounted = False
    check_disk()
    run('mount', '-t', 'vfat', '-o', 'ro,nosuid,nodev,noexec', '/dev/sda1', mount)
    mounted = True
    assert manifest(mount / 'EFI') == wanted
    report = dict(applied=True, change='Added -v only', config_sha256=sha(payload),
                  independent_readback_passed=True, audio_and_gpu_unchanged=True,
                  internal_disk_writes=False, apfs_writes=False, backup=str(backup.relative_to(mount)))
    (out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))
finally:
    if mounted: run('umount', mount)
    for p in [out, *out.rglob('*')]: os.chown(p, 1000, 1000)
