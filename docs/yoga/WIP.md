# Yoga progress — 2026-10-24

## Working baseline

I confirmed Iris Xe acceleration on my Yoga after repairing driver matching,
HookCase import/signature handling and the 60 MiB GMS paths. Both framebuffer
and accelerator runtime GMS corrections were present in captured boot logs.
The Yoga ACPI/peripheral setup, CPUID/topology correction, RTX-off SSDT,
`SSDT-PLUG-ALT.aml`, `ngreenV142=3`, and verbose `-v` remain in the EFI.
WhateverGreen and the minimal `SSDT-PLUG.aml` remain disabled.

SimpleHDA was added alongside AppleALC/layout11. OpenCore pre-picker debug noise
was disabled; macOS verbose boot remains enabled. These are separate settings.

## Cursor and stability — unresolved

YogaCursorArb is a reversible experimental Lilu plugin, not a confirmed visual
fix. GPU restart reports show stamp timeouts and BCS context6 hangs; there is
no per-submission retirement proof establishing a completed stability repair.
Browser lag and cursor stripes must not be described as fixed.

## External monitor — unresolved

Measured Linux route: TC3 / PHY TC3 / Port F / Apple DDI5, DP-alt through an HDMI
adapter. The deployed external connector instead selects DDI-B. Correcting the
row alone does not supply ADL-P ownership, power, clocks, PHY or link training.

Implemented research components under `research/yoga/yoga-typec/` include:

- ADL-P acquisition/cleanup transaction and resumable error handling;
- modular FIA lane planning, DKL mode/signal/PLL plans and DDI clock sequencing;
- bounded DP1.x training with actual-count DPCD transfers;
- kernel-SDK-compilable mapping, AUX and DPCD implementation units;
- strict retained-object/BAR/cache checks, original `_gController` provenance,
  recursive command-lock use, short-transfer rejection and bounded DEFER retries;
- host ASan/UBSan tests and comparisons against pinned Linux helpers.

The last pre-publication local regression recorded 68 passing tests. **Those
tests do not demonstrate monitor output**, and the public sanitized snapshot
does not include the captured Apple driver/kernel inputs needed by all audits.

Still missing: concrete native start/stop/remap/sleep admission, retained port
and configuration provenance, core/DC6/power coordination, exclusion against
all native shared-register writers, guarded hook activation, mutation journals,
hardware readback and atomic connector publication. There is **no complete
loadable USB-C repair** in this checkpoint.

## WhateverXe review

Upstream reviewed at `c6cd8ec5ec5bd64ad3c8a852a30d1f537d819fb1`, Graphics at
`e83018e76c0f39f0f870ff38dcc304fb357c3485`. Real TGL DP-alt support exists,
but `46A6` has no automatic backend, and Display13 TC ownership rejects DP-alt.
TGL TC3 PLL `0x46038` differs from ADL-P `0x46048`; ownership registers differ.
WEX's accepted framebuffer and provider ABI also differ from this Yoga stack.
Nothing from those packages was deployed.

The local WhateverXe source snapshot is revision
`e481a1ef037d1ca622f51479603af06dbc565497`, with the existing test include edit
preserved. It has exact 46A6/D0 GT resource guards and experimental eDP work,
not completed USB-C support. It is isolated under `research/`, not enabled in EFI.

## Validation required after restore

1. Boot, confirm the intended kexts load, and verify both GMS corrections.
2. Confirm native display image and acceleration, not just an IORegistry name.
3. Test speaker/headphone/mic, wireless and inputs independently.
4. Stress ordinary window/browser activity and inspect fresh GPU restart reports.
5. Only claim external display support after the monitor actually shows a stable
   image; boot, unplug/replug and sleep behavior need separate tests.
