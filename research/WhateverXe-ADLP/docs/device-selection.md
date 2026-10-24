# Device selection and DeviceProperties

WhateverXe selects the GPU backend from a PCI device table. No CPU model or
machine-name setting is required.

## Automatic selection

By default, the physical Intel GPU device ID selects the backend. The implemented
IDs are Tiger Lake `9A40`, `9A49`, `9A78`, Alder Lake-N / Twin Lake
`46D0`–`46D4`, and the experimental Alder Lake-P GT2 `46A6` at PCI revision
`0C` only. Alder Lake-N / Twin Lake require PCI revision 0. OS, driver and
hardware-resource checks still apply after selection.

Other entries in the table supply identification and display names only. A
listed ID does not imply an implemented backend or a tested machine. See the
[compatibility summary](../README.md#compatibility) for the current scope.

The `46A6` entry is deliberately a physical-identity profile: an override does
not qualify another PCI device or revision as Alder Lake-P GT2.

## Advanced override: device-id

To select an existing backend explicitly, set `device-id` on the integrated
GPU in OpenCore DeviceProperties. Its value must be **Data containing exactly
four little-endian bytes**, with a 16-bit device ID and two zero bytes.

For example, Data `D2460000` selects device ID `0x46D2`. This can select a backend
for an unlisted physical Intel ID, but it does not establish hardware compatibility.
The physical PCI identity and revision remain available to hardware checks.
EU counts, memory sizes and connector wiring are determined independently and
are not fabricated by the override.

Malformed values, zero/FFFF, unknown target IDs, identification-only targets and
non-Intel physical devices are rejected. An invalid override does not silently
fall back to the physical ID. Remove an obsolete ICL or other unsupported spoof
to use automatic selection. A restart is required after changing the selection.

Do not use an override merely to change the displayed GPU name.

## Display name

A user-supplied `model` property is preserved. Otherwise, the name associated
with the physical ID is used; when that ID is unknown, the selected ID's name is
used instead. Names are descriptive labels and do not control functionality or
EU counts. SKU and system configuration can affect Intel's marketing name.
Metal may still report the Apple driver's internal name.

PCI ID/family groupings were checked against Linux v6.12's i915 tables; individual
marketing names are not a claim of independently verified SKU branding.
