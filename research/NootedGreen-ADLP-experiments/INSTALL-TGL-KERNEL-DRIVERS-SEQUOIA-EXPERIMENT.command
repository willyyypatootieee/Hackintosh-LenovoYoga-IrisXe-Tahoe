#!/bin/bash
# Deploy the donor TGL kernel drivers at the on-volume paths used by the
# NootedGreen TGL patch registration. This is an explicit, reversible test for
# the external Sequoia 15.8.1 / 24H32 installation only.
#
# The existing EFI/OC injected copies are deliberately not modified here. A
# successful on-volume deployment lets NootedGreen resolve and patch the exact
# /Library/Extensions executable paths it registers for the TGL route.
set -euo pipefail

ROOT=$(cd "$(dirname "$0")" && pwd)
SRC="$ROOT/sle_Internal/le"
DEST=/Library/Extensions
STAMP=$(date +%Y%m%d-%H%M%S)
BACKUP="$HOME/NootedGreen-ADLP-Sequoia-kext-backups/$STAMP"
MODE=preflight

if [ "${1:-}" = "--apply" ]; then
    MODE=apply
elif [ "${1:-}" = "--rebuild-only" ]; then
    MODE=rebuild-only
elif [ "${1:-}" = "--repair-admission" ]; then
    MODE=repair-admission
elif [ -n "${1:-}" ]; then
    echo "Usage: $0 [--apply|--rebuild-only|--repair-admission]" >&2
    exit 2
fi

[ "$(uname -m)" = x86_64 ] || { echo 'STOP: intended only for x86_64 macOS.' >&2; exit 1; }
[ "$(sw_vers -productVersion)" = 15.8.1 ] || { echo "STOP: requires macOS 15.8.1; found $(sw_vers -productVersion)." >&2; exit 1; }
[ "$(sw_vers -buildVersion)" = 24H32 ] || { echo "STOP: requires Sequoia build 24H32; found $(sw_vers -buildVersion)." >&2; exit 1; }
[ -d "$SRC" ] || { echo "STOP: donor-kext source is missing: $SRC" >&2; exit 1; }
[ -d "$DEST" ] || { echo "STOP: destination is missing: $DEST" >&2; exit 1; }

KEXTS=(
    HookCase.kext
    AppleIntelTGLGraphicsFramebuffer.kext
    AppleIntelTGLGraphics.kext
)

echo '== TGL donor kernel-driver deployment =='
echo "source=$SRC"
echo "destination=$DEST"
echo "mode=$MODE"
echo 'scope=external Sequoia experiment only; no EFI, NVRAM, partition, or internal-NVMe writes'
echo 'physical_gpu=8086:46A6 revision 0C (preserved; 9A49 is donor-driver persona only)'
echo

for kext in "${KEXTS[@]}"; do
    path="$SRC/$kext"
    plist="$path/Contents/Info.plist"
    executable="$path/Contents/MacOS/${kext%.kext}"
    [ -d "$path" ] || { echo "STOP: source kext is missing: $path" >&2; exit 1; }
    [ -f "$plist" ] || { echo "STOP: source kext has no Info.plist: $path" >&2; exit 1; }
    [ -x "$executable" ] || { echo "STOP: source kext has no executable: $executable" >&2; exit 1; }
    /usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$plist" >/dev/null
    if [ "$kext" != HookCase.kext ]; then
        grep -qi '0x46a68086' "$plist" || {
            echo "STOP: $kext lacks physical 46A6 catalogue admission." >&2
            exit 1
        }
        grep -qi '0x9a498086' "$plist" || {
            echo "STOP: $kext lacks donor 9A49 catalogue admission." >&2
            exit 1
        }
    fi
    printf 'SOURCE  %s  ' "$kext"
    /usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$plist"
    shasum -a 256 "$plist" "$executable"
done

if [ "$MODE" = preflight ]; then
    cat <<'EOF'

No files were changed. The installed TGL framebuffer and accelerator have a
hard OSBundleLibraries dependency on org.smichaud.HookCase. This deployment
places that dependency and both TGL kexts at /Library/Extensions, which is the
path registered by the active NootedGreen TGL patcher. It preserves the current
EFI injection route.

To install only missing/different donor kexts after reviewing the source hashes:

  ./INSTALL-TGL-KERNEL-DRIVERS-SEQUOIA-EXPERIMENT.command --apply

If the kexts were already installed and only the Auxiliary Kernel Collection
needs to be rebuilt, use:

  ./INSTALL-TGL-KERNEL-DRIVERS-SEQUOIA-EXPERIMENT.command --rebuild-only

If KernelManagement reports `Bad code signature` or cannot find HookCase while
loading the framebuffer, use:

  ./INSTALL-TGL-KERNEL-DRIVERS-SEQUOIA-EXPERIMENT.command --repair-admission

The apply operation needs an administrator password, preserves only
same-named *different* on-volume kexts under your home directory, creates a
restore script only when a file changed, and asks kernelmanagerd to rebuild
the Auxiliary Kernel Collection. `--repair-admission` does not copy kexts: it
ad-hoc-signs only installed bundles whose current signature is invalid, then
asks kernelmanagerd to rebuild the Auxiliary Kernel Collection. Both modes
leave EFI, identity injection, NVRAM, and boot arguments unchanged. Afterward use
ACTIVATE-TGL-DRIVERS-SEQUOIA-EXPERIMENT.command --apply to make a direct,
dependency-ordered driver-start attempt and verify real attachment.
EOF
    exit 0
fi

CHANGED=0

if [ "$MODE" = apply ]; then
    for kext in "${KEXTS[@]}"; do
        source_plist="$SRC/$kext/Contents/Info.plist"
        source_executable="$SRC/$kext/Contents/MacOS/${kext%.kext}"
        dest_plist="$DEST/$kext/Contents/Info.plist"
        dest_executable="$DEST/$kext/Contents/MacOS/${kext%.kext}"

        if [ -f "$dest_plist" ] && [ -f "$dest_executable" ] && \
           cmp -s "$source_plist" "$dest_plist" && \
           cmp -s "$source_executable" "$dest_executable"; then
            echo "UNCHANGED  $DEST/$kext already matches the verified source"
            continue
        fi

        if [ "$CHANGED" -eq 0 ]; then
            mkdir -p "$BACKUP"
            echo "Backup directory: $BACKUP"
            CHANGED=1
        fi

        if [ -e "$DEST/$kext" ]; then
            echo "Backing up different existing $DEST/$kext"
            sudo ditto "$DEST/$kext" "$BACKUP/$kext"
        else
            echo "No existing $DEST/$kext; installing new donor copy"
            : > "$BACKUP/NEW-$kext"
        fi

        echo "Installing $kext"
        sudo rm -rf "$DEST/$kext"
        sudo ditto "$SRC/$kext" "$DEST/$kext"
        sudo chown -R root:wheel "$DEST/$kext"
        sudo chmod -R go-w "$DEST/$kext"
    done
fi

# The donor Info.plists were intentionally amended with physical 46A6 catalogue
# matching. That changes the sealed bundle metadata, so an old vendor signature
# is no longer valid. KernelManagement correctly refuses such a bundle instead
# of admitting it to the Auxiliary KC. Repair only invalid on-volume bundles;
# leave a valid existing signature untouched. The external OpenCore injection
# is separate and is deliberately not modified.
if [ "$MODE" = apply ] || [ "$MODE" = repair-admission ]; then
    echo
    echo '== Auxiliary-KC authentication repair =='
    for kext in "${KEXTS[@]}"; do
        installed="$DEST/$kext"
        if codesign --verify --deep --strict --verbose=1 "$installed" >/dev/null 2>&1; then
            echo "SIGNED     $kext already has a valid code signature"
        else
            # Preserve the exact pre-signing bundle once. This includes its
            # original CodeResources and lets the generated restore script
            # return to the prior on-volume state if admission does not help.
            if [ ! -d "$BACKUP/$kext" ] && [ ! -e "$BACKUP/NEW-$kext" ]; then
                mkdir -p "$BACKUP"
                echo "Backing up pre-signing $installed"
                sudo ditto "$installed" "$BACKUP/$kext"
            fi
            echo "SIGNING    $kext has an invalid/missing signature; applying local ad-hoc signature"
            sudo codesign --force --sign - "$installed"
            if codesign --verify --deep --strict --verbose=1 "$installed" >/dev/null 2>&1; then
                echo "SIGNED     $kext now verifies"
                CHANGED=1
            else
                echo "STOP: codesign verification still fails for $installed." >&2
                codesign --verify --deep --strict --verbose=4 "$installed" >&2 || true
                exit 1
            fi
        fi
    done
fi

if [ "$CHANGED" -eq 1 ]; then
    {
        echo "installed_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
        echo "source=$SRC"
        echo "destination=$DEST"
        echo "backup=$BACKUP"
        echo 'installed_kexts='
        for kext in "${KEXTS[@]}"; do
            shasum -a 256 "$DEST/$kext/Contents/Info.plist" "$DEST/$kext/Contents/MacOS/${kext%.kext}"
        done
    } | tee "$BACKUP/INSTALL-INFO.txt"

    cat > "$BACKUP/RESTORE-PREEXISTING-TGL-KEXTS.sh" <<'EOF'
#!/bin/bash
# Restores the on-volume donor-kext state captured before one explicit test.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
DEST=/Library/Extensions
KEXTS=(HookCase.kext AppleIntelTGLGraphicsFramebuffer.kext AppleIntelTGLGraphics.kext)

for kext in "${KEXTS[@]}"; do
    # Leave unchanged kexts alone. Only process a kext that this deployment
    # actually replaced or added.
    if [ -d "$HERE/$kext" ] || [ -e "$HERE/NEW-$kext" ]; then
        sudo rm -rf "$DEST/$kext"
        if [ -d "$HERE/$kext" ]; then
            sudo ditto "$HERE/$kext" "$DEST/$kext"
            sudo chown -R root:wheel "$DEST/$kext"
            sudo chmod -R go-w "$DEST/$kext"
            echo "Restored prior $kext"
        else
            echo "Removed experimental $kext (none existed before this install)"
        fi
    fi
done
sudo kmutil rebuild
echo 'Restore complete. Reboot before testing graphics again.'
EOF
    chmod 700 "$BACKUP/RESTORE-PREEXISTING-TGL-KEXTS.sh"
elif [ "$MODE" = apply ]; then
    echo 'No kext payload files changed: all installed kexts already match the verified source.'
fi

echo
echo 'Requesting an auxiliary kernel-collection rebuild…'
# Do not use `kmutil install --update-all` here. That command rebuilds sealed
# System/Boot collections and requires a KDK; it fails on a normal sealed
# Sequoia installation. `kmutil rebuild` is the supported operation for
# installed /Library/Extensions kexts and only evaluates the Auxiliary KC.
if sudo kmutil rebuild; then
    :
else
    status=$?
    echo >&2
    echo 'Auxiliary KC rebuild did not complete. Capturing kext diagnostics; no files are removed.' >&2
    for kext in "${KEXTS[@]}"; do
        sudo kmutil print-diagnostics --bundle-path "$DEST/$kext" --diagnose-dependencies || true
    done
    exit "$status"
fi

echo
echo 'Installed donor dependency + TGL kernel drivers at the paths NootedGreen registers.'
echo 'For a direct guarded ADL-P driver-start attempt, run:'
echo '  sudo ./ACTIVATE-TGL-DRIVERS-SEQUOIA-EXPERIMENT.command --hookcase-only'
echo '  sudo ./ACTIVATE-TGL-DRIVERS-SEQUOIA-EXPERIMENT.command --framebuffer-only'
echo '  sudo ./ACTIVATE-TGL-DRIVERS-SEQUOIA-EXPERIMENT.command --apply'
echo "To undo this kernel-driver deployment: $BACKUP/RESTORE-PREEXISTING-TGL-KEXTS.sh"
