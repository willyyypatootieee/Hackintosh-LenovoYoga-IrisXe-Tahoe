# Yoga installation and recovery files

I've confirmed working acceleration on my Yoga. Download these two packages from
the [release](https://github.com/willyyypatootieee/Hackintosh-LenovoYoga-IrisXe-Tahoe/releases/tag/yoga-checkpoint-20261024):

1. **`Yoga-EFI-sanitized-20261024.tar.gz`** — my active EFI with personal
   SMBIOS/Bluetooth identifiers removed. Supply your own valid identity.
2. **`Yoga-GPU-installer-20261024.tar.gz`** — the complete captured 11-bundle GPU
   stack, **including repaired HookCase**, with one `Install GPU.command` launcher.

Download both matching `.sha256` files and verify them before extracting. Read
the **[installation guide](../../readme.md)** for checks, installation and rollback.
The old original/v2/v3 downloads have been removed from the public release to
avoid installing defective HookCase or following obsolete repair prerequisites.
Original archives remain in my local historical backup, not as install choices.

## Limits

The payload matches my captured installed GPU stack. The consolidated installer
is a new packaging of those files; **a fresh macOS installation has not been
tested**. It keeps the Intel Tahoe **25G241**, original external physical-store
partition UUID and non-internal-target guards. Formatting changes that identity;
stop and review the verified external target rather than bypassing protections.
No fixed old backup timestamp or separate HookCase repair is required anymore.

The installer verifies checksums, signs disposable payload copies with macOS
`codesign`, verifies them, backs up all destinations, then installs only the
11 named GPU bundles and runs `kmutil`. It stops on unexpected failures and does
not reboot automatically, alter EFI, remount System or change peripherals.

Backups and their restore script are under
`/Library/Application Support/YogaXeGPU/backups/`. Preserve these and your fallback
EFI. The package is not a full macOS/APFS backup. Do not erase your only copy.

## Provenance and source

The original read-only EFI config SHA-256 was
`9078149a667c94d17519b494a4d19df9c66ccc8f751370857b20c7822b725a11`.
Public sanitization changes the config hash; `EFI-SHA256.json` describes the
public download. Other EFI files match the captured active files.

`Yoga-code-and-WIP-20261024.tar.gz` preserves source, EFI, docs, reviewed firmware
research and isolated experiments. Historical configurations and development
scripts under `research/` are not alternative install choices. Some audits need
private captured inputs/toolchains not bundled here. Exact private EFI and raw
identifying firmware captures are not published.

HDMI, cursor corruption, GPU hangs and browser lag remain unresolved. See [WIP](WIP.md).
