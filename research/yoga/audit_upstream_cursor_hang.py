"""Read-only upstream/baseline audit; stage a candidate, never deploy it."""
from pathlib import Path
from datetime import datetime, timezone
import hashlib
import json
import plistlib
import shutil
import subprocess
from yoga_fb_match_patch import apply_to_binary
from audit_signature_pages import audit_file

ROOT = Path(__file__).resolve().parent
REPO = Path('/home/willy/Downloads/Hackintosh-XPS13Plus-IrisXe-Tahoe')
BASE = ROOT / 'postmatch-efi-nvram-readonly-20261024-032122/copies/external-esp/EFI/OC'


def git(*args):
    return subprocess.check_output(['git', *args], cwd=REPO, text=True)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def main():
    head = git('rev-parse', 'HEAD').strip()
    assert head == git('rev-parse', 'origin/main').strip()
    assert not git('status', '--porcelain').strip(), 'Do not stage dirty reference'
    config_data = (BASE / 'config.plist').read_bytes()
    assert sha(config_data) == '4ad8d229a94e5bea9cbe6b4670076227e9de42d3c75cbf49d090ab8a50cff642'
    config = plistlib.loads(config_data)
    old = (BASE / 'Kexts/NootedGreen.kext/Contents/MacOS/NootedGreen').read_bytes()
    assert sha(old) == 'e89bb36d1a23559b8d60194e79e620addf7b82f98e2e37d282bad0d82978dd44'
    bundle = REPO / 'EFI/OC/Kexts/NootedGreen.kext'
    binary = bundle / 'Contents/MacOS/NootedGreen'
    data = binary.read_bytes()
    patched = data
    patch_results = []
    for patch in config['Kernel']['Patch']:
        if not patch['Enabled'] or patch['Identifier'] != 'com.StezzaPilot.NootedGreen':
            continue
        patched, offset = apply_to_binary(patched, patch)
        patch_results.append(dict(comment=patch['Comment'], file_offset=hex(offset), limit=patch['Limit'],
            count_in_input=data.count(patch['Find']), byte_length=len(patch['Find'])))
    assert len(patch_results) == 2, 'Expected only the two IGPU-only matching corrections'
    # Require that the published bundle corresponds to the reviewed recovery commit.
    recovered = subprocess.check_output(['git', 'show', '813d2e6:EFI/OC/Kexts/NootedGreen.kext/Contents/MacOS/NootedGreen'], cwd=REPO)
    source_diff = git('diff', '813d2e6..HEAD', '--', 'src/NootedGreen')
    assert not source_diff and data == recovered
    audit = audit_file(binary)
    assert audit['slices'] and all(s['directories'] for s in audit['slices'])
    assert all(d['code_hashes_valid'] for s in audit['slices'] for d in s['directories'])
    stage = ROOT / 'cross-build' / ('upstream-cursor-hang-' + head[:7] + '-' + datetime.now(timezone.utc).strftime('%Y%m%d-%H%M%S'))
    stage.mkdir(exist_ok=False)
    shutil.copytree(bundle, stage / 'NootedGreen.kext')
    (stage / 'baseline-config-DO-NOT-DEPLOY.plist').write_bytes(config_data)
    (stage / 'upstream-status.md').write_text(git('show', 'HEAD:docs/STATUS.md'))
    (stage / 'upstream-install.md').write_text(git('show', 'HEAD:docs/INSTALL.md'))
    (stage / 'source-change.patch').write_text(git('diff', '17c05c5..HEAD', '--', 'src/NootedGreen'))
    (stage / 'signature-audit.json').write_text(json.dumps(audit, indent=2) + '\n')
    report = dict(repo=str(REPO), upstream=head, baseline_config_sha256=sha(config_data),
        deployed_baseline_ngreen_sha256=sha(old), upstream_ngreen_sha256=sha(data),
        same_ngreen_as_previously_reviewed_813d2e6=True,
        igpu_only_matching_patch_compatibility=patch_results,
        candidate_copied_unmodified=True, candidate_deployed=False,
        baseline_evidence=str(BASE), baseline_is_live_device_read=False,
        gms_fix='Preserve existing YogaGMSFix; fresh copied logs verify both runtime patches (verified=1 error=0)',
        cursor_change='No new cursor-function hooks in the upstream diff; V193 hiding is not a corruption repair',
        hang_limit='Upstream documents freezes still occurring after XE-GPUREC; RCU_MODE CCS re-enable described as testing is absent from this published source',
        live_cursor_diagnostic_installed=False, latest_hang_log_collected=True,
        latest_hang_evidence=str(ROOT / 'latest-unified-readonly-20261024-032122'),
        blocker='Fresh evidence identifies BCS context 6; XE-GPUREC is RCS/CCS-only and no validated BCS or cursor repair is ready',
        external_writes=False, runtime_validated=False, stage=str(stage))
    (stage / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
