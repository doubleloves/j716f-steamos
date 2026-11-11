#!/usr/bin/env python3
"""Sanitize the j716f-steamos repo: remove personal identifiers from text files."""
import os
import re
import sys

ROOT = sys.argv[1] if len(sys.argv) > 1 else "."
DELETE = [
    "scripts/j716f-netconsole.sh",
    "configs/systemd/system/j716f-netconsole.service",
]
SKIP_DIRS = {".git", "__pycache__", "node_modules"}
REPLACEMENTS = [
    ("/home/j716f", "/home/j716f"),
    (r"\byp@", "player@"),
    (r"\byp\b", "player"),
    ("CHANGEME", "CHANGEME"),
    ("SERIALNUMBER", "SERIALNUMBER"),
    (r"192\.168\.\d{1,3}\.\d{1,3}", "192.168.1.10"),
    (r"([0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}", "AA:BB:CC:DD:EE:FF"),
    ("j716f_key", "j716f_key"),
]

changed = 0
removed = 0
for dirpath, dirnames, filenames in os.walk(ROOT):
    dirnames[:] = [d for d in dirnames if d not in SKIP_DIRS]
    for fn in filenames:
        p = os.path.join(dirpath, fn)
        rel = os.path.relpath(p, ROOT).replace(os.sep, "/")
        if rel in DELETE:
            os.remove(p)
            removed += 1
            print(f"deleted {rel}")
            continue
        if fn.endswith((".pyc", ".bak", ".bak-rename", ".tgz", ".img", ".gz", ".simg", ".zip")):
            continue
        try:
            with open(p, "r", encoding="utf-8") as f:
                text = f.read()
        except (UnicodeDecodeError, OSError):
            continue
        new = text
        for pat, rep in REPLACEMENTS:
            new = re.sub(pat, rep, new)
        if new != text:
            with open(p, "w", encoding="utf-8", newline="") as f:
                f.write(new)
            changed += 1
print(f"sanitized {changed} files, removed {removed}")
