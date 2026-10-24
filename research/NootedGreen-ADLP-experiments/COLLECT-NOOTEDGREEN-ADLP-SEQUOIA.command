#!/bin/bash
# Evidence collector for a Sequoia ADL-P NootedGreen test boot.
# By default it is read-only: it does not load/unload kexts, change boot
# arguments, EFI, kernel collections, or system configuration.  An explicit
# --metal-probe option additionally creates a user-space Metal device after a
# successful desktop boot; it neither installs anything nor changes EFI/KC.
set -u

ROOT=$(cd "$(dirname "$0")" && pwd)
STAMP=$(date +%Y%m%d-%H%M%S)
METAL_PROBE=0

if [ "${1:-}" = "--metal-probe" ]; then
    METAL_PROBE=1
    shift
fi

OUT="${1:-$HOME/Desktop/NootedGreen-ADLP-Sequoia-reports/boot-$STAMP}"
mkdir -p "$OUT"

# Keep machine-observed records separate from report prose.  In particular,
# receipts in the documentation are not evidence that the corresponding kernel
# path executed on this boot.
log show --last boot --style syslog --predicate 'eventMessage CONTAINS[c] "ADLP" OR eventMessage CONTAINS[c] "ngreen" OR eventMessage CONTAINS[c] "NootedGreen" OR eventMessage CONTAINS[c] "AppleIntelTGLGraphics" OR eventMessage CONTAINS[c] "IOAccelerator" OR eventMessage CONTAINS[c] "IOGPU" OR eventMessage CONTAINS[c] "Metal"' \
    > "$OUT/unified-log-NootedGreen-graphics.txt" 2>&1 || true
dmesg > "$OUT/dmesg.txt" 2>&1 || true

# liludump=N writes a boot-time Lilu dump here.  Copy every readable dump so a
# later collector run cannot overwrite the only kernel-side record.  This is
# read-only with respect to the running system.
mkdir -p "$OUT/lilu-dumps"
for source in /private/var/log/Lilu_*.txt; do
    [ -f "$source" ] || continue
    cp -p "$source" "$OUT/lilu-dumps/" 2>> "$OUT/lilu-dumps/copy-errors.txt" || true
done

# Preserve unfiltered registry and profiler data for later comparison rather
# than relying on a grep excerpt in report.txt.
ioreg -l -w0 -p IOService > "$OUT/ioreg-IOService.txt" 2>&1 || true
ioreg -r -c IOPCIDevice -d 1 -l -w0 > "$OUT/ioreg-IOPCIDevice.txt" 2>&1 || true
ioreg -r -n IGPU -l -w0 > "$OUT/ioreg-IGPU.txt" 2>&1 || true
ioreg -r -c IOAccelerator -l -w0 > "$OUT/ioreg-IOAccelerator.txt" 2>&1 || true
ioreg -r -c IntelAccelerator -l -w0 > "$OUT/ioreg-IntelAccelerator.txt" 2>&1 || true
ioreg -r -c IOGPU -l -w0 > "$OUT/ioreg-IOGPU.txt" 2>&1 || true
ioreg -r -c IOFramebuffer -l -w0 > "$OUT/ioreg-IOFramebuffer.txt" 2>&1 || true
system_profiler SPDisplaysDataType -detailLevel full > "$OUT/SPDisplaysDataType.txt" 2>&1 || true
kmutil showloaded --list-only > "$OUT/kmutil-showloaded.txt" 2>&1 || true

{
    echo '== NootedGreen ADL-P / Sequoia boot evidence =='
    date -u
    echo
    echo '== OS and boot arguments =='
    sw_vers 2>&1
    BOOT_ARGS=$(nvram boot-args 2>&1)
    printf '%s\n' "$BOOT_ARGS"
    uname -a 2>&1
	if printf '%s\n' "$BOOT_ARGS" | grep -q -- '-ngreenadlpadmit'; then
	    echo 'PROFILE=ADLP-ADMISSION-ONLY'
	else
	    echo 'PROFILE=BASELINE-OR-OTHER'
	fi
	if printf '%s\n' "$BOOT_ARGS" | grep -q -- '-ngreenadlppubtrace'; then
	    echo 'PUBLICATION_TRACE=REQUESTED'
	else
	    echo 'PUBLICATION_TRACE=NOT-REQUESTED'
	fi
    echo
    echo '== loaded relevant kexts =='
    kmutil showloaded --list-only 2>&1 | grep -iE 'lilu|nooted|intel.*(tgl|icl).*graphics|ioaccelerator' || true
    echo
    echo '== physical Intel PCI identity =='
    ioreg -r -c IOPCIDevice -d 1 -l -w0 2>&1 | grep -i -A14 -B5 -E 'a6460000|8086|Intel Iris|IGPU' || true
    echo
    echo '== accelerator, GPU, and framebuffer services =='
    ioreg -l -w0 -p IOService 2>&1 | grep -iE 'IntelAccelerator|IOAccelerator|IOGPU|IOFramebuffer|AppleIntel.*Graphics|NootedGreen' || true
    echo
    echo '== WindowServer / compositor process state =='
    pgrep -fl 'WindowServer|loginwindow' 2>&1 || true
    ps -axo pid,ppid,state,etime,command 2>&1 | grep -E '[W]indowServer|[l]oginwindow' || true
    echo
    echo '== observed NootedGreen / graphics receipts from this boot =='
    cat "$OUT/unified-log-NootedGreen-graphics.txt"
    echo
    echo '== observed Lilu dump files =='
    if ls "$OUT/lilu-dumps"/Lilu_*.txt >/dev/null 2>&1; then
        grep -iE 'ADLP|ADLP-TRACE|ngreen|NootedGreen|IntelAccelerator|IOCatalogue|AppleIntelTGLGraphics' "$OUT"/lilu-dumps/Lilu_*.txt || true
    else
        echo 'NO_READABLE_LILU_DUMP: liludump may not have run yet, may be unavailable, or may be unreadable.'
    fi
    echo
    echo '== system graphics framework state =='
    system_profiler SPDisplaysDataType 2>&1 || true
    echo
    echo '== required donor userspace bundles (identity and plist validity) =='
    for bundle in \
        AppleIntelGraphicsShared.bundle \
        AppleIntelTGLGraphicsMTLDriver.bundle \
        AppleIntelTGLGraphicsGLDriver.bundle \
        AppleIntelTGLGraphicsVADriver.bundle \
        AppleIntelTGLGraphicsVAME.bundle; do
        for root in /Library/Extensions /Library/GPUBundles; do
            if [ -d "$root/$bundle" ]; then
                echo "PRESENT $root/$bundle"
                if [ -f "$root/$bundle/Contents/Info.plist" ]; then
                    plutil -lint "$root/$bundle/Contents/Info.plist" 2>&1 || true
                    /usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$root/$bundle/Contents/Info.plist" 2>&1 || true
                    /usr/libexec/PlistBuddy -c 'Print :CFBundleVersion' "$root/$bundle/Contents/Info.plist" 2>&1 || true
                else
                    echo "MALFORMED: missing $root/$bundle/Contents/Info.plist"
                fi
            fi
        done
    done
    echo 'Missing bundles prevent the corresponding Metal/GL/VA user-space driver from loading.'
    echo
    echo '== evidence interpretation (not observed receipts) =='
    echo 'A loaded donor kext or an IOAccelerator registry service alone is not QE/CI.'
    echo 'For a positive user-space Metal result, require a non-null Metal device probe'
    echo 'or a system_profiler Metal Supported result, plus a stable WindowServer.'
	echo 'For IOCatalogue admission, look in the observed logs for addDrivers returned 1.'
	echo 'With -ngreenadlppubtrace, ADLP-TRACE ENTER proves the personality matched IGPU;'
	echo 'ADLP-TRACE RETURN records the donor native start result and final service state.'
} > "$OUT/report.txt" 2>&1

if [ "$METAL_PROBE" -eq 1 ]; then
    cat > "$OUT/MetalProbe.swift" <<'EOF'
import Foundation
import Metal

guard let device = MTLCreateSystemDefaultDevice() else {
    fputs("METAL_DEVICE=nil\n", stderr)
    exit(2)
}

print("METAL_DEVICE=present")
print("name=\(device.name)")
print("registryID=0x\(String(device.registryID, radix: 16))")
print("isLowPower=\(device.isLowPower)")
print("isHeadless=\(device.isHeadless)")
print("maxThreadsPerThreadgroup=\(device.maxThreadsPerThreadgroup.width)x\(device.maxThreadsPerThreadgroup.height)x\(device.maxThreadsPerThreadgroup.depth)")
EOF
    if command -v xcrun >/dev/null 2>&1; then
        xcrun --sdk macosx swiftc "$OUT/MetalProbe.swift" -framework Metal -o "$OUT/MetalProbe" > "$OUT/metal-probe-build.log" 2>&1 || true
        if [ -x "$OUT/MetalProbe" ]; then
            "$OUT/MetalProbe" > "$OUT/metal-probe.txt" 2>&1 || true
        else
            echo 'Metal probe was not built; inspect metal-probe-build.log.' > "$OUT/metal-probe.txt"
        fi
    else
        echo 'Metal probe unavailable: xcrun is not installed.' > "$OUT/metal-probe.txt"
    fi
fi

cp -p "$ROOT/docs/ADLP-SEQUOIA-24H32.md" "$OUT/" 2>/dev/null || true
echo "Evidence report written to: $OUT/report.txt"
if [ "$METAL_PROBE" -eq 1 ]; then
    echo "Metal probe result: $OUT/metal-probe.txt"
fi
