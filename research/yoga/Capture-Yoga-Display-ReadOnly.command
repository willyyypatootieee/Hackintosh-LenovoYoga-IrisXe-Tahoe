#!/bin/bash
# Native macOS diagnostics only. No sudo, installs, disk mounting, NVRAM,
# preferences, firmware, PCI/MMIO, or driver writes. Creates evidence files.
set -u
umask 077
if [ "$(/usr/bin/uname -s)" != Darwin ]; then
    echo 'Run this only in the existing macOS installation. Nothing collected.'
    exit 1
fi
if [ ! -x /usr/bin/perl ]; then
    echo 'Built-in Perl is unavailable; refusing unbounded diagnostic commands.'
    exit 1
fi
OUT="$HOME/Desktop/Yoga-Display-ReadOnly-$(/bin/date +%Y%m%d-%H%M%S)"
/bin/mkdir "$OUT" || exit 1
STATUS="$OUT/command-status.txt"
bounded() {
    local seconds="$1" file="$2" result
    shift 2
    /usr/bin/perl -e 'my $t=shift @ARGV; alarm $t; exec @ARGV or die "exec: $!";' \
        "$seconds" "$@" >"$OUT/$file" 2>"$OUT/$file.stderr"
    result=$?
    /usr/bin/printf '%s exit=%s limit=%ss\n' "$file" "$result" "$seconds" >>"$STATUS"
    return 0
}
snapshot() {
    local tag="$1"
    bounded 12 "$tag-ioreg.xml" /usr/sbin/ioreg -a -l -p IOService
    bounded 10 "$tag-framebuffers.txt" /usr/sbin/ioreg -l -w0 -r -c AppleIntelFramebuffer
    bounded 10 "$tag-displays.txt" /usr/sbin/ioreg -l -w0 -r -c IODisplayConnect
}
cat <<'TEXT'
This records registry state and recent display logs. No fixes are applied.
The output may contain device serial numbers, app names, and display EDIDs.
Review it before sharing. No screenshot is taken.

1. Leave the USB-C display adapter DISCONNECTED.
2. Keep the corrupted cursor visible; do not change resolution yet.
Press Return to capture that state (or Ctrl-C to cancel).
TEXT
read -r _
bounded 5 system-version.txt /usr/bin/sw_vers
bounded 5 boot-arguments.txt /usr/sbin/sysctl -n kern.bootargs
snapshot before
echo 'Now connect the same USB-C-to-HDMI adapter and turn on the monitor.'
echo 'Move the cursor for a few seconds, then press Return for the second capture.'
read -r _
snapshot connected
bounded 25 graphics-report.txt /usr/sbin/system_profiler SPDisplaysDataType -detailLevel full
bounded 25 recent-display-log.txt /usr/bin/log show --last 4m --style compact \
    --predicate '(process == "kernel" AND (eventMessage CONTAINS[c] "cursor" OR eventMessage CONTAINS[c] "AUX" OR eventMessage CONTAINS[c] "Dkl" OR eventMessage CONTAINS[c] "hotplug" OR eventMessage CONTAINS[c] "Port" OR eventMessage CONTAINS[c] "GMS")) OR (process == "WindowServer" AND (eventMessage CONTAINS[c] "cursor" OR eventMessage CONTAINS[c] "display" OR eventMessage CONTAINS[c] "mode"))'
cat >"$OUT/observations.txt" <<'TEXT'
Please fill in before sharing:
- Pointer: corrupted box / normal / invisible:
- Pointer changes shape over text? Is every shape corrupted?
- Current resolution, scaling choice and refresh rate:
- Connected USB-C socket (physical location):
- External monitor detected by macOS? Image / black / no signal:
- Cursor corruption also appears on external monitor, if it works?:

Limits: ioreg cannot expose arbitrary cursor sprite memory or private driver
object fields. If these captures do not separate image corruption from plane
state, a narrowly scoped diagnostic driver would still be needed. This script
does not enable extra logging and cannot promise the existing driver logs AUX
or Type-C state. Nonzero command exits are preserved in command-status.txt.
TEXT
echo "Evidence saved to: $OUT"
echo 'No installer or reboot is needed by this script. Press Return to close.'
read -r _
