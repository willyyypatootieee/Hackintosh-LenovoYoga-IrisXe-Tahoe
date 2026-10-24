# Reinstall / recovery checkpoint

This preserves the accelerated installation I've confirmed on my Yoga, with known failures.
It is **not** a clean-install certification. Do not format your only backup.

## Before erasing Linux or macOS

1. Download the EFI, source and GPU recovery archives from this fork's release.
2. Save them and their SHA-256 files on storage that you will **not** erase.
3. Verify with `shasum -a 256 -c FILE.sha256` on macOS or `sha256sum -c FILE.sha256`
   on Linux. Also keep your own exact EFI and personal SMBIOS identity privately.
4. Keep a macOS installer and a second bootable EFI. Do not assume GitHub alone
   preserves your operating systems, personal data or an entire APFS volume.

## EFI snapshot

`Yoga-EFI-sanitized-20261024.tar.gz` matches the public `EFI/` tree. Its active
configuration came from the read-only external ESP; source config SHA-256:

```text
9078149a667c94d17519b494a4d19df9c66ccc8f751370857b20c7822b725a11
```

Public sanitization clears SMBIOS serial/MLB/UUID/ROM and Bluetooth controller
identity. **The public config hash therefore differs.** See `EFI-SHA256.json`
for public file hashes. Supply your own valid identity before use. Leave the
graphics arguments, GMS corrections, CPU settings, Yoga ACPI and kext order
intact unless deliberately requalifying them. Do not copy the Dell reference EFI.

Historical configs under `research/yoga/historical-configs/` are sanitized old
experiments, **not alternative known-good configurations**.

## Existing macOS iGPU installer

The released original packages are preserved rather than silently rebuilt:

- `Yoga-GPU-installer-original.tar.gz`: original first package, superseded, historical only.
- `Yoga-GPU-installer-v2.tar.gz`: native-signature preparation and GPU installation.
- `Yoga-HookCase-repair-v3.tar.gz`: targeted repair of the v2
  HookCase `___cxa_atexit` link failure. **Do not use v2 alone as the final stack.**
- `Yoga-GPU-installed-capture-20261024.tar.gz`: named captured installed graphics
  bundles and repaired HookCase, for recovery/research, **not a new installer**.

See the [installation guide](../../readme.md) for the sequence and stop conditions.
Archive filenames have been cleaned up; the original internal folder names and
payloads are unchanged so their existing checks remain intact.

The existing v2 launcher is `Install GPU.command`; the v3 launcher is
`Repair HookCase.command`. Read each included README and script first. They use
macOS built-in tools, keep backups, perform signature checks and invoke `kmutil`;
they do not supply the Yoga EFI or automatically reboot.

**Important:** the scripts intentionally accept only Intel Tahoe build **25G241**
and the original external APFS physical-store partition UUID. Formatting changes
that identity. A refusal after reinstall is expected; do not delete target checks
or point them at an internal disk just to continue. The current scripts and
archives are a preserved recovery workflow, not a generic new-disk installer.
Review `diskutil info -plist /` and the actual external APFS physical store before
making any targeted adaptation. This repository has not validated that adaptation.

The v3 repair also requires my original incomplete v2 backup at
`/Library/Application Support/YogaXeGPU/backups/20261023-221106`, including its
`INSTALL-INCOMPLETE` marker and matching `___cxa_atexit` error log. A new v2 run
creates a different backup timestamp, so v3 is not automatically applicable to it.
Do not rerun v2 over a working repaired stack or fake these prerequisites.

EFI-installed YogaGMSFix and matching framebuffer/GPU matching are also required;
an old package plus an unrelated fresh EFI is not the known working baseline.

The installed capture is not a bootable macOS backup and does not establish
post-reinstall AuxKC approval/signing. Do not bulk copy its directories over a
running system without backups and a reviewed installation procedure.

## Code recovery

`Yoga-code-and-WIP-20261024.tar.gz` contains this fork's source, EFI, documentation,
sanitized firmware research and the local WEX development snapshot. Some archived
audit/build/deployment scripts require local paths, exact captured drivers,
MacKernelSDK, Lilu and toolchains not bundled in the public tree. Machine serial
guards in public source are deliberately redacted and must fail closed until
reviewed. Original source copies remain in my private local backup.

## Known failures remain after backup

USB-C→HDMI, cursor corruption, GPU hangs and browser lag are unresolved. Backing
up files does not turn these into working features. See [WIP.md](WIP.md).
