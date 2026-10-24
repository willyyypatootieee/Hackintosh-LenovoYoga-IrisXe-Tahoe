"""Versioned local repair archive; no disk access or Apple payload publication."""
from pathlib import Path
import hashlib
import json
import shutil
import struct
import tarfile

ROOT = Path(__file__).resolve().parent
BUILD = ROOT / 'cross-build/fixed-hookcase-kernel-dtors-v3'
NAME = 'YogaXeGPU-no-Xcode-v3-HookCase-repair'
OUT = ROOT / 'cross-build' / NAME
assert not OUT.exists() and not OUT.with_suffix('.tar.gz').exists()
audit = json.loads((BUILD / 'installed-tahoe-import-audit.json').read_text())
assert audit['passed'] and len(audit['results']) == 3
binary = BUILD / 'HookCase.kext/Contents/MacOS/HookCase'
digest = hashlib.file_digest(binary.open('rb'), 'sha256').hexdigest()
assert digest == audit['results'][0]['sha256']
assert struct.unpack_from('<I', binary.read_bytes(), 12)[0] == 11
assert '___cxa_atexit' not in (BUILD / 'undefined-symbols.log').read_text()
shutil.copytree(BUILD / 'HookCase.kext', OUT / 'payload/HookCase.kext')
for source, dest in [('repair_hookcase_v3.sh', 'repair_hookcase_v3.sh'),
                     ('launch_hookcase_repair_v3.command', 'Repair HookCase.command')]:
    shutil.copy2(ROOT / source, OUT / dest)
    (OUT / dest).chmod(0o755)
for name in ['installed-tahoe-import-audit.json', 'verification.json', 'section-headers.log', 'undefined-symbols.log']:
    shutil.copy2(BUILD / name, OUT / name)
(OUT / 'README.txt').write_text('''Yoga v3: targeted repair of the HookCase ___cxa_atexit build defect

For installed external Tahoe 25G241 after the specific v2 failure on this Yoga.
Extract into your macOS home folder. Open "Repair HookCase.command", type
REPAIR, and authorize. No Xcode, CLT, Homebrew, downloads or compilation.

Replaces ONLY /Library/Extensions/HookCase.kext. The other ten signed GPU
bundles are verified but not changed. Original backup 20261023-221106 is
preserved; the previous HookCase and new log go in YogaXeGPU/repairs/.
No EFI, boot argument, CPU, Wi-Fi, trackpad, audio, USB or ACPI changes.

Fix: prevent LLVM 23's backend from rewriting real kernel destructor entries
into user-space __cxa_atexit registration. Keeps __mod_term_func and the real
MetaClass destructor. No dummy runtime symbols or deleted destructors.
367 HookCase imports and 614 framebuffer imports resolve by name in exact
installed Tahoe dependencies. Accelerator: 1194 resolve by name; six inherited
vtable slots were checked against actual installed parent classes for native
C++ ABI patching. This static audit is NOT native kmutil validation.

Native Apple signing and strict checks are mandatory. kmutil errors are NOT
ignored. Only if the command explicitly reports SUCCESS: approve extensions
if requested, then restart directly into installed external Tahoe. Metal is
not proven until that boot. If it fails, retain the full repair.log; do not
reboot for a GPU test or rerun the old v2 installer.
''')
checksums = []
for path in sorted(OUT.rglob('*')):
    if path.is_file():
        checksums.append(hashlib.file_digest(path.open('rb'), 'sha256').hexdigest() + '  ' + str(path.relative_to(OUT)))
(OUT / 'SHA256SUMS').write_text('\n'.join(checksums) + '\n')
archive = OUT.with_suffix('.tar.gz')
with tarfile.open(archive, 'w:gz', compresslevel=6) as tar:
    tar.add(OUT, arcname=OUT.name)
report = dict(archive=str(archive), bytes=archive.stat().st_size,
              sha256=hashlib.file_digest(archive.open('rb'), 'sha256').hexdigest(),
              hookcase_sha256=digest, static_import_audit=True,
              native_kmutil_validated=False, runtime_validated=False, deployed=False)
archive.with_suffix(archive.suffix + '.sha256').write_text(report['sha256'] + '  ' + archive.name + '\n')
(ROOT / 'cross-build' / (NAME + '-status.json')).write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report, indent=2))
