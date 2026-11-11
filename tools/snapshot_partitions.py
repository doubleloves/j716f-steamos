#!/usr/bin/env python3
"""Snapshot partition tables / metadata from the tablet to this PC.

Saves:
  backup/partitions/<name>            -- raw first 34 sectors of each /dev/sdX (GPT primary)
  backup/partitions/lsblk.txt         -- lsblk -P output
  backup/partitions/proc-partitions.txt
  backup/partitions/blkid.txt
  backup/partitions/by-partlabel.txt
  backup/partitions/cmdline.txt
  backup/partitions/fstab.txt
"""
import base64
import os

import paramiko

HOST, USER, PASS = "192.168.1.10", "player", "CHANGEME"
OUTDIR = r"D:\xiaoxinpadpro\backup\partitions"
SUDO = "echo CHANGEME | sudo -S -p '' "


def run(client, cmd, timeout=120):
    stdin, stdout, stderr = client.exec_command(cmd, timeout=timeout)
    out = stdout.read()
    err = stderr.read().decode("utf-8", "replace")
    rc = stdout.channel.recv_exit_status()
    return out, err, rc


def main() -> int:
    os.makedirs(OUTDIR, exist_ok=True)
    client = paramiko.SSHClient()
    client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    client.connect(HOST, 22, USER, PASS, timeout=20, banner_timeout=30,
                   auth_timeout=30, look_for_keys=False, allow_agent=False)

    text_cmds = {
        "lsblk.txt": "lsblk -P -o NAME,SIZE,TYPE,FSTYPE,PARTUUID,PARTLABEL,LABEL,MOUNTPOINT 2>/dev/null",
        "proc-partitions.txt": "cat /proc/partitions",
        "blkid.txt": SUDO + "blkid 2>/dev/null || true",
        "by-partlabel.txt": "for p in /dev/disk/by-partlabel/*; do echo \"$(basename $p) -> $(readlink -f $p)\"; done",
        "cmdline.txt": "cat /proc/cmdline",
        "fstab.txt": "cat /etc/fstab",
        "uname.txt": "uname -a; cat /etc/os-release",
    }
    for name, cmd in text_cmds.items():
        out, err, rc = run(client, cmd)
        with open(os.path.join(OUTDIR, name), "wb") as f:
            f.write(out)
        print(f"{name}: {len(out)} bytes rc={rc}")

    for dev in ("sda", "sdb", "sdc", "sdd", "sde", "sdf"):
        cmd = SUDO + f"dd if=/dev/{dev} bs=512 count=34 2>/dev/null | base64 -w0"
        out, err, rc = run(client, cmd)
        try:
            raw = base64.b64decode(out.strip())
        except Exception:
            raw = b""
        path = os.path.join(OUTDIR, f"{dev}-gpt-head.bin")
        with open(path, "wb") as f:
            f.write(raw)
        print(f"{dev}: saved {len(raw)} bytes rc={rc}")

    client.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
