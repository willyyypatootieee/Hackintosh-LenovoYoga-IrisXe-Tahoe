"""Pinned offline compatibility audit. No installer, device access or EFI writes.

Uses the independently read-back *post-cursor-trial* config, not the older
pre-trial baseline. Hash/signature checks establish file integrity, not runtime
compatibility, provenance of a build, AMFI acceptance or recovery correctness.
"""
import hashlib
import json
import plistlib
import re
import subprocess
from pathlib import Path

from audit_cursor_diag_inputs import image
from audit_signature_pages import audit_file
from yoga_fb_match_patch import apply_to_binary, symbol

ROOT = Path(__file__).resolve().parent
REPO = Path('/home/willy/Downloads/Hackintosh-XPS13Plus-IrisXe-Tahoe')
HEAD = 'c3ea92670cd043979784fc22c1aab8b137f020f4'
BASE = ROOT / 'cursor-arb-apply-20261024-042459/config.plist'
OUT = ROOT / 'cross-build/upstream-yoga-c3ea926-audit-20261024'
ACC = ROOT / 'installed-signatures-readonly-20261023-233323/copies/data/Library/Extensions/AppleIntelTGLGraphics.kext/Contents/MacOS/AppleIntelTGLGraphics'


def git(*args, binary=False):
    return subprocess.check_output(['git', *args], cwd=REPO, text=not binary)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def byte_array(source, name):
    match = re.search(r'static const uint8_t\s+' + re.escape(name) + r'\[\]\s*=\s*\{([^}]+)\}', source)
    if not match:
        raise ValueError(name)
    return bytes(int(n, 16) for n in re.findall(r'0x([0-9a-fA-F]+)', match[1]))


def main():
    if not __debug__:
        raise RuntimeError('Do not run with Python optimization')
    assert git('rev-parse', 'HEAD').strip() == HEAD
    assert not git('status', '--porcelain').strip(), 'Reference must be clean'
    config_data = BASE.read_bytes()
    assert sha(config_data) == '9078149a667c94d17519b494a4d19df9c66ccc8f751370857b20c7822b725a11'
    config = plistlib.loads(config_data)
    adds = {x['BundlePath']: x['Enabled'] for x in config['Kernel']['Add']}
    assert all(adds[x] for x in ['Lilu.kext', 'NootedGreen.kext', 'YogaGMSFix.kext', 'YogaCursorArb.kext'])
    assert not adds['WhateverGreen.kext']
    args = config['NVRAM']['Add']['7C436110-AB2A-4BBB-A880-FE41995C9F82']['boot-args']
    assert 'ngreenV142=3' in args.split() and '-yogacursorarb' in args.split()
    assert not any(a.startswith(('ngreen-eu=', 'ngreen-l3=')) for a in args.split())
    bundle_binary = REPO / 'EFI/OC/Kexts/NootedGreen.kext/Contents/MacOS/NootedGreen'
    data = bundle_binary.read_bytes()
    assert data == git('show', HEAD + ':EFI/OC/Kexts/NootedGreen.kext/Contents/MacOS/NootedGreen', binary=True)
    signature = audit_file(bundle_binary)
    assert signature['slices'] and all(s['directories'] for s in signature['slices'])
    assert all(d['code_hashes_valid'] and d['x86_page_size_supported']
               for s in signature['slices'] for d in s['directories'])
    patched, patch_checks = data, []
    for patch in config['Kernel']['Patch']:
        if patch['Enabled'] and patch['Identifier'] == 'com.StezzaPilot.NootedGreen':
            patched, offset = apply_to_binary(patched, patch)
            patch_checks.append(dict(comment=patch['Comment'], offset=hex(offset),
                                     limit=patch['Limit'], within_current_limit=True))
    assert len(patch_checks) == 2
    # Do not save the patched image: these are runtime OC edits, not a newly
    # signed on-disk binary. Applying them offline invalidates its code hashes.
    acc, at = image(ACC)
    assert sha(acc) == '2df441e29032a68c0945942996f12337703689c18e3a447d4d15cfdab6858d5f'
    source = git('show', HEAD + ':src/NootedGreen/kern_gen11.cpp')
    rec = source[source.index('static void xeGpuRecInstall(KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size) {'):]
    # Reproduce exact install-time signature windows on the captured Apple
    # accelerator. Dynamic reset-list constructors cannot be validated offline.
    checks = []
    for name, sym, window, exact in [
        ('sGdrst', '__ZN20IGHardwareRingBuffer19resetGraphicsEngineEP17IGHardwareContext', 0x320, None),
        ('sProc', '__ZN20IGHardwareRingBuffer19resetGraphicsEngineEP17IGHardwareContext', 0x320, None),
        ('sFlags', '__ZN20IGHardwareRingBuffer19resetGraphicsEngineEP17IGHardwareContext', 0x320, None),
        ('sS5', '__ZN12IGScheduler515processGPUResetE10IGHwCsTypeP17IGHardwareContext', 0x40, 4),
        ('sCs5', '__ZN26IGHardwareCommandStreamer515processGPUResetEv', 0x40, None),
        ('sRcu', '__ZN16IntelAccelerator17initModeRegistersEv', 0x120, None),
        ('sNp', '__ZN16IntelAccelerator17initModeRegistersEv', 0x120, None),
    ]:
        pattern = byte_array(rec, name)
        address = symbol(acc, sym)
        offset = at(address, window).find(pattern, 0x10) if exact is None else exact
        # Upstream specially permits sCs5 at +0xe as well.
        if name == 'sCs5' and offset < 0 and at(address + 0xe, len(pattern)) == pattern:
            offset = 0xe
        assert offset >= 0 and at(address + offset, len(pattern)) == pattern, name
        checks.append(dict(array=name, symbol=sym, address=hex(address), offset=hex(offset)))
    report = dict(
        upstream=HEAD, config_evidence=str(BASE), config_sha256=sha(config_data),
        config_is_saved_independent_readback_not_live_read=True,
        upstream_ngreen_sha256=sha(data), igpu_only_patch_checks=patch_checks,
        code_directory_hashes_valid=True, signature_scope='Code pages only; not AMFI or bundle resource sealing',
        accelerator_sha256=sha(acc), accelerator_install_byte_checks=checks,
        runtime_reset_lists_checked=False, runtime_routes_checked=False,
        runtime_validation=False, installer_run=False, candidate_deployed=False,
        source_binary_build_equivalence_proven=False,
        findings={
            'gpurec2': 'Present in published source; exact Apple install signatures pass offline. Render-domain recovery only, not a BCS repair.',
            'blitfix': 'Adds non-64-aligned-pitch IOSurface copies to BCS. Yoga has recorded BCS hangs: not selected for blind deployment.',
            'rc6fw': 'New since deployed 17c05c5: forcewake clamping and idle hold release. Requires separate lifetime/locking/power audit; not assumed safe from Dell results.',
            'topology': 'Default remains 96 EU. Saved Yoga hardware reports 96 EU; no ngreen-eu=80 or speculative L3 override.',
            'cursor': 'No validated Yoga pixel/WM/coherency repair established by this update.',
            'typec': 'No complete Yoga ADL-P TC3 transaction integration established by this update.',
        }, external_writes=False, device_access=False)
    OUT.mkdir(exist_ok=True)
    (OUT / 'signature-audit.json').write_text(json.dumps(signature, indent=2) + '\n')
    (OUT / 'deployed-to-upstream-ngreen.patch').write_text(git('diff', '--no-ext-diff', '17c05c5..' + HEAD, '--', 'src/NootedGreen'))
    (OUT / 'upstream-status.md').write_text(git('show', HEAD + ':docs/STATUS.md'))
    (OUT / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
