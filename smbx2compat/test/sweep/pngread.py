# pngread.py: load(path) -> (width, height, rows of (r, g, b)) for 8-bit RGB/RGBA PNGs
# (the game's screenshots), with only the standard library
import zlib,struct
def load(p):
    d=open(p,'rb').read(); i=8; idat=b''
    while i<len(d):
        n,t=struct.unpack('>I4s',d[i:i+8]); c=d[i+8:i+8+n]; i+=12+n
        if t==b'IHDR': w,h,bd,ct=struct.unpack('>IIBB',c[:10])
        elif t==b'IDAT': idat+=c
    bpp={2:3,6:4}[ct]; raw=zlib.decompress(idat); st=w*bpp; prev=bytearray(st); out=[]; o=0
    for y in range(h):
        f=raw[o]; o+=1; cur=bytearray(raw[o:o+st]); o+=st
        for x in range(st):
            a=cur[x-bpp] if x>=bpp else 0; b=prev[x]; cc=prev[x-bpp] if x>=bpp else 0
            if f==1: cur[x]=(cur[x]+a)&255
            elif f==2: cur[x]=(cur[x]+b)&255
            elif f==3: cur[x]=(cur[x]+(a+b)//2)&255
            elif f==4:
                pp=a+b-cc; pa,pb,pc=abs(pp-a),abs(pp-b),abs(pp-cc)
                cur[x]=(cur[x]+(a if pa<=pb and pa<=pc else b if pb<=pc else cc))&255
        out.append([tuple(cur[x*bpp:x*bpp+3]) for x in range(w)]); prev=cur
    return w,h,out
