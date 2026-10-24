#!/bin/bash
# Controlled ADL-P -> TGL donor-driver activation ladder.
#
# This does not claim native Alder Lake support. It verifies the physical ADL-P
# 46A6 identity, verifies NootedGreen's active TGL route, then attempts only
# the requested stage. No EFI, NVRAM, boot argument, partition, or internal
# NVMe write is performed. --apply can load kernel code and may destabilise an
# unsupported graphics stack; run the stages in order. On macOS 11+, a newly
# admitted on-volume kext requires one reboot before the AuxKC can load it.
set -euo pipefail

DEST=/Library/Extensions
MODE=preflight

case "${1:-}" in
    '') MODE=preflight ;;
    --hookcase-only) MODE=hookcase ;;
    --framebuffer-only) MODE=framebuffer ;;
    --apply) MODE=full ;;
    --stage-next-boot) MODE=stage-next-boot ;;
    *)
        echo "Usage: $0 [--hookcase-only|--framebuffer-only|--apply|--stage-next-boot]" >&2
        exit 2
        ;;
esac

[ "$(uname -m)" = x86_64 ] || { echo 'STOP: intended only for x86_64 macOS.' >&2; exit 1; }
[ "$(sw_vers -productVersion)" = 15.8.1 ] || { echo "STOP: requires macOS 15.8.1; found $(sw_vers -productVersion)." >&2; exit 1; }
[ "$(sw_vers -buildVersion)" = 24H32 ] || { echo "STOP: requires Sequoia build 24H32; found $(sw_vers -buildVersion)." >&2; exit 1; }

HOOKCASE_KEXT=HookCase.kext
HOOKCASE_ID=org.smichaud.HookCase
FRAMEBUFFER_KEXT=AppleIntelTGLGraphicsFramebuffer.kext
FRAMEBUFFER_ID=com.xxxxx.driver.AppleIntelTGLGraphicsFramebuffer
ACCELERATOR_KEXT=AppleIntelTGLGraphics.kext
ACCELERATOR_ID=com.xxxxx.driver.AppleIntelTGLGraphics

loaded_kexts() {
    kmutil showloaded --list-only 2>/dev/null || true
}

kext_loaded() {
    loaded_kexts | grep -Fq "$1 ("
}

bundle_id() {
    /usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$DEST/$1/Contents/Info.plist"
}

verify_kext() {
    local kext=$1 expected_id=$2
    local path="$DEST/$kext"
    local plist="$path/Contents/Info.plist"
    local executable="$path/Contents/MacOS/${kext%.kext}"

    if [ ! -d "$path" ]; then
        echo "STOP: $path is not installed. Run INSTALL-TGL-KERNEL-DRIVERS-SEQUOIA-EXPERIMENT.command --apply first." >&2
        return 1
    elif [ ! -f "$plist" ]; then
        echo "STOP: malformed kext (missing Info.plist): $path" >&2
        return 1
    elif [ ! -x "$executable" ]; then
        echo "STOP: malformed kext (missing executable): $executable" >&2
        return 1
    elif [ "$(bundle_id "$kext")" != "$expected_id" ]; then
        echo "STOP: $kext identifier does not match expected $expected_id." >&2
        return 1
    else
        echo "READY     $kext ($expected_id)"
        return 0
    fi
}

verify_tgl_catalogue() {
    local kext=$1 plist="$DEST/$1/Contents/Info.plist"
    if grep -qi '0x46a68086' "$plist" && grep -qi '0x9a498086' "$plist"; then
        echo "CATALOGUE $kext: physical 46A6 + donor 9A49 present"
        return 0
    else
        echo "STOP: $kext lacks either physical 46A6 or donor 9A49 catalogue matching." >&2
        return 1
    fi
}

load_stage() {
    local label=$1 kext=$2 identifier=$3

    if kext_loaded "$identifier"; then
        echo "SKIP      $label is already loaded: $identifier"
        return 0
    fi

    echo "LOAD      $label from $DEST/$kext"
    if sudo kmutil load -p "$DEST/$kext"; then
        # KernelManagement may accept a request before IOKit has published the
        # kmod. Require a real loaded state before advancing to the next stage.
        sleep 2
        if kext_loaded "$identifier"; then
            echo "ACTIVE    $label loaded: $identifier"
            return 0
        else
            echo "STOP: KernelManagement accepted $label but it is not loaded after 2 seconds." >&2
            echo "== Diagnostics for $label ==" >&2
            sudo kmutil print-diagnostics --bundle-path "$DEST/$kext" --diagnose-dependencies || true
            return 1
        fi
    else
        local status=$?
        echo "STOP: $label load request failed (exit $status)." >&2
        echo "== Diagnostics for $label ==" >&2
        sudo kmutil print-diagnostics --bundle-path "$DEST/$kext" --diagnose-dependencies || true
        return "$status"
    fi
}

stage_full_stack_for_next_boot() {
    # A kext absent from the current AuxKC cannot be dynamically introduced to
    # the running kernel. Request all three members together so
    # kernelmanagerd can build one dependency-complete AuxKC and the user needs
    # only one reboot, rather than one reboot for HookCase and another each for
    # the framebuffer and accelerator.
    echo 'STAGE     HookCase + TGL framebuffer + TGL accelerator for one next-boot Auxiliary KC'
    set +e
    sudo kmutil load \
        -p "$DEST/$HOOKCASE_KEXT" \
        -p "$DEST/$FRAMEBUFFER_KEXT" \
        -p "$DEST/$ACCELERATOR_KEXT"
    local status=$?
    set -e

    case "$status" in
        0)
            echo 'ACTIVE    complete donor stack is already available to the running kernel.'
            return 0
            ;;
        28)
            cat <<'EOF'
QUEUED    KernelManagement accepted the complete donor stack for the next boot.
NEXT      Reboot the external Sequoia/OpenCore entry exactly once. Do not run
          --hookcase-only, --framebuffer-only, or --apply before that reboot.
          After boot, run --apply to verify the three loaded kexts and real
          Intel attachment. A reboot cannot be bypassed for a newly built AuxKC.
EOF
            return 0
            ;;
        *)
            echo "STOP: full next-boot AuxKC staging failed (exit $status)." >&2
            echo '== Diagnostics for complete donor stack ==' >&2
            sudo kmutil print-diagnostics --bundle-path "$DEST/$HOOKCASE_KEXT" --diagnose-dependencies || true
            sudo kmutil print-diagnostics --bundle-path "$DEST/$FRAMEBUFFER_KEXT" --diagnose-dependencies || true
            sudo kmutil print-diagnostics --bundle-path "$DEST/$ACCELERATOR_KEXT" --diagnose-dependencies || true
            return "$status"
            ;;
    esac
}

attachment_state() {
    echo
    echo '== Real ADL-P attachment state =='
    # Use the same targeted IGPU snapshot that passed the preflight test. A
    # broad recursive IOService lookup is presentation-dependent and produced
    # a false negative after an otherwise successful physical-identity check.
    if printf '%s\n' "$IGPU_STATE" | grep -qi 'pci8086,46a6'; then
        echo 'PASS      physical compatible identity remains pci8086,46a6'
    else
        echo 'FAIL      physical pci8086,46a6 compatible identity is absent'
    fi

    if ioreg -r -c IOFramebuffer -l -w0 | grep -qiE 'AppleIntel.*Framebuffer|AppleIntelFramebufferController'; then
        echo 'PASS      Intel framebuffer service is present'
    else
        echo 'PENDING   no Intel framebuffer service; IONDRV fallback is not acceleration'
    fi

    if ioreg -r -c IOAccelerator -l -w0 | grep -qiE 'IntelAccelerator|AppleIntel.*Graphics|com\.xxxxx\.driver\.AppleIntelTGLGraphics'; then
        echo 'PASS      Intel accelerator service is present'
    else
        echo 'PENDING   no real Intel IOAccelerator service'
    fi

    if ioreg -r -c IOGPU -l -w0 | grep -qi .; then
        echo 'INFO      IOGPU service exists; inspect its provider before calling this QE/CI'
    else
        echo 'PENDING   no IOGPU service'
    fi
}

echo '== Controlled ADL-P donor-driver activation =='
echo "mode=$MODE"
echo "source=$DEST (already-installed on-volume drivers)"
echo 'scope=external Sequoia only; no EFI, NVRAM, boot-argument, partition, or internal-NVMe writes'
echo 'route=physical Alder Lake-P 8086:46A6 -> NootedGreen compatibility patches -> TGL donor drivers'
echo

IGPU_STATE=$(ioreg -r -n IGPU -d 1 -l -w0 2>/dev/null || true)
if printf '%s\n' "$IGPU_STATE" | grep -qi 'pci8086,46a6'; then
    echo 'PASS      physical ADL-P compatible identity: pci8086,46a6'
elif printf '%s\n' "$IGPU_STATE" | grep -qi 'pci8086,9a49'; then
    echo 'STOP: IGPU only reports donor 9A49; physical 46A6 compatible identity is missing.' >&2
    exit 1
else
    echo 'STOP: could not verify the target Intel IGPU as physical 46A6.' >&2
    exit 1
fi

if printf '%s\n' "$IGPU_STATE" | grep -qi 'device-id.*499a0000'; then
    echo 'PASS      donor driver persona is 9A49; physical identity remains checked above'
else
    echo 'WARN      9A49 donor persona is not visible; do not change identity here'
fi

BOOT_ARGS=$(nvram boot-args 2>/dev/null || true)
if printf '%s\n' "$BOOT_ARGS" | grep -q -- '-ngreentglwithgfx'; then
    echo 'PASS      NootedGreen TGL framebuffer + accelerator route is enabled'
else
    echo 'STOP: -ngreentglwithgfx is missing; the active NootedGreen route would not patch both donor kexts.' >&2
    exit 1
fi

if kext_loaded com.StezzaPilot.NootedGreen; then
    echo 'PASS      NootedGreen is loaded'
else
    echo 'STOP: NootedGreen is not loaded; donor drivers would not receive ADL-P compatibility patches.' >&2
    exit 1
fi

verify_kext "$HOOKCASE_KEXT" "$HOOKCASE_ID"
verify_kext "$FRAMEBUFFER_KEXT" "$FRAMEBUFFER_ID"
verify_kext "$ACCELERATOR_KEXT" "$ACCELERATOR_ID"
verify_tgl_catalogue "$FRAMEBUFFER_KEXT"
verify_tgl_catalogue "$ACCELERATOR_KEXT"

if [ "$MODE" = preflight ]; then
    cat <<'EOF'

No kernel code was loaded. Select one real ADL-P experiment stage:

  --hookcase-only       Load/verify only the mandatory HookCase dependency.
  --framebuffer-only    Require HookCase, then load/verify the TGL framebuffer.
  --apply               Require both prior stages, then load/verify the TGL accelerator.
  --stage-next-boot     Build one AuxKC containing all three kexts; reboot once.

The script uses if/else guards: an already loaded stage is skipped, an absent or
rejected stage stops the ladder, and the accelerator is never attempted until
HookCase and the framebuffer are confirmed loaded.
EOF
    exit 0
fi

if [ "$MODE" = stage-next-boot ]; then
    stage_full_stack_for_next_boot
    exit $?
fi

if ! load_stage 'HookCase dependency' "$HOOKCASE_KEXT" "$HOOKCASE_ID"; then
    attachment_state
    exit 1
elif [ "$MODE" = hookcase ]; then
    echo 'RESULT    HookCase stage completed; no graphics donor driver was requested.'
    exit 0
fi

if ! load_stage 'TGL framebuffer' "$FRAMEBUFFER_KEXT" "$FRAMEBUFFER_ID"; then
    attachment_state
    exit 1
elif [ "$MODE" = framebuffer ]; then
    attachment_state
    echo 'RESULT    framebuffer stage completed; accelerator intentionally not requested.'
    exit 0
fi

if ! load_stage 'TGL accelerator' "$ACCELERATOR_KEXT" "$ACCELERATOR_ID"; then
    attachment_state
    exit 1
fi

attachment_state
echo
echo 'RESULT    all three kexts are loaded. This is real driver admission, but not yet proof of QE/CI.'
echo 'NEXT      require Intel framebuffer + accelerator attachment and a non-null Metal device before claiming acceleration.'
