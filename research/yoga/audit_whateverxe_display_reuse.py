"""Read-only comparison of pinned WEX sources/payloads and captured Yoga drivers.

No device access, mounting, installer execution, EFI writes or source modification.
This audit establishes incompatibilities, not functioning display output.
"""
import hashlib
import json
import plistlib
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parent
WEX = Path('/tmp/opencode/WhateverXe-upstream-20261024-2221')
GRAPHICS = Path('/tmp/opencode/WhateverXe-Graphics-upstream-20261024')
LOCAL = ROOT.parent / 'hackintosh-yoga/xe-lab/WhateverXe'
CAPTURE = ROOT / 'installed-signatures-readonly-20261023-233323/copies/data/Library/Extensions'
OUT = ROOT / 'cross-build/whateverxe-display-reuse-20261024'


def command(*args, cwd=None):
    return subprocess.check_output(args, cwd=cwd, text=True).strip()


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def source_evidence(root, path, needle):
    p = root / path
    lines = p.read_text().splitlines()
    matches = [{'line': i, 'text': line.strip()} for i, line in enumerate(lines, 1)
               if needle in line]
    if not matches:
        raise RuntimeError(f'Missing evidence: {p}: {needle}')
    return {'path': path, 'sha256': sha(p), 'matches': matches}


def bundle(base, name):
    path = base / name / 'Contents'
    info = plistlib.loads((path / 'Info.plist').read_bytes())
    executable = path / 'MacOS' / info['CFBundleExecutable']
    symbols = {}
    for line in command('llvm-nm', '--defined-only', '--format=posix', str(executable)).splitlines():
        parts = line.split()
        if len(parts) == 4:
            symbols[parts[0]] = int(parts[2], 16)
    return {
        'path': str(executable), 'sha256': sha(executable), 'bytes': executable.stat().st_size,
        'identifier': info['CFBundleIdentifier'], 'version': info['CFBundleVersion'],
        'dependencies': info.get('OSBundleLibraries', {}),
        'pci_matches': {name: value.get('IOPCIPrimaryMatch')
                        for name, value in info.get('IOKitPersonalities', {}).items()},
    }, symbols


def audit():
    revisions = {
        'wex': command('git', 'rev-parse', 'HEAD', cwd=WEX),
        'graphics': command('git', 'rev-parse', 'HEAD', cwd=GRAPHICS),
        'local_wex': command('git', 'rev-parse', 'HEAD', cwd=LOCAL),
    }
    assert revisions == {
        'wex': 'c6cd8ec5ec5bd64ad3c8a852a30d1f537d819fb1',
        'graphics': 'e83018e76c0f39f0f870ff38dcc304fb357c3485',
        'local_wex': 'e481a1ef037d1ca622f51479603af06dbc565497',
    }, 'Review changed source revisions rather than silently updating conclusions'
    upstream = GRAPHICS / 'payloads/Tahoe/Library/Extensions'
    payloads, symbol_sets = {}, {}
    for label, base in [('upstream', upstream), ('captured', CAPTURE)]:
        payloads[label] = {}
        for role, name in [('framebuffer', 'AppleIntelTGLGraphicsFramebuffer.kext'),
                           ('accelerator', 'AppleIntelTGLGraphics.kext')]:
            payloads[label][role], symbols = bundle(base, name)
            if role == 'framebuffer':
                symbol_sets[label] = symbols
    expected = {
        ('upstream', 'framebuffer'): '181537d02fc25fe939923f5001c0c1f3c0bb3477035bf299ac716516d9f0256f',
        ('upstream', 'accelerator'): '6ebafd2380e6e0a83569aded411a00690ecc98a5eb1e32d367868ff22789a406',
        ('captured', 'framebuffer'): '3f2bb3336507242ddc3f69c5d7645d19ce1cd4d8748bdb4b4c8858c4b49103df',
        ('captured', 'accelerator'): '2df441e29032a68c0945942996f12337703689c18e3a447d4d15cfdab6858d5f',
    }
    for (label, role), digest in expected.items():
        assert payloads[label][role]['sha256'] == digest, (label, role)
    required = [
        '__ZN14AppleIntelPort11setPortModeENS_8PortModeE',
        '__ZN17AppleIntelPortHAL11setPortModeEN14AppleIntelPort8PortModeE',
        '__ZN17AppleIntelPortHAL13runAUXCommandEN14AppleIntelPort11AUX_CommandEPjjb',
        '__ZN17AppleIntelPortHAL15ReadDekelPhyRegEj',
        '__ZN17AppleIntelPortHAL16WriteDekelPhyRegEjj',
        '__ZN19AppleIntelPowerWell17enablePowerWellPGEj',
        '__ZN19AppleIntelPowerWell18disablePowerWellPGEj',
        '_gController',
    ]
    abi = {s: {label: hex(symbol_sets[label][s]) for label in symbol_sets} for s in required}
    assert all(values['upstream'] != values['captured'] for values in abi.values())
    evidence = [source_evidence(WEX, p, n) for p, n in [
        ('README.md', 'Alder Lake-H/P/U and Raptor Lake-H/P/U'),
        ('README.md', 'HDMI and USB-C DisplayPort Alt Mode'),
        ('WhateverXe/kern_xelp_devices.hpp', '{0x46A6, Family::AlderLakeP, Display::XeLPD, GT::Unknown'),
        ('WhateverXe/kern_xe.cpp', 'displayAdapter(self->xeProfile.family)==XELP::DisplayAdapter::TGL'),
        ('WhateverXe/kern_display13_tc.hpp', 'if(ready&3)'),
        ('WhateverXe/kern_tgl_port_map.hpp', 'out.pllEnable = 0x46030 + 4 * tc;'),
        ('WhateverXe/kern_display13_tc.hpp', '0x46038+8*tc'),
        ('WhateverXe/kern_xe.cpp', 'modeFn==address+0x5FCB8'),
        ('WhateverXe/kern_xe.cpp', 'fn == address + 0x60AE0'),
    ]]
    evidence += [source_evidence(LOCAL, p, n) for p, n in [
        ('WhateverXe/kern_alderlake_p.hpp', 'Identity = 0x46A68086'),
        ('WhateverXe/kern_xelp_platform.hpp', 'if(profile.display!=Display::XeLPD || legacy || typeC || legacyBackend)'),
        ('WhateverXe/kern_xelp_platform.hpp', 'uint32_t abi {4}'),
    ]]
    dirty = command('git', 'status', '--porcelain=v1', cwd=LOCAL)
    return {
        'revisions': revisions, 'payloads': payloads, 'native_symbol_offsets': abi,
        'source_evidence': evidence, 'local_wex_status_preserved': dirty,
        'local_modified_fixture_sha256': sha(LOCAL / 'tests/tgl_bootstrap_aux_fixture.cpp'),
        'tc3_coordinates': {
            'apple_ddi': 5, 'zero_based_tc': 2, 'fia_instance': 1, 'fia_local': 0,
            'tgl_pll': hex(0x46030 + 4*2), 'adlp_pll': hex(0x46038 + 8*2),
            'adlp_ready': hex(0x161500 + 4*2), 'adlp_ownership': 'DDI_BUF_CTL(DDI5) bit6',
        },
        'decision': 'Use WEX as reference; neither stock WEX nor its Graphics package is a qualified drop-in Yoga repair.',
        'runtime_validated': False, 'display_output_fixed': False,
        'device_writes': False, 'efi_changes': False, 'apfs_writes': False,
    }


if __name__ == '__main__':
    result = audit()
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / 'report.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({'report': str(OUT / 'report.json'), 'decision': result['decision'],
                      'display_output_fixed': False, 'device_writes': False}, indent=2))
