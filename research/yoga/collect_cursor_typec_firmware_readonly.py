"""Read OS-exposed display firmware/topology; no flash, PCI or disk writes.

Destinations are ordinary research files only. No /dev/mem, SPI access,
PCI ROM enable, register writes, connector reprobe or firmware modification.
"""
from pathlib import Path
import datetime
import hashlib
import json
import os
import shutil
import subprocess

ROOT = Path(__file__).resolve().parent
assert os.geteuid() == 0
out = ROOT / ('cursor-typec-firmware-readonly-' + datetime.datetime.now().strftime('%Y%m%d-%H%M%S'))
out.mkdir()
report = dict(flash_access=False, pci_writes=False, target_disk_writes=False, files=[], errors=[])

def save(src, relative):
    try:
        data = src.read_bytes()
        dest = out / relative
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_bytes(data)
        report['files'].append(dict(source=str(src), path=str(relative), size=len(data), sha256=hashlib.sha256(data).hexdigest()))
    except OSError as e: report['errors'].append(dict(source=str(src), error=str(e)))

try:
    for p in Path('/sys/firmware/acpi/tables').iterdir():
        if p.is_file(): save(p, Path('acpi') / p.name)
    for p in Path('/sys/firmware/dmi/tables').glob('*'):
        if p.is_file(): save(p, Path('dmi') / p.name)
    for p in Path('/sys/class/dmi/id').glob('*'):
        if p.is_file(): save(p, Path('dmi-id') / p.name)
    for card in Path('/sys/class/drm').glob('card[0-9]*'):
        if '-' in card.name or (card / 'device').resolve().name != '0000:00:02.0': continue
        minor = card.name[4:]
        debug = Path('/sys/kernel/debug/dri') / minor
        for name in ['i915_vbt', 'i915_opregion', 'i915_display_info', 'state']:
            if (debug / name).exists(): save(debug / name, Path('intel-display') / name)
    for conn in Path('/sys/class/drm').glob('card*-*'):
        for name in ['status', 'enabled', 'modes', 'edid', 'connector_id', 'dpms']:
            if (conn / name).exists(): save(conn / name, Path('drm') / conn.name / name)
    for p in Path('/sys/class/typec').glob('*'):
        for name in ['data_role', 'power_role', 'port_type', 'preferred_role', 'power_operation_mode', 'active', 'svid', 'mode', 'vdo', 'description']:
            if (p / name).exists(): save(p / name, Path('typec') / p.name / name)
    for filename, command in [('lspci.txt', ['lspci', '-Dnnk']), ('usb.txt', ['lsusb', '-t']),
                              ('drm-connectors.txt', ['modetest', '-M', 'i915', '-c']),
                              ('kernel-log.txt', ['dmesg', '--color=never'])]:
        result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=20)
        (out / filename).write_bytes(result.stdout)
    source = Path('/tmp/opencode/yoga-display-host.json')
    if source.exists(): shutil.copyfile(source, out / 'host-topology.json')
finally:
    (out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    for p in [out, *out.rglob('*')]: os.chown(p, 1000, 1000)
print(json.dumps(dict(output=str(out), files=len(report['files']), errors=report['errors'], flash_access=False, pci_writes=False, target_disk_writes=False), indent=2))
