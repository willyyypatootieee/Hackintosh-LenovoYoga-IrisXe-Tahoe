# SoftCursorFix (Experimental Opt-In Software Cursor Fallback)

Lightweight, reversible Lilu plugin that routes
`AppleIntelFramebuffer::setCursorImage(void *)` onto Apple's own native
software-cursor fallback path so WindowServer composites the pointer in Metal
instead of using the ADL-P hardware cursor plane.

## Background

On 12th-gen Intel ADL-P Iris Xe (`8086:46A6`, 96 EU, `GGC=0xFEC1` 60 MiB GMS)
driving a `2880x1800 @ 90Hz` panel under macOS Tahoe 26.7.1 (`25G241`):

- Hardware cursor rendering exhibits horizontal black stripes at 90 Hz (matching
  the cursor stripe symptom noted in `docs/yoga/WIP.md`).
- Testing `YogaCursorArb.kext` (`Wa_22012358565`) on a ThinkBook 14+ 2022 with
  the same GPU/GMS/panel class did not clear the horizontal stripes.
- Triggering macOS software cursor compositing (e.g., Shake Mouse Pointer to
  Locate or Accessibility Zoom > 1.0x) immediately rendered a clean pointer at
  90 Hz.

## Mechanism

Disassembly of `AppleIntelTGLGraphicsFramebuffer` (Tahoe `25G241`) shows the
following sequence inside `AppleIntelFramebuffer::setCursorImage(void *)` at
offset `+0x154`:

```text
+0x154: 84 c0        testb %al, %al
+0x156: 74 58        je    +0x58      ; -> +0x1b0 (native SW fallback)
+0x158: 8b 45 a4     movl  -0x5c(%rbp), %eax
+0x15b: 8b 4d a8     movl  -0x58(%rbp), %ecx
```

At `+0x1b0`, Apple's native fallback path sets `controller+0xd54`, releases the
lock at `controller+0xd0`, and returns `kIOReturnUnsupported` (`0xE00002C7`),
causing WindowServer to composite the cursor in software/Metal.

`SoftCursorFix.cpp`:

1. Stays completely inactive unless `-swcursor` is present in `boot-args` (and
   honors `-swcursoroff` via Lilu's disable-arg mechanism).
2. Resolves `__ZN21AppleIntelFramebuffer14setCursorImageEPv`, checks bounds, and
   requires an exact 10-byte match at `+0x154`:
   - Expected: `84 c0 74 58 8b 45 a4 8b 4d a8`
   - Patched:  `84 c0 eb 58 8b 45 a4 8b 4d a8` (changes only 1 byte: `0x74 je` -> `0xeb jmp`)
3. Refuses to write if the symbol, bounds, or 10-byte guard does not match.
4. Performs no function hooks, no pixel reads, and no direct MMIO/PCI writes.

## Validation Status and Scope

- **Tested hardware**: Runtime-verified on a **ThinkBook 14+ 2022**
  (`i9-12900H`, Iris Xe `8086:46A6` 96 EU, `GGC=0xFEC1` 60 MiB GMS,
  `2880x1800 @ 90Hz`, macOS Tahoe 26.7.1 `25G241`). Result: clean pointer at
  90 Hz without Accessibility Zoom, zero panics.
- **Lenovo Yoga 14IAH7**: **Not yet runtime-tested on physical Yoga hardware.**
  Provided as an isolated, opt-in research candidate for Yoga owners or the
  maintainer to evaluate.
- **Unrelated issues**: This plugin only addresses the hardware cursor plane. It
  makes no claims regarding other unresolved Yoga issues (such as BCS context6
  hangs or USB-C external display).

## Building

Requires macOS Command Line Tools (`clang`, `ld`, `codesign`), `MacKernelSDK`,
and a debug/SDK `Lilu.kext` (containing `Contents/Resources/Headers` and
`Contents/Resources/Library/plugin_start.cpp`):

```bash
python3 research/yoga/yoga-soft-cursor/build_soft_cursor_fix.py \
  --sdk /path/to/MacKernelSDK \
  --lilu /path/to/Lilu.kext \
  --out /tmp/soft-cursor-build
```

Paths can also be supplied via environment variables `MAC_KERNEL_SDK`,
`LILU_KEXT` (or `LILU_RESOURCES`), and `OUT_DIR`.

## Opt-In Usage and Rollback

This repository does **not** ship a prebuilt `.kext` or modify
`EFI/OC/config.plist`. To test locally:

1. Copy the built `SoftCursorFix.kext` to `EFI/OC/Kexts/`.
2. Add an entry in `EFI/OC/config.plist` under `Kernel -> Add` **after**
   `Lilu.kext` and `NootedGreen.kext`.
3. Add `-swcursor` to `NVRAM -> Add -> 7C436110-AB2A-4BBB-A880-FE41995C9F82 -> boot-args`.
4. Reboot and verify the patch log:
   ```bash
   log show --last boot --predicate 'eventMessage CONTAINS "swcursor"'
   ```
   Expected line on a matching Tahoe `25G241` framebuffer:
   `setCursorImage -> native SW fallback verified=1 error=0`

**Rollback**: Remove `-swcursor` (or add `-swcursoroff`) from `boot-args`, or
disable/remove `SoftCursorFix.kext` in `EFI/OC/config.plist`.
