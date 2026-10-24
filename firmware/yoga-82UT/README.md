# Yoga 82UT firmware research capture

BIOS: **KRCN27WW**. Capture originated from Linux on this Yoga.

- `acpi/`: selected DSDT/SSDT and hardware-description tables.
- `decompiled/`: available iasl decompilations, not newly generated firmware.
- `i915_vbt.bin`: extracted Intel VBT.
- `i915_opregion.bin`: captured Intel graphics OpRegion.

These files describe wiring and firmware interfaces. They are **not a full BIOS
or SPI dump, not a BIOS update, and must never be flashed**. No raw SPI image was
found in the inspected capture. No new flashrom/programmer operation was run.

Raw DMI/SMBIOS, MSDM (OEM product key), UEFI/NVRAM/config-table dumps, TPM tables
and raw BIOS-variable backups are intentionally excluded. Selected captures were
screened for known machine identifiers; no claim is made that every opaque byte
has a documented format. Keep the original raw capture private.
