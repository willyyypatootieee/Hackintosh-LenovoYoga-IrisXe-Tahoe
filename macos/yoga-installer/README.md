# Yoga GPU installer

Download **`Yoga-GPU-installer-20261024.tar.gz`** from the
[release](https://github.com/willyyypatootieee/Hackintosh-LenovoYoga-IrisXe-Tahoe/releases/tag/yoga-checkpoint-20261024).
It contains all 11 captured GPU bundles, **including repaired HookCase**, the
`Install GPU.command` launcher, signing/backup/restore scripts and checksums.
There is no separate repair step.

See the [installation guide](../../readme.md). The payload is from my accelerated
Yoga installation, but this consolidated installer has not been tested on a fresh
macOS install. It retains build 25G241, original external-partition and non-internal
disk checks; do not bypass them. It does not alter EFI or peripherals.

This source directory alone does not contain the full payload or its manifest.
The release archive is the installable package. `repaired/` preserves the same
corrected HookCase separately for source-tree recovery, not as a second installer.
