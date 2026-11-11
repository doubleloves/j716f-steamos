#!/bin/bash
[ -w /sys/module/pstore/parameters/kmsg_bytes ] && echo 131072 > /sys/module/pstore/parameters/kmsg_bytes
exit 0
