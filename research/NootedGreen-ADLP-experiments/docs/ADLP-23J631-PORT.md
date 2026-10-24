# Alder Lake-P / Sonoma 23J631 port notebook

## Target system

| Item | Observed value |
| --- | --- |
| Laptop | Lenovo Yoga Slim 7 Pro 14IAH7 (82UT) |
| CPU | Core i7-12700H, CPUID model `0x9a` (Alder Lake-P) |
| iGPU | `8086:46a6`, revision `0x0c`, GT2 / 96 EU |
| dGPU | RTX 2050 `10de:25a9`, PCI `00:06.2 -> 02:00.0` |
| macOS | Sonoma 14.8.9, build `23J631` |
| panel | 2880x1800 eDP on DDI A / AUX A |
| stolen memory | 60 MiB observed; framebuffer workaround required |
| display IP | Intel Linux calls ADL-P a display-version-13 / Xe-LPD platform |

This target must not be treated as Raptor Lake-P.  It shares Gen12 rendering
heritage, but it has a distinct Display 13/Xe-LPD display engine, DMC image,
revision map, and port topology.

## Ground truth captured from Linux / firmware

* `bios-dump/vbt-children-ADLP.txt`: internal eDP is DDI A / AUX A; Type-C
  candidates are DDI F/G/H/I; no populated HDMI child is present in the VBT.
* `bios-dump/dsdt.dsl`: firmware iGPU is `GFX0`; NVIDIA is
  `_SB.PC00.PEG2.PEGP`.
* Linux i915 binds natively to this GPU.  Its device description identifies
  ADL-P revision `0x0c`; upstream i915 maps that revision to ADL-P stepping D0.
* Upstream i915 selects `adlp_dmc.bin` / fallback `adlp_dmc_ver2_16.bin` and
  applies Display 13 pipe-DMC clock-gating handling.  The port must preserve
  ADL-P DMC, not fall back to a TGL DMC assumption.

## Why prior V212 profiles were not a useful port test

The V212 release was developed primarily on Raptor Lake-P / Sonoma 23H222.
Its V212 change specifically overrides an RPL-P GPU watchdog condition.  On
this target every full V212 profile stalled at `KextLog: Loading Fileset Kernel
Collection`, before an observable TGL framebuffer, TGL HW, DMC, or Metal
callback.  Therefore framebuffer IDs, scanout flags, SMBIOS changes, and
post-start Metal patches cannot yet be evaluated.

## Diagnostic controls added on branch `adlp-23j631-port`

These are **test-build-only** arguments.  All are inactive unless supplied.

| Argument | Effect | What it isolates |
| --- | --- | --- |
| `-ngreeninittrace` | logs checkpoints tagged `ADLP` | early-init progress |
| `-ngreenskipdyld` | skips `_cs_validate_page` routing / DYLD patch setup | kernel symbol route / shared-cache hook |
| `-ngreenskipdeviceinfo` | returns before `DeviceInfo::create()` | PCI discovery / early iGPU setup |
| `-ngreenskipswitchoff` | creates DeviceInfo but skips `processSwitchOff()` | Lilu dGPU/power-off traversal |
| `-ngreenrplcompat` | enables RPL-P-only V111/V212 hooks on ADL-P | legacy RPL workaround comparison only |

### Required test order

Each run starts from the known-good desktop profile and uses a **separate**
EFI-injected diagnostic kext.  Do not replace the stable profile or the known
V212 binary.

1. `-ngreeninittrace -ngreenskipdyld -ngreenskipdeviceinfo`
   * establishes that the kext can load and register callbacks on 23J631.
2. `-ngreeninittrace -ngreenskipdyld -ngreenskipswitchoff`
   * isolates DeviceInfo creation and iGPU setup from Lilu's PCI switch-off
     pass.
3. `-ngreeninittrace -ngreenskipdyld`
   * enables the complete DeviceInfo path.
4. `-ngreeninittrace -ngreenskipdeviceinfo`
   * isolates DYLD routing from PCI discovery.
5. Only after all four pass: enable `-ngreentglwithgfx` with the 9A49 identity,
   ADL-P DMC, and no full-Metal override.  Add Metal and DMC variants only
   after the kext reaches its specific callbacks.

`-ngreenrplcompat` must stay absent for the initial ADL-P accelerator test.
V111 (forced `IGAccelDevice::deviceStart`) and V212 (`isGpuIdle` watchdog
override) were written from Raptor Lake-P observations.  Earlier source routed
them whenever `!isRealTGL`, unintentionally applying them to ADL-P as well.
The wrapper itself is also guarded, so ADL-P can never force a false success
unless that explicit comparison argument is supplied.

## Build boundary

Linux is the evidence and source-analysis workstation.  The repository includes
an artifact-only GitHub Actions workflow at
`.github/workflows/build-kext.yml` which uses a hosted macOS 14 runner to build
the exact pushed commit. It is manually runnable from **Actions → Build
diagnostic kext → Run workflow**, and also runs on changes pushed to
`adlp-23j631-port`.

The workflow deliberately does not have deployment credentials and cannot
modify an EFI partition or a Mac. Its downloadable artifact contains:

* `NootedGreen-ADLP-diagnostic.kext.zip`;
* `SHA256SUMS.txt`; and
* `BUILD-INFO.txt` with the immutable source commit, Actions run URL, Xcode
  version, bundle identifier, and x86_64 architecture check.

Only download an artifact whose `source_commit` is `91acf75` or a reviewed
descendant and whose SHA-256 has been checked locally. Before a boot test it
must still be installed as a separate diagnostic EFI kext, not over the known
working desktop profile.

The equivalent local macOS build command is:

```sh
xcodebuild -project NootedGreen.xcodeproj -scheme NootedGreen -configuration Release
file build/Release/NootedGreen.kext/Contents/MacOS/NootedGreen
codesign -dvv build/Release/NootedGreen.kext
```

No binary from this branch is to be installed until its hash, bundle ID,
architecture, and source commit are recorded beside the EFI test profile.

## Port work after early-init admission

1. Validate 23J631 kernel and driver symbols/patterns before every route.
   Hard-coded 14.7.1/23H222 byte patterns must not silently patch 23J631.
2. Make the ADL-P branch explicit (`CPU 0x9a`, PCI `46a6`, revision D0), rather
   than inheriting RPL watchdog and reset workarounds by broad `!isRealTGL`
   checks.
3. Compare the Apple TGL driver register accesses with Linux Display 13 facts:
   four pipes, DDI A eDP, ADL-P pipe-DMC, and ADL-P combo/DKL PHY boundaries.
4. Introduce one register workaround at a time, backed by Linux i915 source and
   a captured macOS trace; do not enable dp0/V88 diagnostic scanout paths as a
   baseline.
