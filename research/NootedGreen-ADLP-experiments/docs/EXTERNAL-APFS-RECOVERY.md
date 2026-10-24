# External Sequoia APFS recovery — before QE/CI testing

The macOS test system is installed on the **external USB SSD**, not the
internal Linux NVMe.

| Environment | External macOS physical store |
| --- | --- |
| Linux | `/dev/sda3` — GPT label `Sequoia macOS Target` |
| macOS boot log | `disk2s1` (this number can change in Recovery) |

The boot messages below occur before NootedGreen or any Intel donor graphics
driver receipt:

```text
disk2s1: device is not readable I/O, no raw disk access
apfs_keybag_init: failed to initialize volume keybag
apfs_trim_to_prev_fsize: reset ...
```

This is an APFS/external-storage failure, not a QE/CI result.  Do **not**
disable the external EFI QE/CI profile to repair it; its graphics experiment is
separate from filesystem recovery.

## Recovery procedure

1. Boot the USB's **Install macOS Sequoia** entry (the separate 20 GiB HFS+
   installer partition), not the installed `Sequoia macOS Target` entry.
2. In the installer, open **Utilities → Disk Utility**.
3. Choose **View → Show All Devices**.
4. Identify the external SSD by its physical identity:
   `SAMSUNG MZVLB256HAHQ-000L7` in the USB enclosure. Do not select the
   internal NVMe or any Linux/Windows volume.
5. Run **First Aid** in this order, stopping if it reports an unrepaired error:
   external physical disk → APFS container/physical store → each APFS volume.
6. If Disk Utility cannot repair the physical store, use **Utilities →
   Terminal**, confirm identifiers first, then run a verification before a
   repair:

   ```sh
   diskutil list
   diskutil apfs list
   diskutil verifyVolume disk2s1
   ```

   Replace `disk2s1` only with the identifier that `diskutil apfs list` shows
   is the physical store for the external `Sequoia macOS Target` container.
   If verification reports repairable metadata errors:

   ```sh
   diskutil repairVolume disk2s1
   ```

7. Shut down, reconnect the SSD directly (no hub), and boot the experimental
   external EFI profile again.

## If First Aid or repair cannot read the drive

The external device is a Samsung NVMe behind a Realtek RTL9210B USB/UAS
enclosure. Try a different direct USB port, a known-good short cable, and—if
available—a different NVMe enclosure before changing graphics settings. Back
up recoverable data before erasing/reinstalling. Do not run erase, repartition,
or restore commands against an uncertain disk identifier.

## QE/CI profile status

The active external EFI graphics profile deliberately remains enabled while
performing this recovery. Its current donor kext stack is:

```text
HookCase.kext
NootedGreen-ADLP-DIAG.kext
AppleIntelTGLGraphicsFramebuffer.kext
AppleIntelTGLGraphics.kext
```

It also retains the TGL donor, ADL-P DMC, host scheduler, full-Metal, 3D,
topology, and verbose logging arguments. APFS recovery only restores the
ability to reach that graphics experiment.
