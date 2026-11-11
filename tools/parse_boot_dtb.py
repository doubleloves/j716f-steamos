import struct, hashlib, os

def parse_bootimg(path, base=0):
    with open(path,'rb') as f:
        data=f.read()
    d=data[base:base+4096]
    if d[:8]!=b'ANDROID!':
        return None
    (kernel_size,kernel_addr,ramdisk_size,ramdisk_addr,second_size,second_addr,
     tags_addr,page_size,hdr_ver,os_ver)=struct.unpack_from('<10I',d,8)
    name=d[48:64].split(b'\0')[0]
    cmdline=d[64:64+512].split(b'\0')[0]
    header_size=struct.unpack_from('<I',d,1644)[0] if hdr_ver>=2 else 0
    dtb_size=struct.unpack_from('<I',d,1648)[0] if hdr_ver>=2 else 0
    dtb_addr=struct.unpack_from('<Q',d,1652)[0] if hdr_ver>=2 else 0
    off=page_size
    kernel=data[base+off:base+off+kernel_size]; off+=((kernel_size+page_size-1)//page_size)*page_size
    ramdisk=data[base+off:base+off+ramdisk_size]; off+=((ramdisk_size+page_size-1)//page_size)*page_size
    second=data[base+off:base+off+second_size]; off+=((second_size+page_size-1)//page_size)*page_size
    dtb=data[base+off:base+off+dtb_size] if dtb_size else b''
    # verify dtb magic
    if dtb and dtb[:4]!=b'\xd0\x0d\xfe\xed':
        dtb=b''
    return dict(ver=hdr_ver,page=page_size,name=name,cmdline=cmdline,
                ksize=len(kernel),rsize=len(ramdisk),dsize=len(dtb),
                khash=hashlib.sha1(kernel).hexdigest(),
                rhash=hashlib.sha1(ramdisk).hexdigest(),
                dhash=hashlib.sha1(dtb).hexdigest(),
                kernel=kernel,ramdisk=ramdisk,dtb=dtb)

for path,base in [(r'D:\xiaoxinpadpro\backup\boot-main.img',0),
                  (r'D:\xiaoxinpadpro\backup\boot_a.raw',0)]:
    print('===',path)
    r=parse_bootimg(path,base)
    if not r:
        print('no ANDROID! at base'); 
        # scan first 1MB for ANDROID!
        with open(path,'rb') as f: d=f.read(1024*1024)
        i=d.find(b'ANDROID!')
        print('scan ANDROID! at',i)
        if i>=0:
            r=parse_bootimg(path,i); print(r['ver'] if r else None)
    if r:
        print({k:v for k,v in r.items() if k not in ('kernel','ramdisk','dtb')})
        out=r'D:\xiaoxinpadpro\backup\dtb-'+os.path.basename(path)+'.dtb'
        if r['dtb']:
            open(out,'wb').write(r['dtb']); print('saved',out)
