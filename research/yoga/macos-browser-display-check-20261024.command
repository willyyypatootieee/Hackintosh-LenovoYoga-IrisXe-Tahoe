#!/bin/bash
# Built-in macOS tools only. No sudo, EFI, NVRAM, system preferences or existing
# browser-profile writes. Optional browser tests use separate, retained profiles.
set -u
umask 077
if [ "$(/usr/bin/uname -s)" != Darwin ]; then
    echo 'Run this file in macOS, not Linux.'
    exit 1
fi
OUT=$(/usr/bin/mktemp -d "$HOME/Desktop/Yoga-display-check-$(/bin/date +%Y%m%d-%H%M%S)-XXXXXX") || exit 1
exec > >(/usr/bin/tee "$OUT/session.txt") 2>&1
echo "Report folder: $OUT"
echo 'No driver changes. No browser history, cookies or saved passwords collected.'
echo 'Existing browser profiles are not copied or changed.'
printf '\nBrowser symptom (crash / blank / slow / other): '
IFS= read -r symptom
printf 'Where is 60 MB displayed? '
IFS= read -r memory_label
printf 'Was the striped-cursor photo taken after the cursor-trial boot? '
IFS= read -r cursor_trial
printf 'symptom=%s\nmemory_display=%s\nphoto_after_trial=%s\n' "$symptom" "$memory_label" "$cursor_trial" > "$OUT/answers.txt"
/usr/bin/sw_vers > "$OUT/macos.txt" 2>&1
/usr/sbin/sysctl kern.boottime kern.bootargs hw.memsize > "$OUT/boot.txt" 2>&1
/usr/sbin/system_profiler SPDisplaysDataType -detailLevel mini > "$OUT/displays.txt" 2>&1
/usr/sbin/kextstat -l > "$OUT/loaded-kexts.txt" 2>&1
/usr/sbin/ioreg -r -c IntelAccelerator -l -w 0 > "$OUT/intel-accelerator.txt" 2>&1
/usr/sbin/ioreg -r -c AppleIntelFramebuffer -l -w 0 > "$OUT/framebuffers.txt" 2>&1
/usr/bin/vm_stat > "$OUT/vm-stat.txt" 2>&1
/usr/bin/top -l 1 -n 15 -o cpu -stats pid,command,cpu,mem,threads > "$OUT/cpu-sample.txt" 2>&1
# Only the last three minutes and relevant display/browser errors. No logarchive.
predicate='(process == "kernel" AND (eventMessage CONTAINS[c] "YogaCursorArb" OR eventMessage CONTAINS[c] "YogaGMSFix" OR eventMessage CONTAINS[c] "gpuRestart" OR eventMessage CONTAINS[c] "GPU Hang")) OR ((process CONTAINS[c] "Chromium" OR process CONTAINS[c] "Firefox" OR process CONTAINS[c] "Chrome") AND (eventMessage CONTAINS[c] "GPU" OR eventMessage CONTAINS[c] "Metal" OR eventMessage CONTAINS[c] "WebRender" OR eventMessage CONTAINS[c] "ANGLE"))'
/usr/bin/log show --last 3m --style compact --predicate "$predicate" > "$OUT/recent-display-browser.txt" 2>&1
echo
echo 'Optional comparison: open browsers with NEW isolated profiles.'
echo 'This is a workaround/test, NOT a GPU-driver repair; video/WebGL may be slower.'
echo 'It leaves system acceleration and existing browser settings unchanged.'
printf 'Launch these isolated test browsers now? [y/N]: '
IFS= read -r launch
if [ "$launch" = y ] || [ "$launch" = Y ]; then
    printf 'Choose default rendering [d] or software workaround [s] (default d): '
    IFS= read -r render_mode
    echo "Isolated-profile rendering selection: ${render_mode:-d}"
    for name in Chromium 'Google Chrome' Firefox; do
        app="/Applications/$name.app"
        if [ ! -d "$app" ]; then app="$HOME/Applications/$name.app"; fi
        if [ ! -d "$app" ]; then continue; fi
        exe=$(/usr/libexec/PlistBuddy -c 'Print :CFBundleExecutable' "$app/Contents/Info.plist" 2>/dev/null) || continue
        version=$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$app/Contents/Info.plist" 2>/dev/null)
        echo "$app version=$version"
        bin="$app/Contents/MacOS/$exe"
        profile="$OUT/$name-test-profile"
        /bin/mkdir "$profile" || continue
        if [ "$name" = Firefox ]; then
            if [ "$render_mode" = s ]; then
            /bin/cat > "$profile/user.js" <<'PREFS'
user_pref("gfx.webrender.software", true);
user_pref("layers.acceleration.disabled", true);
user_pref("media.hardware-video-decoding.enabled", false);
user_pref("browser.shell.checkDefaultBrowser", false);
PREFS
            fi
            "$bin" -no-remote -profile "$profile" about:support > "$OUT/$name-test.log" 2>&1 &
        else
            # Avoid empty arrays under nounset in macOS's old Bash 3.2.
            if [ "$render_mode" = s ]; then
                "$bin" --user-data-dir="$profile" --disable-gpu --no-first-run --no-default-browser-check chrome://gpu > "$OUT/$name-test.log" 2>&1 &
            else
                "$bin" --user-data-dir="$profile" --no-first-run --no-default-browser-check chrome://gpu > "$OUT/$name-test.log" 2>&1 &
            fi
        fi
    done
    echo 'Check scrolling and window drawing; record whether they improve.'
    echo 'For the software test, verify software WebRender in Firefox about:support'
    echo 'and disabled hardware acceleration in Chromium chrome://gpu.'
    echo 'A clean profile alone may help: compare default vs software runs before'
    echo 'attributing a change to the GPU. Each run creates a separate report/profile.'
    echo 'These profiles are deliberately retained in the report folder. Close the test'
    echo 'browsers to stop the test. Launch your usual browser normally for normal settings.'
fi
echo
echo 'Capture chrome://gpu and Firefox about:support from the NORMAL browsers too,'
echo 'if they can open. Do not sign in or enter passwords in the test profiles.'
echo 'Review the report before sharing: hardware identifiers, process names and local'
echo 'paths may be present. A missing startup patch log here does NOT prove patch failure.'
echo "Done. Report folder: $OUT"
printf 'Press Return to close. '
IFS= read -r unused
