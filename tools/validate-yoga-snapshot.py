#!/usr/bin/env python3
"""Offline snapshot checks. Does not mount disks or establish runtime success."""
import hashlib
import json
import plistlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
EFI = ROOT / 'EFI'
config = plistlib.loads((EFI / 'OC/config.plist').read_bytes())
generic = config['PlatformInfo']['Generic']
for key in ['SystemSerialNumber', 'MLB', 'SystemUUID']:
    assert not generic[key], f'Public SMBIOS identifier present: {key}'
assert not any(generic['ROM']), 'Public ROM identifier present'

for row in config['Kernel']['Add']:
    if not row['Enabled']:
        continue
    base = EFI / 'OC/Kexts' / row['BundlePath']
    assert (base / row['PlistPath']).is_file(), row['BundlePath']
    if row['ExecutablePath']:
        assert (base / row['ExecutablePath']).is_file(), row['BundlePath']
for row in config['ACPI']['Add']:
    if row['Enabled']:
        assert (EFI / 'OC/ACPI' / row['Path']).is_file(), row['Path']
for row in config['UEFI']['Drivers']:
    if row['Enabled']:
        assert (EFI / 'OC/Drivers' / row['Path']).is_file(), row['Path']

enabled = {x['BundlePath'] for x in config['Kernel']['Add'] if x['Enabled']}
assert {'Lilu.kext', 'NootedGreen.kext', 'YogaGMSFix.kext', 'YogaCursorArb.kext'} <= enabled
assert 'WhateverGreen.kext' not in enabled
expected = json.loads((ROOT / 'docs/yoga/EFI-SHA256.json').read_text())
actual = {str(p.relative_to(EFI)): hashlib.sha256(p.read_bytes()).hexdigest()
          for p in EFI.rglob('*') if p.is_file()}
assert actual == expected, 'EFI differs from the published manifest'
for p in ROOT.rglob('*'):
    if p.is_file() and '.git' not in p.parts:
        assert p.stat().st_size < 100_000_000, f'Use release assets for large file: {p}'
print(f'PASS: {len(actual)} EFI hashes, enabled paths and public SMBIOS sanitization')
print('Offline checks only; HDMI/stability and clean reinstall are NOT validated.')
