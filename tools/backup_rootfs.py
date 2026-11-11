#!/usr/bin/env python3
"""Stream a compressed tar of the tablet root filesystem to this PC (Phase 0 backup).

Remote: sudo tar -C / ... | gzip -1
Local : write raw stream + sha256 sidecar, with progress logging.
"""
import hashlib
import os
import sys
import threading
import time

import paramiko

HOST, USER, PASS = "192.168.1.10", "player", "CHANGEME"
OUT = r"D:\xiaoxinpadpro\backup\ubuntu-rootfs-20261106.tar.gz"
OUT_SHA = OUT + ".sha256"

EXCLUDES = [
    "./proc", "./sys", "./dev", "./run", "./tmp", "./mnt", "./media",
    "./swapfile", "./home/j716f/.cache", "./var/cache/apt/archives",
]
excl = " ".join(f"--exclude='{p}'" for p in EXCLUDES)

REMOTE = (
    "bash -c \"echo CHANGEME | sudo -S -p '' tar -C / -cf - "
    + excl
    + " . | gzip -1; echo TARRC=${PIPESTATUS[1]} 1>&2\""
)


def main() -> int:
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    client = paramiko.SSHClient()
    client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    client.connect(HOST, 22, USER, PASS, timeout=20, banner_timeout=30,
                   auth_timeout=30, look_for_keys=False, allow_agent=False)
    transport = client.get_transport()
    transport.set_keepalive(30)
    stdin, stdout, stderr = client.exec_command(REMOTE, timeout=None)

    err_lines = []

    def drain_err():
        for line in iter(stderr.readline, ""):
            err_lines.append(line.rstrip())
            if "TARRC" in line or "tar:" in line or "error" in line.lower():
                print("[remote-stderr]", line.rstrip(), flush=True)

    t = threading.Thread(target=drain_err, daemon=True)
    t.start()

    h = hashlib.sha256()
    total = 0
    last = time.time()
    started = time.time()
    with open(OUT, "wb") as f:
        while True:
            chunk = stdout.read(4 * 1024 * 1024)
            if not chunk:
                break
            f.write(chunk)
            h.update(chunk)
            total += len(chunk)
            now = time.time()
            if now - last >= 15:
                mb = total / 1048576
                rate = mb / max(now - started, 1)
                print(f"progress: {mb:8.1f} MiB  ({rate:6.2f} MiB/s)", flush=True)
                last = now
    rc = stdout.channel.recv_exit_status()
    client.close()
    t.join(timeout=5)
    with open(OUT_SHA, "w", encoding="ascii") as f:
        f.write(f"{h.hexdigest()}  {os.path.basename(OUT)}\n")
    print(f"DONE size={total/1048576:.1f} MiB sha256={h.hexdigest()} rc={rc}")
    print("stderr tail:", err_lines[-5:])
    return 0 if rc == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
