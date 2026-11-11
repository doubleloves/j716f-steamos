import os, tarfile, sys, time

ROOT = r'D:\xiaoxinpadpro\restore\home\player\j716f-linux'
OUT = r'D:\xiaoxinpadpro\backup\kernel-src.tar.gz'
EXCLUDE_SUFFIX = ('.o', '.ko', '.cmd', '.a', '.tmp', '.order', '.d', '.mod', '.mod.c', '.o.d', '.o.cmd', '.su', '.o.tmp')
EXCLUDE_NAMES = {'vmlinux', 'System.map', 'vmlinux.o', '.git'}
count = 0
start = time.time()
with tarfile.open(OUT, 'w:gz', compresslevel=3) as tf:
    for dirpath, dirnames, filenames in os.walk(ROOT):
        # skip .git
        dirnames[:] = [d for d in dirnames if d != '.git']
        rel_dir = os.path.relpath(dirpath, ROOT)
        if rel_dir == '.':
            rel_dir = ''
        for fn in filenames:
            if fn in EXCLUDE_NAMES:
                continue
            if fn.endswith(EXCLUDE_SUFFIX):
                continue
            full = os.path.join(dirpath, fn)
            rel = os.path.join(rel_dir, fn) if rel_dir else fn
            try:
                if os.path.getsize(full) > 300 * 1024 * 1024:
                    continue
                tf.add(full, arcname=rel, recursive=False)
                count += 1
            except Exception as e:
                pass
        if count % 20000 == 0:
            print('files', count, 'elapsed', int(time.time()-start), flush=True)
print('done files', count, 'elapsed', int(time.time()-start), 'size', os.path.getsize(OUT), flush=True)
