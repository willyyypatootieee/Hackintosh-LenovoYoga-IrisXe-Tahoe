#!/bin/bash
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
echo 'Yoga Iris Xe — v3 targeted HookCase repair'
echo 'Fixes the v2 ___cxa_atexit build defect. No compiler or Xcode required.'
echo 'Preserves your original v2 backup; replaces ONLY HookCase.kext.'
echo 'Static imports audited against your installed 25G241; native kmutil/live GPU validation still required.'
printf 'Apply the targeted repair? Type REPAIR: '
read -r answer
[ "$answer" = REPAIR ] || exit 0
if sudo /bin/bash "$HERE/repair_hookcase_v3.sh"; then
    echo 'Repair command completed successfully.'
else
    status=$?
    echo 'Repair did not complete. Do not treat this as success or reboot for a GPU test.'
    printf '\nPress Return to close. '
    read -r answer
    exit "$status"
fi
printf '\nPress Return to close. '
read -r answer
