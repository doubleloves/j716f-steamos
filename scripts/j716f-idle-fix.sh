#!/bin/bash
for c in /sys/devices/system/cpu/cpu[0-7]; do
  for s in $c/cpuidle/state*; do
    [ -d "$s" ] || continue
    [ "$(basename $s)" = "state0" ] && continue
    echo 1 > "$s/disable" 2>/dev/null
  done
done
echo on > /sys/bus/platform/devices/3d00000.gpu/power/control 2>/dev/null
exit 0
