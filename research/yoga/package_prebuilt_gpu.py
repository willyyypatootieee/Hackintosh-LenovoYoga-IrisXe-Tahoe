"""Package existing Linux-built bundles for a no-Xcode macOS install.

No physical disk access. Never publishes or uploads the proprietary payload.
"""
from pathlib import Path
import hashlib
import json
import shutil
import tarfile
import sys

ROOT = Path(__file__).resolve().parent
STACK = ROOT / 'cross-build/gpu-stack-assembled'
NAME = sys.argv[1] if len(sys.argv) > 1 else 'YogaXeGPU-no-Xcode'
assert Path(NAME).name == NAME and NAME.startswith('YogaXeGPU-no-Xcode')
OUTPUT = ROOT / 'cross-build' / NAME
assert not OUTPUT.exists(), 'Do not overwrite an existing package'

# Check that staged payload files still match the assembly manifest.
manifest = json.loads((STACK / 'manifest.json').read_text())
for item in manifest:
    path = STACK / item['path']
    assert hashlib.file_digest(path.open('rb'), 'sha256').hexdigest() == item['sha256'], str(path)
assert json.loads((STACK / 'llvm-import-verification.json').read_text())['passed']
checks = json.loads((STACK / 'independent-codeslot-verification.json').read_text())
assert len(checks) == 29 and all(x['passed'] for x in checks)
OUTPUT.mkdir()
for folder in ['Library', 'System']:
    shutil.copytree(STACK / folder, OUTPUT / 'payload' / folder, symlinks=True)
for source, dest in [('install_prebuilt_gpu.sh', 'install_prebuilt_gpu.sh'),
                     ('prepare_native_signatures.sh', 'prepare_native_signatures.sh'),
                     ('restore_prebuilt_gpu.sh', 'restore_prebuilt_gpu.sh'),
                     ('launch_prebuilt_gpu.command', 'Install GPU.command')]:
    shutil.copy2(ROOT / source, OUTPUT / dest)
    (OUTPUT / dest).chmod(0o755)
checksums = []
for path in sorted((OUTPUT / 'payload').rglob('*')):
    if path.is_file() and not path.is_symlink():
        checksums.append(hashlib.file_digest(path.open('rb'), 'sha256').hexdigest() + '  ' + str(path.relative_to(OUTPUT)))
(OUTPUT / 'SHA256SUMS').write_text('\n'.join(checksums)+'\n')
(OUTPUT / 'README.txt').write_text('''Yoga Iris Xe prebuilt GPU payload — Tahoe 25G241 ONLY

STATUS: built and packaged on Linux; NOT deployed or runtime-validated.

No Xcode, Command Line Tools, Homebrew, downloads or compilation required.
Extract this archive in your macOS home folder (APFS), then open
"Install GPU.command". Authorization is via macOS sudo. No manual build commands.
Do not run the upstream source installer: it would try to rebuild the stack.

The installer first checks the OS build, external physical-store partition UUID,
payload checksums and writable destinations. It copies the verified payload
to a disposable temporary folder, re-signs it with Tahoe's built-in codesign,
and strictly verifies every binary and bundle before replacing installed files.
No verification failure is ignored. Original extracted payload is unchanged.
It refuses any root volume not on the previously authorized external partition.
If that partition was recreated during installation, the guard must be updated
after checking the disk identity; do not remove it or bypass it.

Existing destination bundles are backed up before replacement, with a restore
script and log under /Library/Application Support/YogaXeGPU/backups/.
kmutil registration failures are NOT ignored. macOS may require extension
approval. A successful kmutil return is not proof of approval or GPU loading.

This package does NOT change EFI, boot arguments, CPU, Wi-Fi, trackpad, audio,
USB maps or ACPI. It does NOT reboot. Active EFI must be reviewed separately
against the installed system before an accelerated test boot.

Version 2 repairs the stale detached resource seal found in the first package
using native signing, not by relaxing signature checks. Native signing is not
compilation and does not require developer tools. native-signing-v2.log is
saved beside the installer even if preflight fails. Stop on any error.
Some upstream shared bundles name an absent main executable. Their acceptance
by Apple codesign has not been established here.

Source: JulioNectar/Hackintosh-XPS13Plus-IrisXe-Tahoe, fixed HookCase and MTL17 /
xe.bundle reference patches; existing local 16.0.32 donor bundles.
Known other-machine reports: WAIT_ON_SCANLINE hangs in Displays, Trackpad
settings and Photo Booth. Preserve the working nonaccelerated boot fallback.
This archive contains local Apple donor binaries: do not publicly redistribute.
''')
archive = OUTPUT.with_suffix('.tar.gz')
assert not archive.exists()
with tarfile.open(archive, 'w:gz', compresslevel=6) as tar:
    tar.add(OUTPUT, arcname=OUTPUT.name)
report = dict(archive=str(archive), bytes=archive.stat().st_size,
              sha256=hashlib.file_digest(archive.open('rb'), 'sha256').hexdigest(),
              payload_files=len(checksums), deployed=False, runtime_validated=False)
archive.with_suffix(archive.suffix+'.sha256').write_text(report['sha256']+'  '+archive.name+'\n')
(ROOT / ('cross-build/'+NAME+'-status.json')).write_text(json.dumps(report, indent=2)+'\n')
print(json.dumps(report, indent=2))
