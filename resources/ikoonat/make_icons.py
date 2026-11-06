#!/usr/bin/env python3
"""Ikoonat Launcher's icons from the pixel-art leaf (leaf.png).

Writes icon_<size>.png (16-1024) and thextech.icns (the bundle's icon file
keeps upstream's name, so existing build folders keep working) next to this
script. Sizes 128 and up scale the art by whole pixels (1x, 2x, 4x, 8x);
smaller ones are box-filtered from the 8x copy. Standard library only, plus
macOS's iconutil for the .icns.

    python3 resources/ikoonat/make_icons.py
"""
import os, shutil, struct, subprocess, sys, tempfile, zlib

HERE = os.path.dirname(os.path.abspath(__file__))
SIZES = [16, 32, 64, 128, 256, 512, 1024]
ART = 93 / 128   # the art's taller side, as a share of the icon (1x at 128)


def read_png(path):
    data = open(path, 'rb').read()
    assert data[:8] == b'\x89PNG\r\n\x1a\n', path
    pos, idat = 8, b''
    while pos < len(data):
        n, = struct.unpack('>I', data[pos:pos + 4])
        kind, body = data[pos + 4:pos + 8], data[pos + 8:pos + 8 + n]
        pos += 12 + n
        if kind == b'IHDR':
            w, h, depth, ctype, _, _, interlace = struct.unpack('>IIBBBBB', body)
            assert depth == 8 and ctype == 6 and not interlace, 'needs 8-bit RGBA'
        elif kind == b'IDAT':
            idat += body
    raw, stride, prev, rows, i = zlib.decompress(idat), w * 4, bytearray(w * 4), [], 0
    for _ in range(h):
        f, line = raw[i], bytearray(raw[i + 1:i + 1 + stride])
        i += 1 + stride
        for x in range(stride):
            a = line[x - 4] if x >= 4 else 0
            b = prev[x]
            c = prev[x - 4] if x >= 4 else 0
            if f == 1: line[x] = (line[x] + a) & 255
            elif f == 2: line[x] = (line[x] + b) & 255
            elif f == 3: line[x] = (line[x] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append([tuple(line[x:x + 4]) for x in range(0, stride, 4)])
        prev = line
    return rows


def write_png(path, rows):
    h, w = len(rows), len(rows[0])
    raw = b''.join(b'\0' + bytes(v for px in row for v in px) for row in rows)
    def chunk(kind, body):
        return struct.pack('>I', len(body)) + kind + body + struct.pack('>I', zlib.crc32(kind + body))
    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0))
                + chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))


def crop(rows):
    ys = [y for y, row in enumerate(rows) if any(px[3] for px in row)]
    xs = [x for x in range(len(rows[0])) if any(row[x][3] for row in rows)]
    return [row[xs[0]:xs[-1] + 1] for row in rows[ys[0]:ys[-1] + 1]]


def scale(rows, k):
    return [[px for px in row for _ in range(k)] for row in rows for _ in range(k)]


def shrink(rows, w, h):
    """Box filter (premultiplied alpha) to w x h."""
    sh, sw = len(rows), len(rows[0])
    out = []
    for y in range(h):
        y0, y1 = y * sh // h, max((y + 1) * sh // h, y * sh // h + 1)
        line = []
        for x in range(w):
            x0, x1 = x * sw // w, max((x + 1) * sw // w, x * sw // w + 1)
            r = g = b = a = 0
            for row in rows[y0:y1]:
                for pr, pg, pb, pa in row[x0:x1]:
                    r += pr * pa; g += pg * pa; b += pb * pa; a += pa
            n = (y1 - y0) * (x1 - x0)
            line.append((r // a, g // a, b // a, (a + n // 2) // n) if a else (0, 0, 0, 0))
        out.append(line)
    return out


def place(art, size):
    h, w = len(art), len(art[0])
    top, left = (size - h) // 2, (size - w) // 2
    blank = (0, 0, 0, 0)
    return [[blank] * size if not top <= y < top + h else
            [blank] * left + art[y - top] + [blank] * (size - left - w) for y in range(size)]


def main():
    art = crop(read_png(os.path.join(HERE, 'leaf.png')))
    big = scale(art, 8)
    icons = {}
    for size in SIZES:
        if size >= 128:
            icons[size] = place(scale(art, size // 128), size)
        else:
            th = max(1, round(size * ART))
            tw = max(1, round(th * len(art[0]) / len(art)))
            icons[size] = place(shrink(big, tw, th), size)
        write_png(os.path.join(HERE, 'icon_%d.png' % size), icons[size])
    if sys.platform == 'darwin':
        tmp = tempfile.mkdtemp()
        iconset = os.path.join(tmp, 'thextech.iconset')
        os.mkdir(iconset)
        for size in (16, 32, 128, 256, 512):   # the iconset's sizes, each with @2x
            name = os.path.join(iconset, 'icon_%dx%d' % (size, size))
            shutil.copy(os.path.join(HERE, 'icon_%d.png' % size), name + '.png')
            shutil.copy(os.path.join(HERE, 'icon_%d.png' % (size * 2)), name + '@2x.png')
        subprocess.check_call(['iconutil', '-c', 'icns', iconset, '-o', os.path.join(HERE, 'thextech.icns')])
        shutil.rmtree(tmp)


if __name__ == '__main__':
    main()
