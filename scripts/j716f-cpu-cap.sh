#!/bin/bash
# cap CPU max frequency to avoid unstable OPPs
echo 1804800 > /sys/devices/system/cpu/cpufreq/policy0/scaling_max_freq 2>/dev/null
echo 2246400 > /sys/devices/system/cpu/cpufreq/policy4/scaling_max_freq 2>/dev/null
echo 2361600 > /sys/devices/system/cpu/cpufreq/policy7/scaling_max_freq 2>/dev/null
