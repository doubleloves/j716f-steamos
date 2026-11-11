#!/bin/bash
# CPU pinned
echo 1804800 > /sys/devices/system/cpu/cpufreq/policy0/scaling_max_freq 2>/dev/null
echo 1804800 > /sys/devices/system/cpu/cpufreq/policy0/scaling_min_freq 2>/dev/null
echo 1574400 > /sys/devices/system/cpu/cpufreq/policy4/scaling_max_freq 2>/dev/null
echo 1574400 > /sys/devices/system/cpu/cpufreq/policy4/scaling_min_freq 2>/dev/null
echo 1862400 > /sys/devices/system/cpu/cpufreq/policy7/scaling_max_freq 2>/dev/null
echo 1862400 > /sys/devices/system/cpu/cpufreq/policy7/scaling_min_freq 2>/dev/null
# GPU pinned
G=$(cat /sys/class/devfreq/3d00000.gpu/available_frequencies 2>/dev/null | tr ' ' '\n' | sort -n | sed -n '2p')
[ -n "$G" ] && echo "$G" > /sys/class/devfreq/3d00000.gpu/min_freq 2>/dev/null
[ -n "$G" ] && echo "$G" > /sys/class/devfreq/3d00000.gpu/max_freq 2>/dev/null
# UFS pinned to max
U=$(cat /sys/class/devfreq/1d84000.ufshc/available_frequencies 2>/dev/null | tr ' ' '\n' | sort -n | tail -1)
[ -n "$U" ] && echo "$U" > /sys/class/devfreq/1d84000.ufshc/min_freq 2>/dev/null
[ -n "$U" ] && echo "$U" > /sys/class/devfreq/1d84000.ufshc/max_freq 2>/dev/null
exit 0
