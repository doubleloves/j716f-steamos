import struct

SRC = r'D:\xiaoxinpadpro\backup\boot-main.img'
OUT = r'D:\xiaoxinpadpro\backup\boot-stable.img'
d = bytearray(open(SRC, 'rb').read())
assert d[:8] == b'ANDROID!'
old = d[64:64+512].split(b'\x00')[0]
print('old cmdline:', old)
mask = b' systemd.mask=j716f-gamemode.service systemd.mask=j716f-gamemode-bootstrap.service'
new = old + mask
assert len(new) <= 512, len(new)
d[64:64+512] = new + b'\x00' * (512 - len(new))
# keep header id (sha1 of kernel/ramdisk/second/dtb) as-is
open(OUT, 'wb').write(d)
print('wrote', OUT, 'new cmdline:', d[64:64+512].split(b'\x00')[0])
print('size', len(d))
