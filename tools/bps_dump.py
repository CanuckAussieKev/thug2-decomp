import sys, zlib, struct
p = open(sys.argv[1],'rb').read()
assert p[:4]==b'BPS1'
i=4
def num():
    global i
    d=0; s=1
    while True:
        x=p[i]; i+=1
        d+=(x&0x7f)*s
        if x&0x80: return d
        s<<=7; d+=s
ss, ts, ms = num(), num(), num()
meta=p[i:i+ms]; i+=ms
src_crc, tgt_crc, patch_crc = struct.unpack('<III', p[-12:])
print(f"source size={ss} target size={ts} meta={meta!r}")
print(f"source crc32={src_crc:08x} target crc32={tgt_crc:08x}")
out=0; sr=0; tr=0
end=len(p)-12
while i<end:
    d=num(); cmd=d&3; ln=(d>>2)+1
    if cmd==0: out+=ln
    elif cmd==1:
        data=p[i:i+ln]; i+=ln
        print(f"  TargetRead  @ file 0x{out:06x} len {ln}: {data.hex(' ')}"); out+=ln
    else:
        o=num(); o=(-1 if o&1 else 1)*(o>>1)
        if cmd==2: sr+=o; print(f"  SourceCopy  @ file 0x{out:06x} len {ln} from src 0x{sr:06x}"); sr+=ln
        else: tr+=o; print(f"  TargetCopy  @ file 0x{out:06x} len {ln} from tgt 0x{tr:06x}"); tr+=ln
        out+=ln
