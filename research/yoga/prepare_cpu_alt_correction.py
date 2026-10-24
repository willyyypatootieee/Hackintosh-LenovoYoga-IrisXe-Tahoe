"""Audit and back up the authorized external-EFI CPU-table correction.

Does not modify the live config or execute native firmware methods.
"""
from pathlib import Path
import datetime
import hashlib
import json
import plistlib
import struct
import subprocess

ROOT = Path(__file__).resolve().parent
OC = Path('/run/media/willy/EFI/EFI/OC')
NATIVE = ROOT.parent / 'yoga-adlp-firmware-capture-20261012/acpi'
WORKING = Path('/home/willy/Videos/EFI-backup-20261011-003339/OC')
EXPECTED = '480b9be681de3afc116a2bc9c511d97b6284432a52f855d620f55fd53a6df445'


def digest(data):
    return hashlib.sha256(data).hexdigest()


def manifest():
    return {str(p.relative_to(OC)): digest(p.read_bytes())
            for p in sorted(OC.rglob('*'))
            if p.is_file() and 'Backups' not in p.relative_to(OC).parts}


source = subprocess.check_output(
    ['findmnt', '-no', 'SOURCE', '--target', str(OC)], text=True).strip()
assert source == '/dev/sda1', source
uuid = subprocess.check_output(
    ['findmnt', '-no', 'UUID', '--target', str(OC)], text=True).strip()
assert uuid == '2719-4E8A', uuid
original = (OC / 'config.plist').read_bytes()
assert digest(original) == EXPECTED, 'Active configuration changed; stop.'
config = plistlib.loads(original)
working = plistlib.loads((WORKING / 'config.plist').read_bytes())
for name in ['SSDT-PLUG-ALT.aml', 'SSDT-PLUG.aml']:
    entry = next(e for e in config['ACPI']['Add'] if e['Path'] == name)
    assert entry['Enabled'] == (name == 'SSDT-PLUG.aml')
    entry['Enabled'] = name == 'SSDT-PLUG-ALT.aml'
assert next(e for e in working['ACPI']['Add']
            if e['Path'] == 'SSDT-PLUG-ALT.aml')['Enabled']
alt = (OC / 'ACPI/SSDT-PLUG-ALT.aml').read_bytes()
assert alt == (WORKING / 'ACPI/SSDT-PLUG-ALT.aml').read_bytes()
assert digest(alt) == 'a466f849333ffef4881af2fbb07275417a9caa0f53d2448720e775f15ea674b3'
assert sum(alt) % 256 == 0

# MADT processor IDs are firmware UIDs, not hardware APIC IDs.
madt = (NATIVE / 'APIC').read_bytes()
assert sum(madt) % 256 == 0
offset, processors = 44, []
while offset < len(madt):
    kind, length = madt[offset:offset + 2]
    assert length >= 2
    if kind == 0:
        uid, apic, flags = struct.unpack_from('<BBI', madt, offset + 2)
        processors.append(dict(uid=uid, apic_id=apic, flags=flags))
    elif kind == 9:
        _, apic, flags, uid = struct.unpack_from('<HIII', madt, offset + 2)
        processors.append(dict(uid=uid, apic_id=apic, flags=flags))
    offset += length
active = [p for p in processors if p['flags'] & 3]
assert len(active) == 20 and {p['uid'] for p in active} == set(range(20))

stamp = datetime.datetime.now().strftime('%Y%m%d-%H%M%S')
stage = ROOT / ('cpu-alt-correction-' + stamp)
stage.mkdir()
files, applied = [], []
native_paths = [NATIVE / 'DSDT'] + [NATIVE / ('SSDT' + str(i)) for i in range(1, 27)]
for path in native_paths:
    data = path.read_bytes()
    if data[:4] not in (b'DSDT', b'SSDT'):
        continue
    assert len(data) == int.from_bytes(data[4:8], 'little')
    for patch in config['ACPI']['Patch']:
        if not patch['Enabled']:
            continue
        assert not patch['Mask'] and not patch['ReplaceMask']
        assert not patch['Base'] and not patch['Skip'] and not patch['Limit']
        if patch['TableSignature'] and patch['TableSignature'] != data[:4]:
            continue
        if patch['OemTableId'] and patch['OemTableId'] != data[16:24]:
            continue
        if patch['TableLength'] and patch['TableLength'] != len(data):
            continue
        count = data.count(patch['Find'])
        if patch['Count']:
            assert count == patch['Count'], (path, patch['Comment'], count)
        data = data.replace(patch['Find'], patch['Replace'], patch['Count'] or -1)
        if count:
            applied.append(dict(table=path.name, patch=patch['Comment'], count=count))
    data = bytearray(data)
    data[9] = 0
    data[9] = (-sum(data)) & 255
    out = stage / (path.name + '.aml')
    out.write_bytes(data)
    files.append(str(out))
assert len(files) == 27
enabled = [e['Path'] for e in config['ACPI']['Add'] if e['Enabled']]
files += [str(OC / 'ACPI' / name) for name in enabled]
command = ['acpiexec', '-di', '-l', '-b', 'quit'] + files
run = subprocess.run(command, capture_output=True, text=True, timeout=90)
output = run.stdout + run.stderr
(stage / 'namespace-load.txt').write_text(output)
assert run.returncode == 0, output[-5000:]
assert '34 ACPI AML tables successfully acquired and loaded' in output, output[-5000:]
assert 'AE_ALREADY_EXISTS' not in output and 'ACPI Error:' not in output, output[-5000:]

before = manifest()
backup = OC / 'Backups' / ('cpu-alt-correction-before-' + stamp)
backup.mkdir(parents=True)
(backup / 'config.plist').write_bytes(original)
(backup / 'original-oc-sha256.json').write_text(json.dumps(before, indent=2) + '\n')
(stage / 'config.before.plist').write_bytes(original)
(stage / 'original-oc-sha256.json').write_text(json.dumps(before, indent=2) + '\n')
report = dict(scope='Only enable ALT and disable property-only PLUG; no live config edit yet.',
              backup=str(backup), stage=str(stage), original_config_sha256=EXPECTED,
              working_backup=str(WORKING), alt_sha256=digest(alt),
              active_madt_processors=active, enabled_acpi_after=enabled,
              native_patches=applied, offline_namespace_result='34 tables loaded; no ACPI errors or name collisions; STA/INI disabled',
              expected_config=config)
(stage / 'expected-config.plist').write_bytes(plistlib.dumps(config, sort_keys=False))
del report['expected_config']
(stage / 'preflight.json').write_text(json.dumps(report, indent=2) + '\n')
(ROOT / 'cpu-alt-correction-latest.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report, indent=2))
