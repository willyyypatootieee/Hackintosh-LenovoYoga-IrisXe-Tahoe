"""Verify installed v3 code and GPU boot-collection membership; no disk writes."""
from pathlib import Path
import datetime
import hashlib
import json
import os
import plistlib
import struct
import subprocess

ROOT = Path(__file__).resolve().parent
assert os.geteuid() == 0
def run(*args):
    return subprocess.check_output(args, text=True, stderr=subprocess.STDOUT)
disk = json.loads(run('lsblk', '-b', '-J', '-o', 'NAME,SERIAL,TRAN,SIZE,PARTUUID,MOUNTPOINTS', '/dev/sda'))['blockdevices'][0]
assert disk['serial'] == 'REDACTED_EXTERNAL_SERIAL' and disk['tran'] == 'usb' and disk['size'] == 256060514304
assert disk['children'][2]['partuuid'] == '7bb5254d-220e-4792-9421-1682c9f75718'
assert all(not any(x['mountpoints'] or []) for x in disk['children'])
out = ROOT / ('v3-boot-readonly-' + datetime.datetime.now().strftime('%Y%m%d-%H%M%S'))
out.mkdir()
report = {'external_serial': disk['serial'], 'physical_disk_writes': False, 'collections': [], 'repairs': []}
targets = {'org.smichaud.HookCase', 'com.xxxxx.driver.AppleIntelTGLGraphics', 'com.xxxxx.driver.AppleIntelTGLGraphicsFramebuffer'}

def macho_commands(data, base=0):
    assert struct.unpack_from('<I', data, base)[0] == 0xfeedfacf
    pos = base + 32
    for _ in range(struct.unpack_from('<I', data, base + 16)[0]):
        cmd, size = struct.unpack_from('<II', data, pos)
        assert size >= 8 and pos + size <= len(data)
        yield cmd, pos, size
        pos += size

def code_sections(data):
    result = {}
    for cmd, pos, _ in macho_commands(data):
        if cmd != 0x19:
            continue
        for i in range(struct.unpack_from('<I', data, pos + 64)[0]):
            s = pos + 72 + 80 * i
            section = data[s:s+16].rstrip(b'\0').decode()
            segment = data[s+16:s+32].rstrip(b'\0').decode()
            size, offset = struct.unpack_from('<QI', data, s+40)
            if section in ['__text', '__mod_init_func', '__mod_term_func']:
                result[segment + ',' + section] = {'size': size, 'sha256': hashlib.sha256(data[offset:offset+size]).hexdigest()}
    return result

def collection(path, label):
    item = {'path': label, 'bytes': path.stat().st_size}
    try:
        data = path.read_bytes()
        ids = []
        for cmd, pos, size in macho_commands(data):
            if cmd == 0x80000035:
                off = struct.unpack_from('<I', data, pos+24)[0]
                ids.append(data[pos+off:pos+size].split(b'\0')[0].decode())
        item['entries'] = len(ids)
        item['gpu_entries'] = sorted(targets.intersection(ids))
        item['all_three_present'] = targets.issubset(ids)
        item['sha256'] = hashlib.sha256(data).hexdigest()
        if 'Aux' in path.name:
            (out / (label.replace('/', '_') + '.ids.json')).write_text(json.dumps(ids, indent=2)+'\n')
    except Exception as error:
        item['error'] = str(error)
    report['collections'].append(item)

for name, index in [('data', 0), ('preboot', 1)]:
    mount = out / (name + '-ro')
    mount.mkdir()
    mounted = False
    try:
        run('apfs-fuse', '-v', str(index), '-o', 'ro', '/dev/sda3', str(mount))
        mounted = True
        base = mount / 'root'
        assert base.is_dir()
        if name == 'data':
            binary = base / 'Library/Extensions/HookCase.kext/Contents/MacOS/HookCase'
            local = ROOT / 'cross-build/fixed-hookcase-kernel-dtors-v3/HookCase.kext/Contents/MacOS/HookCase'
            actual, expected = code_sections(binary.read_bytes()), code_sections(local.read_bytes())
            report['installed_hookcase_code_matches_v3'] = actual == expected
            report['installed_hookcase_sections'] = actual
            repairs = base / 'Library/Application Support/YogaXeGPU/repairs'
            for folder in sorted(repairs.iterdir()) if repairs.is_dir() else []:
                info = {'name': folder.name, 'markers': sorted(p.name for p in folder.iterdir() if p.is_file())}
                log = folder / 'repair.log'
                if log.is_file():
                    text = log.read_text(errors='replace')
                    (out / (folder.name + '-repair.log')).write_text(text)
                    info['macos_reboot_request'] = next((line for line in text.splitlines() if line.startswith('Loading extension(s):') and line.endswith(' requires a reboot')), None)
                    info['old_link_error'] = 'Failed to bind' in text
                report['repairs'].append(info)
            search_roots = [base / 'Library/KernelCollections', base / 'System/Library/KernelCollections']
            policy = base / 'private/var/db/SystemPolicyConfiguration/KextPolicy'
            if policy.is_file():
                dest = out / 'KextPolicy'
                dest.write_bytes(policy.read_bytes())
                for suffix in ['-wal', '-shm']:
                    extra = Path(str(policy) + suffix)
                    if extra.is_file():
                        (out / ('KextPolicy' + suffix)).write_bytes(extra.read_bytes())
        else:
            search_roots = [p for p in base.iterdir() if p.is_dir()]
            report['preboot_roots'] = [p.name for p in search_roots]
        for search in search_roots:
            if not search.is_dir():
                continue
            for folder, dirs, files in os.walk(search):
                relative = Path(folder).relative_to(search)
                if len(relative.parts) > 9:
                    dirs[:] = []
                    continue
                for filename in files:
                    if filename.endswith('.kc'):
                        path = Path(folder) / filename
                        collection(path, name + '/' + str(path.relative_to(base)))
    finally:
        if mounted:
            run('umount', str(mount))
(out / 'report.json').write_text(json.dumps(report, indent=2)+'\n')
for p in [out, *out.rglob('*')]:
    os.chown(p, 1000, 1000)
print(json.dumps({'evidence': str(out), **report}, indent=2))
