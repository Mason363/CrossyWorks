#!/usr/bin/env python3
"""PNG -> NWI (raw LZ4 block of RGB565 little-endian pixels), the icon format of
NumWorks apps. Uses an exhaustive match search, so icons come out much smaller
than with nwlink's encoder. Standard library only.

Usage: png2nwi.py icon.png icon.nwi
"""
import struct
import sys
import zlib


def read_png(path):
    data = open(path, "rb").read()
    assert data[:8] == b"\x89PNG\r\n\x1a\n", "not a PNG"
    pos, idat, w = 8, b"", 0
    while pos < len(data):
        n, typ = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        if typ == b"IHDR":
            w, h, depth, ctype, _, _, interlace = struct.unpack(">IIBBBBB", body)
            assert depth == 8 and ctype in (2, 6) and not interlace, "8-bit RGB/RGBA PNG expected"
            bpp = 3 if ctype == 2 else 4
        elif typ == b"IDAT":
            idat += body
        pos += 12 + n
    raw = zlib.decompress(idat)
    stride = w * bpp
    rows, prev = [], bytearray(stride)
    for y in range(h):
        f = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b, c = prev[i], prev[i - bpp] if i >= bpp else 0
            if f == 1: line[i] = (line[i] + a) & 255
            elif f == 2: line[i] = (line[i] + b) & 255
            elif f == 3: line[i] = (line[i] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(line)
        prev = line
    return w, h, bpp, rows


def lz4_block(src):
    n, out, anchor, i = len(src), bytearray(), 0, 0
    limit = n - 12  # a match may not start in the last 12 bytes

    def emit(lit, mlen, off):
        tok_l, tok_m = min(len(lit), 15), 0 if mlen is None else min(mlen - 4, 15)
        out.append(tok_l << 4 | tok_m)
        if len(lit) >= 15:
            r = len(lit) - 15
            while r >= 255: out.append(255); r -= 255
            out.append(r)
        out.extend(lit)
        if mlen is not None:
            out.extend(struct.pack("<H", off))
            if mlen - 4 >= 15:
                r = mlen - 4 - 15
                while r >= 255: out.append(255); r -= 255
                out.append(r)

    seen = {}  # 4-byte prefix -> positions

    def add(p):
        if p + 4 <= n:
            seen.setdefault(src[p:p + 4], []).append(p)

    while i < limit:
        best_len, best_off = 0, 0
        for j in reversed(seen.get(src[i:i + 4], ())):
            if i - j > 65535:
                break
            k = 4
            while i + k < n - 5 and src[j + k] == src[i + k]:
                k += 1
            if k > best_len:
                best_len, best_off = k, i - j
        if best_len >= 4 and i + best_len > n - 5:
            best_len = n - 5 - i
        if best_len >= 4:
            emit(src[anchor:i], best_len, best_off)
            for p in range(i, i + best_len):
                add(p)
            i += best_len
            anchor = i
        else:
            add(i)
            i += 1
    emit(src[anchor:], None, 0)
    return bytes(out)


def main(src, dst):
    w, h, bpp, rows = read_png(src)
    assert (w, h) == (55, 56), "NumWorks icons are 55x56"
    px = bytearray()
    for line in rows:
        for x in range(w):
            r, g, b = line[x * bpp:x * bpp + 3]
            px += struct.pack("<H", (r >> 3) << 11 | (g >> 2) << 5 | b >> 3)
    blob = lz4_block(bytes(px))
    open(dst, "wb").write(blob)
    print("%s: %d bytes of pixels -> %d bytes" % (dst, len(px), len(blob)))


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
