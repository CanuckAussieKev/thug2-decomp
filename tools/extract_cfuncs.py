import struct, sys, collections
d=open(sys.argv[1],'rb').read()
pe=struct.unpack_from('<I',d,0x3c)[0]; o=pe+24+struct.unpack_from('<H',d,pe+20)[0]
secs=[struct.unpack_from('<8sIIII',d,o+i*40) for i in range(struct.unpack_from('<H',d,pe+6)[0])]
def off(va):
    rva=va-0x400000
    for n,vs,v,rs,rp in secs:
        if v<=rva<v+rs: return rp+rva-v
def cstr(va):
    f=off(va); return d[f:d.index(b'\0',f)].decode('latin1')
rows=[]
for i in range(0x4e6):
    np,fp=struct.unpack_from('<II',d,off(0x67f3a0)+i*8)
    rows.append((fp,cstr(np)))
with open(sys.argv[2],'w') as f:
    f.write("address,script_name\n")
    for fp,n in rows: f.write(f"0x{fp:08x},{n}\n")
c=collections.Counter(fp for fp,_ in rows)
print("entries:",len(rows),"unique funcs:",len(c))
print("shared by several names:",[(hex(a),[n for f,n in rows if f==a]) for a,k in c.most_common(5) if k>1])
print("sample:",rows[:8])
