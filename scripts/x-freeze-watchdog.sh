#!/bin/bash
# x-freeze-watchdog — 显示栈卡死时自动抓现场（只记录，不重启 X）
#
# 覆盖两种“卡住”：
#   A. X 停止响应（xset q 超时）        → dump "X unresponsive"
#   B. X 还在跑，但画面不再刷新（vsync 计数停住）→ dump "display stalled"
#   C. Xorg 进程消失（崩溃/退出）        → dump "Xorg disappeared"
#
# 抓取内容：Xorg 主线程/各线程内核栈与 wchan、全机进程（含 D 状态）、内核日志尾部、
#           DSI/DPU 中断两次采样、DRM/DPMS/背光、debugfs 列表。
# 输出：/var/log/dsh-freeze/freeze-<时间>.log，保留最近 20 份；运行记录 watchdog.log。
#
# 用法：
#   x-freeze-watchdog.sh              # 常驻（systemd 用）
#   x-freeze-watchdog.sh --dump-now   # 立刻抓一次现场（自检用）
set -u

OUTDIR=${OUTDIR:-/var/log/dsh-freeze}
INTERVAL=${INTERVAL:-1}
FAIL_THRESHOLD=${FAIL_THRESHOLD:-5}
PROBE_TIMEOUT=${PROBE_TIMEOUT:-2}
VSYNC_STALL_LIMIT=${VSYNC_STALL_LIMIT:-10}
KEEP=${KEEP:-20}
DRM_CARD=${DRM_CARD:-/sys/kernel/debug/dri/0}
CONNECTOR=${CONNECTOR:-/sys/class/drm/card0-DSI-1}

mkdir -p "$OUTDIR" 2>/dev/null
chmod 755 "$OUTDIR" 2>/dev/null

log() { printf '%s %s\n' "$(date '+%F %T')" "$*" >>"$OUTDIR/watchdog.log"; }

xorg_pid() { pgrep -x Xorg | head -n1; }

xorg_field() {  # $1=pid  $2=display|auth
    local cmd
    cmd=$(tr '\0' ' ' <"/proc/$1/cmdline" 2>/dev/null)
    case "$2" in
        display)
            if [[ $cmd =~ (:[0-9]+) ]]; then echo "${BASH_REMATCH[1]}"; else echo ":0"; fi
            ;;
        auth)
            if [[ $cmd =~ -auth[[:space:]]+([^[:space:]]+) ]]; then echo "${BASH_REMATCH[1]}"; fi
            ;;
    esac
}

probe() {  # 返回 0 = X 有响应
    local pid=$1
    DISPLAY="$(xorg_field "$pid" display)" \
    XAUTHORITY="$(xorg_field "$pid" auth)" \
        timeout "$PROBE_TIMEOUT" xset q >/dev/null 2>&1
}

# 命令模式面板的 TE 帧计数（主 DSI），正常约 85-90/秒
read_vsync() {
    timeout 2 cat "$DRM_CARD/encoder-0/status" 2>/dev/null |
        sed -n '1s/.*vsync:[[:space:]]*\([0-9][0-9]*\).*/\1/p'
}

read_dpms() {
    cat "$CONNECTOR/dpms" 2>/dev/null
}

irq_sample() {
    grep -E 'dsi|dpu|mdp|disp|vsync' /proc/interrupts 2>/dev/null | \
        sed -E 's/^[[:space:]]*([0-9]+):[[:space:]]+//' | \
        awk '{name=""; for(i=2;i<=NF;i++) name=name" "$i; print $1, name}'
}

dump() {
    local reason=$1 pid=${2:-0} ts file
    ts=$(date '+%Y%m%d-%H%M%S')
    file="$OUTDIR/freeze-$ts.log"

    {
        echo "=== display freeze evidence ==="
        echo "reason : $reason"
        echo "time   : $(date '+%F %T %Z')"
        echo "kernel : $(uname -r)"
        echo "uptime : $(uptime -p 2>/dev/null || cat /proc/uptime)"
        echo "load   : $(cut -d' ' -f1-3 /proc/loadavg)"
        echo "xorg   : pid=$pid display=$(xorg_field "$pid" display) auth=$(xorg_field "$pid" auth)"
        echo "vsync  : $(read_vsync)  dpms=$(read_dpms)"
        echo

        if [ "$pid" != 0 ] && [ -d "/proc/$pid" ]; then
            echo "--- Xorg main-thread kernel stack (/proc/$pid/stack) ---"
            timeout 3 cat "/proc/$pid/stack" 2>&1
            echo

            echo "--- Xorg threads: comm / state / wchan / stack ---"
            for t in /proc/"$pid"/task/*/; do
                [ -d "$t" ] || continue
                tid=${t%/}; tid=${tid##*/}
                printf 'tid=%s comm=%s state=%s wchan=%s\n' \
                    "$tid" \
                    "$(cat "$t/comm" 2>/dev/null)" \
                    "$(awk '{print $3}' "$t/stat" 2>/dev/null)" \
                    "$(cat "$t/wchan" 2>/dev/null)"
                timeout 2 cat "$t/stack" 2>&1 | sed 's/^/    /'
            done
            echo
        else
            echo "--- Xorg 进程不存在（pid=$pid） ---"
            echo
        fi

        echo "--- top processes (pid ppid stat wchan etimes pcpu args) ---"
        timeout 5 ps -eo pid,ppid,stat,wchan:32,etimes,pcpu,args --sort=-pcpu 2>&1 | head -40
        echo

        echo "--- uninterruptible (D) tasks ---"
        timeout 5 ps -eo pid,ppid,stat,wchan:36,args 2>&1 | awk 'NR==1 || $3 ~ /^D/' | head -30
        echo

        echo "--- display interrupts, sample 1 ---"
        irq_sample
        sleep 1
        echo "--- display interrupts, sample 2 (1s later) ---"
        irq_sample
        echo

        echo "--- DRM encoder/CRTC status ---"
        timeout 3 cat "$DRM_CARD/encoder-0/status" 2>&1 | head -5
        timeout 3 cat "$DRM_CARD/crtc-0/status" 2>&1 | head -12
        echo

        echo "--- vsync samples (1s apart; frozen = 显示链路停住) ---"
        for i in 1 2 3; do
            printf 't%s: %s\n' "$i" "$(read_vsync)"
            sleep 1
        done
        echo

        echo "--- DRM connector state ---"
        for f in /sys/class/drm/*/status /sys/class/drm/*/enabled /sys/class/drm/*/dpms; do
            [ -r "$f" ] && printf '%s = %s\n' "$f" "$(cat "$f" 2>/dev/null)"
        done
        echo

        echo "--- backlight ---"
        for f in /sys/class/backlight/*/brightness /sys/class/backlight/*/actual_brightness; do
            [ -r "$f" ] && printf '%s = %s\n' "$f" "$(cat "$f" 2>/dev/null)"
        done
        echo

        echo "--- kernel log (last 400 lines) ---"
        timeout 10 journalctl -k -n 400 --no-pager -o short-precise 2>&1 || \
            timeout 5 dmesg 2>&1 | tail -400
    } >"$file" 2>&1

    chmod 644 "$file" 2>/dev/null
    prune
}

prune() {
    ls -1t "$OUTDIR"/freeze-*.log 2>/dev/null | tail -n +$((KEEP + 1)) | while read -r f; do
        rm -f -- "$f"
    done
}

latest_dump() { ls -1t "$OUTDIR"/freeze-*.log 2>/dev/null | head -n1; }

main() {
    local fails=0 pid x_ok state=ok prev_vsync="" stall=0 vsync dpms had_pid=""

    log "watchdog started (interval=${INTERVAL}s x-threshold=${FAIL_THRESHOLD} vsync-stall=${VSYNC_STALL_LIMIT}s outdir=$OUTDIR)"

    while :; do
        if [ -f /run/systemd/shutdown/scheduled ]; then
            log "system is shutting down, watchdog exit"
            exit 0
        fi

        pid=$(xorg_pid)

        # C. Xorg 消失
        if [ -z "$pid" ]; then
            if [ -n "$had_pid" ] && [ "$state" = ok ]; then
                log "Xorg disappeared (was pid $had_pid) — dumping"
                dump "Xorg disappeared (was pid $had_pid)" "$had_pid"
                log "evidence written: $(latest_dump)"
                state=gone
            fi
            had_pid=""
            fails=0; stall=0; prev_vsync=""
            sleep "$INTERVAL"
            continue
        fi

        if [ -n "$had_pid" ] && [ "$had_pid" != "$pid" ]; then
            state=ok
        fi
        had_pid=$pid

        # A. X 是否响应
        if probe "$pid"; then x_ok=1; else x_ok=0; fi

        if [ "$x_ok" = 1 ]; then
            if [ "$state" = xfrozen ]; then
                log "X recovered (pid $pid)"
                state=ok
            fi
            fails=0
        else
            fails=$((fails + 1))
            if [ "$state" = ok ] && [ "$fails" -ge "$FAIL_THRESHOLD" ]; then
                state=xfrozen
                log "X unresponsive for ~$((fails * (PROBE_TIMEOUT + INTERVAL)))s (pid $pid) — dumping"
                dump "X unresponsive ~$((fails * (PROBE_TIMEOUT + INTERVAL)))s" "$pid"
                log "evidence written: $(latest_dump)"
            fi
        fi

        # B. X 活着但画面不刷新（vsync 计数停住）
        if [ "$state" = ok ] && [ "$x_ok" = 1 ]; then
            dpms=$(read_dpms)
            if [ "$dpms" = "On" ]; then
                vsync=$(read_vsync)
                if [ -z "$vsync" ]; then
                    stall=0
                elif [ "$vsync" = "$prev_vsync" ]; then
                    stall=$((stall + 1))
                    if [ "$stall" -ge "$VSYNC_STALL_LIMIT" ]; then
                        state=stalled
                        log "display stalled: vsync frozen for ${stall}s while X responsive — dumping"
                        dump "display stalled (vsync frozen ${stall}s, X responsive)" "$pid"
                        log "evidence written: $(latest_dump)"
                    fi
                else
                    stall=0
                fi
                prev_vsync=$vsync
            else
                stall=0; prev_vsync=""
            fi
        elif [ "$state" = stalled ]; then
            vsync=$(read_vsync)
            if [ -n "$vsync" ] && [ "$vsync" != "$prev_vsync" ]; then
                log "display recovered (vsync advancing again)"
                state=ok
                stall=0
            fi
            prev_vsync=$vsync
        fi

        sleep "$INTERVAL"
    done
}

case "${1:-}" in
    --dump-now)
        pid=$(xorg_pid)
        dump "manual test (--dump-now)" "${pid:-0}"
        echo "written: $(latest_dump)"
        ;;
    *)
        main
        ;;
esac
