# WhateverXe

[日本語](README_ja.md)

WhateverXe is a Lilu plugin that adapts Apple graphics drivers for selected
Intel Xe-LP integrated GPUs. It is derived from WhateverGreen.

This project is experimental and requires the Xe root patch provided by the
[OpenCore Legacy Patcher fork](https://github.com/b00t0x/OpenCore-Legacy-Patcher).

## Compatibility

The current target is **macOS Big Sur build 20G1443**.

| GPU family | Device IDs with an implemented backend |
| --- | --- |
| Tiger Lake | `9A40`, `9A49`, `9A78` |
| Alder Lake-N / Twin Lake | `46D0`–`46D4` |
| Alder Lake-P GT2 (experimental) | `46A6`, PCI revision `0C` only |

Support varies by system and connector layout. The table includes implemented
backends, not a guarantee for every machine. See [device selection](docs/device-selection.md)
for detailed selection conditions and overrides.

Dual external 1920×1080 at 60 Hz output, Metal/OpenGL, hardware HEVC and display
sleep/wake have been tested on Tiger Lake and Alder Lake-N. Hardware H.264
encode/decode has also been tested on Tiger Lake. The Alder Lake-P entry is a
strict 46A6 D0 Display-13 bring-up profile; it requires live hardware validation
and is not a compatibility claim for other Alder Lake-P systems. A guarded internal-
eDP lifecycle candidate exists only for the exact firmware panel route (DDI A,
pipe A, AUX `0x40`, native DP mode). It is opt-in experimental support, not a
compatibility claim for other internal displays, sleep/wake, USB-C, or connector
layouts. Three simultaneous displays and higher resolutions require further validation.

Download the **RELEASE** ZIP from [Releases](https://github.com/b00t0x/WhateverXe/releases). The DEBUG ZIP is for troubleshooting.

## Required components

| Component | Role |
| --- | --- |
| Lilu | Runtime required by WhateverXe |
| WhateverXe.kext | Load through OpenCore after Lilu |
| [OpenCore Legacy Patcher fork](https://github.com/b00t0x/OpenCore-Legacy-Patcher) | Apply its Xe root patch to install the graphics stack |
| [AMFIPass v1.4.1 Mod](https://github.com/b00t0x/OpenCore-Legacy-Patcher/blob/mod/payloads/Kexts/Acidanthera/AMFIPass-v1.4.1-Mod-RELEASE.zip) | Install through OpenCore for the modified root-patched graphics stack |

Download the OCLP fork from its [Releases page](https://github.com/b00t0x/OpenCore-Legacy-Patcher/releases).
OCLP installs its root components and updates the kernel collection.

Extract `AMFIPass.kext` from the linked **Mod** archive, place it in
`EFI/OC/Kexts`, and enable it in `Kernel > Add`. Replace an existing AMFIPass
with this Mod build; do not load both copies.

WhateverGreen and WhateverXe must not be enabled together, including in
mixed-GPU configurations.

## Configuration

GPU selection is automatic from the PCI device ID. Connector routing is read
from firmware VBT where supported; fixed HDMI and fixed DisplayPort layouts
normally need no manual wiring properties. Advanced `device-id` overrides are
described in [device selection](docs/device-selection.md).

### DVMT

Set **DVMT Pre-Allocated to 96MB** in the BIOS/UEFI settings where possible.

If it is fixed at **60MB**, add `-igfxdvmt` to `boot-args` and set both properties
below on the iGPU under OpenCore's `DeviceProperties > Add`
(usually `PciRoot(0x0)/Pci(0x2,0x0)`).

| Property | Type | Value (hex) |
| --- | --- | --- |
| `framebuffer-patch-enable` | Data | `01000000` |
| `framebuffer-stolenmem` | Data | `0000C003` |

These Data values use little-endian byte order. `-igfxdvmt` corrects the capacity
calculation, while the properties limit the display memory pool to fit within
60MB. Neither setting increases the memory allocated by the firmware.
With 96MB, these DVMT-specific settings are unnecessary.

### Required boot arguments

Add the following to OpenCore's `boot-args`:

```text
-allow3d -disablegfxfirmware
```

| Argument | Purpose |
| --- | --- |
| `-allow3d` | Enable the Apple driver's 3D path. |
| `-disablegfxfirmware` | Disable the Apple driver's graphics-firmware loading path for the current backend. |
| `-wex-nopanel` | Additionally required on Tiger Lake systems without an internal display. Alder Lake-N / Twin Lake currently builds an external-display topology automatically and does not use this flag. |

No family-selection or per-feature enable arguments are required for Alder Lake-N /
Twin Lake. The strict Alder Lake-P `46A6` D0 eDP candidate additionally requires
its explicit optional argument below.

### Optional boot arguments

| Argument | Purpose |
| --- | --- |
| `gfxrst=` | Boot framebuffer handoff: 0 automatic (default), 1 reset, 2 copy, 3 clear, 4 disable. |
| `-wexdbg` | Enable debug logging in a debug build. |
| `-wexoff` | Disable WhateverXe. |
| `-wexbeta` | Bypass the plugin's OS-version check. Exact kernel/driver checks still apply. |
| `-wex-tgl-off` | Disable the Tiger Lake path. |
| `-wex-adlp-any-pipe` | Experimental `46A6` D0-only admission: allow one validated firmware scanout on Display-13 pipe B or C and retain that measured pipe through boot handoff, power, and IRQ validation. Keep `-allow3d` and `-disablegfxfirmware`; use only for a single external-display experiment and remove it after collecting the preflight result. |
| `-wex-adlp-edp` | Experimental exact-`46A6` D0 internal-panel lifecycle candidate. Requires the firmware VBT panel route DDI A / pipe A / AUX `0x40` / native DP mode and the required arguments above. It never enables HDMI, Type-C, or an alternate pipe; remove it to return to the framebuffer-only fallback. |

## Roadmap

### Planned

- macOS Monterey through Tahoe support using OCLP's capabilities
- Alder Lake and Raptor Lake S/H/P/U support
- USB-C display output
- General internal-display support

### Not Planned

- Xe-LPG support
- Xe2 and later architectures

## Build and test

Use macOS with Xcode command-line tools, Python 3.9 or newer, MacKernelSDK and
Lilu's development headers/library. Place `MacKernelSDK/` and `Lilu.kext/` in the
repository root, or pass their paths with `--sdk` and `--lilu`.

```sh
python3 tools/build.py --release
python3 tools/test.py
```

Kexts and a source manifest are written to `build/`. Optional donor replay tests
accept `--donor /path/to/AppleIntelTGLGraphicsFramebuffer` and
`--accelerator /path/to/AppleIntelTGLGraphics`, using the exact binaries accepted
by each test. CPU tests do not establish physical GPU compatibility.
WhateverGreen source is not a build dependency.

## License and credits

WhateverXe retains WhateverGreen's copyright notices and BSD-3-Clause license.
See [LICENSE.txt](LICENSE.txt). Lilu and MacKernelSDK are separate dependencies.
