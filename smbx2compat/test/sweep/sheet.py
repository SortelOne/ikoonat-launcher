# sheet.py <out.png> <cols> <png>...: tile screenshots (each scaled to 320x180)
# into one contact sheet, to look through a run's shots in one image
import os, struct, sys, zlib
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pngread import load

def write(path, w, h, rows):
    raw = b''.join(b'\x00' + bytes(c for px in r for c in px) for r in rows)
    def chunk(t, d):
        return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
                + chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b''))

out, cols, files = sys.argv[1], int(sys.argv[2]), sys.argv[3:]
tw, th = 320, 180
W, H = tw * cols, th * ((len(files) + cols - 1) // cols)
canvas = [[(40, 40, 40)] * W for _ in range(H)]
for k, p in enumerate(files):
    w, h, px = load(p)
    ox, oy = (k % cols) * tw, (k // cols) * th
    for y in range(th):
        src, dst = px[y * h // th], canvas[oy + y]
        for x in range(tw):
            dst[ox + x] = src[x * w // tw]
write(out, W, H, canvas)
