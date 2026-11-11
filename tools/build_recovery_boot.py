import gzip, struct, os, hashlib

SRC = r'D:\xiaoxinpadpro\backup\boot-main.img'
OUT = r'D:\xiaoxinpadpro\backup\boot-recovery.img'

d = open(SRC, 'rb').read()
assert d[:8] == b'ANDROID!'
(ks, ka, rs, ra, ss, sa, ta, ps, hv, ov) = struct.unpack_from('<10I', d, 8)
dtbsz = struct.unpack_from('<I', d, 1648)[0]
dtbaddr = struct.unpack_from('<Q', d, 1652)[0]
off = ps
kernel = d[off:off+ks]; off += ((ks+ps-1)//ps)*ps
ramdisk = d[off:off+rs]; off += ((rs+ps-1)//ps)*ps
second = d[off:off+ss]; off += ((ss+ps-1)//ps)*ps
dtb = d[off:off+dtbsz]
print('kernel', len(kernel), 'ramdisk', len(ramdisk), 'dtb', len(dtb))

dec = gzip.decompress(ramdisk)
entries = []
i = 0
while i + 110 <= len(dec):
    hdr = dec[i:i+110]
    if hdr[:6] not in (b'070701', b'070702'):
        break
    fs = int(hdr[54:62].decode(), 16)
    ns = int(hdr[94:102].decode(), 16)
    name = dec[i+110:i+110+ns-1]
    ds = (i + 110 + ns + 3) & ~3
    content = dec[ds:ds+fs]
    entries.append([hdr, name, content])
    i = ds + fs
    i = (i + 3) & ~3
print('entries', len(entries))

fix = b'''
# ---- recovery: re-enable NetworkManager WiFi (nmcli radio wifi off persists in state file) ----
mkdir -p /newroot/var/lib/NetworkManager
cat > /newroot/var/lib/NetworkManager/NetworkManager.state <<'NMS'
[main]
NetworkingEnabled=true
WirelessEnabled=true
WWANEnabled=true
NMS
rfkill unblock wifi 2>/dev/null || true
rfkill unblock all 2>/dev/null || true
echo "recovery: NetworkManager state written"
cat /newroot/var/lib/NetworkManager/NetworkManager.state
# also keep Bluetooth disabled to avoid boot-time BT hang loops
ln -sf /dev/null /newroot/etc/systemd/system/bluetooth.service
rm -rf /newroot/etc/systemd/system/bluetooth.service.d
echo "recovery: bluetooth.service masked"
'''

changed = False
for e in entries:
    hdr, name, content = e
    if name == b'init':
        text = content
        marker = b'            sync\n            exec switch_root /newroot /sbin/init'
        assert marker in text, 'switch_root marker not found'
        text = text.replace(marker, fix + marker, 1)
        e[2] = text
        changed = True
        print('patched init ->', len(text))
assert changed

# rebuild newc cpio
out = bytearray()
ino = 300000
for hdr, name, content in entries:
    if name == b'TRAILER!!!':
        continue
    ns = len(name) + 1
    h = bytearray(hdr)
    # update filesize (offset 54, 8 hex) and namesize (94)
    h[54:62] = f'{len(content):08x}'.encode()
    h[94:102] = f'{ns:08x}'.encode()
    out += bytes(h) + name + b'\x00'
    pad = (-(110 + ns)) % 4
    out += b'\x00' * pad
    out += content
    out += b'\x00' * ((-len(content)) % 4)
# trailer
h = bytearray(entries[-1][0])
name = b'TRAILER!!!'
ns = len(name) + 1
h[54:62] = b'00000000'
h[94:102] = f'{ns:08x}'.encode()
out += bytes(h) + name + b'\x00' + b'\x00' * ((-ns) % 4)
print('new cpio', len(out))

new_ram = gzip.compress(bytes(out), compresslevel=9, mtime=0)
print('new ramdisk gz', len(new_ram))
# align to page
def pad4(b, ps):
    return b + b'\x00' * ((ps - len(b) % ps) % ps)

kernel_p = pad4(kernel, ps)
ram_p = pad4(new_ram, ps)
second_p = pad4(second, ps) if second else b''
dtb_p = pad4(dtb, ps)

new = bytearray()
new += bytearray(d[:1632])  # header up to recovery_dtbo fields
# rewrite header fields
struct.pack_into('<I', new, 8, len(kernel))
struct.pack_into('<I', new, 16, len(new_ram))
struct.pack_into('<I', new, 24, len(second))
struct.pack_into('<I', new, 36, ps)
# recovery_dtbo_size stays 0; header_size at 1644, dtb_size 1648, dtb_addr 1652
# rebuild tail of header from original offsets
tail_header = bytearray(d[1632:ps])
struct.pack_into('<I', tail_header, 1648-1632, len(dtb))  # dtb size
# dtb addr unchanged
new += tail_header
new = pad4(bytes(new), ps)
new += kernel_p + ram_p + second_p + dtb_p
open(OUT, 'wb').write(bytes(new))
print('wrote', OUT, len(new))

# quick verify parse
d2 = open(OUT, 'rb').read()
(ks2, ka2, rs2, ra2, ss2, sa2, ta2, ps2, hv2, ov2) = struct.unpack_from('<10I', d2, 8)
dtbsz2 = struct.unpack_from('<I', d2, 1648)[0]
print('verify', ks2, rs2, ss2, dtbsz2, ps2)
