import struct, hashlib, os

BASE = r'D:\xiaoxinpadpro\backup\boot-stable.img'
NEWDTB = r'D:\xiaoxinpadpro\backup\sm8250-lenovo-j716f-btfix.dtb'
OUT = r'D:\xiaoxinpadpro\backup\boot-btfix.img'

d = bytearray(open(BASE, 'rb').read())
assert d[:8] == b'ANDROID!'
(ks, ka, rs, ra, ss, sa, ta, ps, hv, ov) = struct.unpack_from('<10I', d, 8)
dtbsz = struct.unpack_from('<I', d, 1648)[0]
dtbaddr = struct.unpack_from('<Q', d, 1652)[0]
print('base size', len(d), 'hdr', hv, 'page', ps, 'ks', ks, 'rs', rs, 'dtbsz', dtbsz)

off = ps
kernel = bytes(d[off:off+ks]); off += ((ks+ps-1)//ps)*ps
ramdisk = bytes(d[off:off+rs]); off += ((rs+ps-1)//ps)*ps
second = bytes(d[off:off+ss]); off += ((ss+ps-1)//ps)*ps
olddtb = bytes(d[off:off+dtbsz])
print('cmdline:', d[64:64+512].split(b'\0')[0])
print('old dtb sha1', hashlib.sha1(olddtb).hexdigest())

newdtb = open(NEWDTB, 'rb').read()
assert newdtb[:4] == b'\xd0\x0d\xfe\xed'
print('new dtb size', len(newdtb), 'sha1', hashlib.sha1(newdtb).hexdigest())

def pad(b, ps):
    return b + b'\x00' * ((ps - len(b) % ps) % ps)

header = bytearray(d[:ps])
# update section sizes in header
struct.pack_into('<I', header, 8, len(kernel))
struct.pack_into('<I', header, 16, len(ramdisk))
struct.pack_into('<I', header, 24, len(second))
struct.pack_into('<I', header, 36, ps)
struct.pack_into('<I', header, 1648, len(newdtb))
# keep recovery_dtbo_size zero (offset 1632)
new = pad(bytes(header), ps) + pad(kernel, ps) + pad(ramdisk, ps) + pad(second, ps) + pad(newdtb, ps)
open(OUT, 'wb').write(new)
print('wrote', OUT, len(new))

# verify
d2 = open(OUT, 'rb').read()
(ks2, ka2, rs2, ra2, ss2, sa2, ta2, ps2, hv2, ov2) = struct.unpack_from('<10I', d2, 8)
ds2 = struct.unpack_from('<I', d2, 1648)[0]
print('verify ks', ks2, 'rs', rs2, 'ss', ss2, 'dtbsz', ds2, 'page', ps2)
o = ps2
k2 = d2[o:o+ks2]; o += ((ks2+ps2-1)//ps2)*ps2
r2 = d2[o:o+rs2]; o += ((rs2+ps2-1)//ps2)*ps2
s2 = d2[o:o+ss2]; o += ((ss2+ps2-1)//ps2)*ps2
dt2 = d2[o:o+ds2]
print('kernel sha1 match', hashlib.sha1(k2).hexdigest() == hashlib.sha1(kernel).hexdigest())
print('ramdisk match', hashlib.sha1(r2).hexdigest() == hashlib.sha1(ramdisk).hexdigest())
print('dtb sha1', hashlib.sha1(dt2).hexdigest())
print('cmdline:', d2[64:64+512].split(b'\0')[0])
