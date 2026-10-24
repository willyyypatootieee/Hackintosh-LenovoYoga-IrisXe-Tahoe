# How to install my Lenovo Yoga Iris Xe setup

I'm **Willy Patootie ([@willyyypatootieee](https://github.com/willyyypatootieee))**.
I've confirmed working Iris Xe acceleration on my **Lenovo Yoga 82UT, i7-12700H,
96-EU Iris Xe 8086:46A6**, running **macOS Tahoe 26.7.1 / build 25G241** on an
external SSD. These instructions explain the files I've preserved and their
installation limits. They do not promise a tested clean installation on a new disk.

**Known issues:** USB-C → HDMI, GPU hangs, browser lag and cursor corruption
are still unresolved. See [status and hardware](README.md) and [WIP](docs/yoga/WIP.md).

## 1. Download the right files

Download these archives **and their matching `.sha256` files** from
[the release](https://github.com/willyyypatootieee/Hackintosh-LenovoYoga-IrisXe-Tahoe/releases/tag/yoga-checkpoint-20261024):

| File | Use |
|---|---|
| `Yoga-EFI-sanitized-20261024.tar.gz` | My active Yoga EFI, with personal identifiers removed |
| `Yoga-GPU-installer-v2.tar.gz` | Matching graphics installation package; **not the final stack by itself** |
| `Yoga-HookCase-repair-v3.tar.gz` | Targeted correction for the v2 HookCase failure |
| `Yoga-GPU-installed-capture-20261024.tar.gz` | Captured installed GPU files and repaired HookCase, for comparison/recovery; not an installer |

The original installer is historical. The source/WIP and firmware archives are
backups for development, not extra components to install. Do **not** install
the experimental WhateverXe tree or the Dell reference EFI as part of this setup.

Keep downloads, your current EFI, your personal SMBIOS details and a bootable
fallback on another disk before making changes. This is not a full macOS backup.

In macOS Terminal, change to the folder containing your downloads, then verify:

```bash
shasum -a 256 -c Yoga-EFI-sanitized-20261024.tar.gz.sha256
shasum -a 256 -c Yoga-GPU-installer-v2.tar.gz.sha256
shasum -a 256 -c Yoga-HookCase-repair-v3.tar.gz.sha256
shasum -a 256 -c Yoga-GPU-installed-capture-20261024.tar.gz.sha256
```

Every file must report `OK`. Stop if a checksum fails.

## 2. Check the installation target before running anything

The installer checks **Intel macOS build 25G241**, the **original external APFS
physical-store partition UUID**, and that the target is **not internal**. It is
not a generic installer for other laptops, other macOS builds or arbitrary disks.

```bash
uname -m
sw_vers -buildVersion
diskutil info /
diskutil apfs list
```

Expected CPU architecture: `x86_64`; expected build: `25G241`. The disk commands
are inspection only. Read the included scripts before authorizing them.

**If you formatted/repartitioned the disk, stop here.** Its identity has changed.
The preserved scripts will refuse it until their target checks are reviewed and
adapted to the verified external installation. Do not delete checks or substitute
an internal disk. I have not validated that fresh-install adaptation, and these
archives do not install macOS itself.

## 3. Prepare the matching Yoga EFI

Extract `Yoga-EFI-sanitized-20261024.tar.gz` to a staging folder, not directly over
an existing EFI. Back up the existing EFI first, and use a separate bootable USB
or the intended external disk's EFI partition for testing. Do not modify an
unrelated internal disk's EFI.

1. Use the archive's `EFI/BOOT` and `EFI/OC`, or the identical public [`EFI/`](EFI/)
   tree. The Dell EFI under `reference/` is **not** the Yoga configuration.
2. In `EFI/OC/config.plist`, supply your own valid `PlatformInfo → Generic`
   `SystemSerialNumber`, `MLB`, `SystemUUID` and six-byte `ROM`. These values were
   deliberately cleared in the public copy. Keep your identity private and keep
   the existing product model unless you deliberately requalify the configuration.
3. Keep the Yoga CPU settings, RTX-off SSDT, input/audio setup, boot arguments,
   Lilu, NootedGreen and YogaGMSFix intact. Both GMS corrections are required.
   Do not apply the upstream 80-EU settings to this 96-EU GPU.
4. YogaCursorArb is an existing reversible experiment in this captured EFI;
   it is **not** a proven cursor fix. Do not enable additional WIP plugins.
5. Check the staged config with the matching OpenCore `ocvalidate` before using
   it. The repository's hash/path test is not a replacement for `ocvalidate`.
6. Copy the prepared `EFI` folder to the intended test EFI partition only after
   confirming the device and keeping a fallback. Do not regenerate it with
   the inherited Dell `make-efi.sh`.

The published EFI hashes describe the sanitized download. Editing your identity
will intentionally change the config hash.

## 4. Understand the v2 → v3 graphics installation sequence

Extract both installer archives into your macOS home folder on APFS. The outer
archive names are cleaned up; their original internal folder names are retained.
Find the launchers by name rather than assuming the extracted folder name.

**Already running my working accelerated stack? Do not reinstall v2.** It contains
the older defective HookCase and can overwrite the repaired one. Keep the files
as a backup instead.

For the original authorized external installation, the preserved sequence is:

1. Open **`Install GPU.command`** from the v2 package. Read its prompt, type
   `INSTALL` only for the verified target, and authorize it. It validates payloads,
   signs them using macOS tools, backs up replaced files and registers the kexts.
2. **Do not reboot straight into a GPU test after a v2 link failure.** The known
   defect is HookCase's `___cxa_atexit` import. Save the full log. A different
   failure is not covered by the targeted repair.
3. The preserved v3 repair is **specific to my original failed v2 installation**:
   it requires the backup directory
   `/Library/Application Support/YogaXeGPU/backups/20261023-221106`, its
   `INSTALL-INCOMPLETE` marker, and the matching error in `install.log`.
   It does **not** automatically accept a new installation's backup timestamp.
   If these prerequisites are absent, stop; don't fake them or bypass checks.
4. Only when those prerequisites match, open **`Repair HookCase.command`** from
   the v3 package, type `REPAIR`, and authorize it. It replaces only HookCase,
   verifies the other GPU bundles, preserves the original backup and runs `kmutil`.
5. Continue only on the repair's explicit `SUCCESS` or its exact recognized
   `REBOOT REQUIRED` result. Stop on any other signing or `kmutil` error and
   retain the logs. Approve extensions in macOS Privacy & Security if requested,
   then restart into the intended external Tahoe installation using the Yoga EFI.

This sequence documents how the preserved packages relate to my working stack;
**it is not a newly qualified one-click clean installer**. The captured installed
GPU archive contains repaired files, but blindly copying it over macOS does not
perform native signing, extension approval or kernel-collection registration.
The scripts in [`macos/yoga-installer/`](macos/yoga-installer/) alone are also
insufficient: they do not contain the complete installer payload and manifests.

## 5. Verify after reboot

- Check that the intended HookCase, TGL framebuffer/accelerator, NootedGreen and
  YogaGMSFix load, and that both GMS corrections appear in fresh boot logs.
- Check actual accelerated desktop/Metal behavior, not just the GPU name in
  System Information. My earlier working result is not proof of a new boot.
- Test the internal display, inputs, audio and wireless separately.
- Check fresh GPU restart reports during ordinary browser/window activity.
- Do not expect USB-C → HDMI to work; that implementation is still WIP.

## 6. If something goes wrong

Boot your fallback EFI rather than repeatedly changing unrelated kexts. Preserve
the failure logs before retrying. Installer backups and their generated restore
scripts are under `/Library/Application Support/YogaXeGPU/backups/`; targeted
HookCase repair logs/backups are under `/Library/Application Support/YogaXeGPU/repairs/`.
Read the restore script for the **matching** backup before authorizing it; do not
mix backup directories or delete the original pre-install files.

More detail: [recovery notes](docs/yoga/RECOVERY.md), [source/WIP status](docs/yoga/WIP.md)
and [contributors](CONTRIBUTORS.md). Huge thanks to
**[JulioNectar's original project](https://github.com/JulioNectar/Hackintosh-XPS13Plus-IrisXe-Tahoe)**
for the foundation of this port.
