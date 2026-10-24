# Alder Lake-P `46A6` / Sequoia `24H32` diagnostic ladder

This is a development procedure for the Lenovo Yoga Slim 7 Pro 14IAH7
(`8086:46A6`, Alder Lake-P GT2). It is not a compatibility claim and does not
establish QE/CI, Metal, sleep, or internal-panel support.

## Hardware facts used by this branch

| Item | Measured value |
| --- | --- |
| macOS target | Sequoia 15.8.1, build `24H32` |
| CPU | Alder Lake-P, CPUID model `0x9a` |
| iGPU | `8086:46A6`, revision `0x0c`, GT2 |
| topology | one slice, six DSS, 12 donor-visible SS, 96 EU |
| display engine | Intel Display 13 / Xe-LPD |
| internal panel | eDP, DDI A / PHY A, 2880x1800 at 90 Hz |
| Linux DMC evidence | `i915/adlp_dmc.bin`, version 2.20 |

The TGL donor represents Xe-LP topology in traditional sub-slices (SS), not
dual sub-slices (DSS). Therefore the measured six-DSS ADL-P GT2 is represented
as `12 SS x 8 EU = 96 EU`.

## Branch policy

`adlp-23j631-port` does three independent things:

1. provides opt-in early-init checkpoints;
2. provides a bounded Gen12 topology resolver for the legacy Sonoma TGL donor
   experiment (`ngreen-dss=N`, where `N` is 1 through 6); and
3. keeps the entire legacy Raptor Lake accelerator/reset recovery set—and the
   Sonoma donor binary patches—disabled on the Sequoia ADL-P baseline unless
   `-ngreenrplcompat` is deliberately supplied for a comparison.

The topology resolver does not read topology fuses, forcewake a power domain,
or write a display register. On Sequoia ADL-P it is evidence-only at this
stage: no Sonoma byte pattern or donor object layout is assumed valid against
the Sequoia driver collection. It becomes active only in the legacy RPL
comparison path.

Do not add `-ngreenrplcompat` to the baseline. In particular, it enables
RPL-derived BCS readiness, reset-list, engine-start, and stop/recovery paths
whose effect on ADL-P has not been established.

## Before any reboot

1. Keep a known-booting OpenCore entry unchanged and a separate recovery EFI
   entry available.
2. Build the exact branch on macOS or obtain the GitHub Actions artifact for
   the exact commit. Record the source commit, SHA-256, architecture, bundle
   identifier, and build log next to the *separate* diagnostic EFI profile.
3. Confirm that the target is exactly `15.8.1 (24H32)`. Do not reuse Sonoma
   driver byte-pattern assumptions or a Sonoma-only profile.
4. Use the physical `46A6` identity for evidence. TGL `9A49` properties, if
   required by the donor route, are a driver persona only; they do not make
   the hardware Tiger Lake.
5. Leave YogaSMC/fan work unchanged. This graphics diagnostic does not access
   EC fan-control fields.

## Ordered runs

Every run begins from the known-good profile, adds one diagnostic kext through
a separate EFI entry, and has one observation goal. Save the full boot log and
an IORegistry export after a successful boot.

### A. Kext admission only

```text
-v keepsyms=1 debug=0x100 -NGreenDebug -ngreeninittrace \
-ngreenskipdyld -ngreenskipdeviceinfo
```

Expected evidence is the `ADLP INIT` registration sequence. There must be no
TGL framebuffer, DMC, topology, or accelerator claim in this run.

### B. PCI discovery without switch-off traversal

```text
-v keepsyms=1 debug=0x100 -NGreenDebug -ngreeninittrace \
-ngreenskipdyld -ngreenskipswitchoff
```

Expected evidence: `before-deviceinfo-create`, `after-deviceinfo-create`, the
physical iGPU identity/revision, and CPU model `0x9a`. A failure here is not a
display or Metal failure; collect it before changing graphics properties.

### C. Complete early NootedGreen path

```text
-v keepsyms=1 debug=0x100 -NGreenDebug -ngreeninittrace -ngreenskipdyld
```

This isolates the complete DeviceInfo path from DYLD routing. Do not add a
TGL graphics kext or DMC selection yet.

### D. DYLD route isolated from DeviceInfo

```text
-v keepsyms=1 debug=0x100 -NGreenDebug -ngreeninittrace \
-ngreenskipdeviceinfo
```

This determines whether Sequoia's DYLD/shared-cache route itself is reachable.
Do not interpret it as a graphics test.

### E. First ADL-P donor-load observation

Run this only after A through D have produced their expected receipts and the
recovery entry has been checked:

```text
-v keepsyms=1 debug=0x100 IGLogLevel=8 -NGreenDebug \
-ngreentglwithgfx
```

Keep all of the following absent in this baseline:

```text
-ngreenrplcompat -ngreendp0
-ngreen-dmc=adlp -disablegfxfirmware -allow3d
```

Expected code-level receipts:

```text
ADLP: RPL context/blit/reset recovery routes intentionally disabled on Sequoia baseline
ADLP: V212 RPL watchdog routes intentionally disabled on Sequoia baseline
ADLP: RPL GuC host-scheduling stub intentionally disabled on Sequoia baseline
ADLP: V111 RPL force-success hook intentionally disabled on Sequoia baseline
ADLP: Sonoma-only TGL accelerator binary patches withheld on Sequoia baseline
ADLP: Sequoia baseline: TGL accelerator reached; catalog personality and MMIO mapping withheld
ADLP: getGPUInfo: donor topology left unmodified on Sequoia baseline
```

This is intentionally a load-observation run, not driver admission: it withholds
the injected `IOCatalogue` accelerator personality and MMIO mapping as well as
all Sonoma binary patches. Therefore a successful receipt only proves that the
TGL accelerator kext reached NootedGreen's Sequoia guard; it does not prove an
accelerator attached. The six-DSS / 12-SS / 96-EU evidence remains recorded in
the source and this document, but it is deliberately not injected into a
Sequoia donor object. A Sequoia object-layout and byte-pattern receipt is
required before either topology injection or driver admission can be tested.

### F. DMC is a later, separate experiment

Do not append `ngreen-dmc=adlp` to run E. The current ADL-P DMC code carries
Sonoma-specific SRAM, power-well, transcoder, and panel sequencing writes; the
older notes also record a post-boot eDP lane-drop observation. It is neither an
accelerator-admission prerequisite nor a Sequoia-safe default. Test it only
after an exact Sequoia DMC layout has been independently verified, with a
dedicated rollback profile and without any topology or RPL-compat change.

### G. Accelerator admission only (separate EFI profile)

After the donor framebuffer and accelerator kexts are stable, an IOKit
accelerator-attachment test may add:

```text
-ngreenadlpadmit
```

This is deliberately narrower than `-ngreenrplcompat`. It restores the
synthetic `IntelAccelerator` IOCatalogue personality that the baseline
withholds. Without that IOKit match, the donor accelerator kext can be loaded
but cannot publish a real accelerator service, so Metal has no device to open.

The injected personality is constrained to the Intel `9A49` donor persona and
the physical `46A6` fallback. It does not match the laptop's NVIDIA dGPU and
does not alter either PCI identity.

It does **not** enable RPL deviceStart force-success, RPL context/blit/reset
recovery routes, RPL host-firmware stubbing, Sonoma binary patches, or ADL-P
donor topology injection.

Expected receipts:

```text
ADLP: admission-only: TGL IntelAccelerator personality enabled; RPL recovery routes remain withheld
injectAcceleratorPersonality: addDrivers returned 1
ADLP: admission-only: IntelAccelerator::start left native; legacy non-TGL MMIO/reset/IRQ wrapper withheld
```

This is still not a QE/CI or Metal claim. Require a real Intel
`IOAccelerator`, `IOGPU` where applicable, non-null `MTLCreateSystemDefaultDevice()`,
and a stable WindowServer. Keep the known-good EFI profile unchanged; add this
one argument only to a separate external EFI profile.

### H. Admission publication trace (separate diagnostic profile)

When the admission result is ambiguous because boot logs were not retained, a
one-boot diagnostic profile may add the following **in addition to** the
admission argument:

```text
-ngreenadlpadmit -ngreenadlppubtrace
```

`-ngreenadlppubtrace` installs a logging-only wrapper around the donor's native
`IntelAccelerator::start(IOService *)`. It performs no MMIO access, changes no
registry property, does not alter the donor return value, and does not enable
any RPL compatibility/recovery path. It is not enabled by `-ngreenadlpadmit`
alone.

Interpret only machine-observed records from the collector's unified log or
copied Lilu dump:

| Observed record | Narrow conclusion |
| --- | --- |
| `injectAcceleratorPersonality: addDrivers returned 1` | The injected personality was accepted and an IOKit rematch was requested; no attach is implied. |
| `ADLP-TRACE: ... ENTER` | The personality matched a provider and IOKit entered the donor accelerator start path. |
| `ADLP-TRACE: ... RETURN ret=...` | The native donor start returned; inspect the accompanying service state and IORegistry snapshot. |
| no `ADLP-TRACE ... ENTER` after a confirmed trace-arm receipt | The experiment did not reach donor start; inspect matching/driver publication before changing GPU configuration. |

This trace does not itself prove a published `IOAccelerator`, `IOGPU`, QE/CI,
or Metal. Preserve the profile as a separate external-USB rollback point and
remove the trace argument after collecting the evidence.

## Result classification

| Last reliable receipt | Meaning | Next action |
| --- | --- | --- |
| no `ADLP INIT` receipt | kext admission/load problem | inspect EFI config, kext hash, and Lilu order only |
| fails before/at DeviceInfo | early PCI/Lilu path problem | compare runs A/B/C; do not change DMC/topology |
| TGL kext does not load | donor/root-patch installation issue | inspect installed TGL kext paths and Sequoia symbol receipts |
| donor loads but no `getGPUInfo` | accelerator-routing problem | inspect only route/symbol failure; do not add DMC or topology injection |
| `getGPUInfo` but no display | accelerator admission only | collect MMIO/IORegistry evidence; do not call it QE/CI |
| display but no Metal | display handoff only | treat Metal as a separate stage |

An internal eDP picture is not expected from topology admission alone. ADL-P
Display 13 routing, DMC sequencing, and the DDI A / AUX A panel path remain
independent work items.
