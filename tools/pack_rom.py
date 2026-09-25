#!/usr/bin/env python3
"""Pull the constant tables (symbol rom_init) out of a compiled ARM object and
write them compressed as a C byte list, for the calculator build.

Format (bits, most significant first): per item a flag bit, then either a
literal byte (flag 0) or a match (flag 1): 8 bits distance - 1 and 4 bits
length - 2. The parse is optimal. Standard library only.

Usage: pack_rom.py crossy.o rom.inc
"""
import struct
import sys

MIN_LEN, MAX_LEN, MAX_DIST = 2, 17, 256


def symbol_bytes(path, name):
    d = open(path, "rb").read()
    shoff, = struct.unpack_from("<I", d, 0x20)
    shentsize, shnum = struct.unpack_from("<HH", d, 0x2E)
    secs = [struct.unpack_from("<IIIIIIIIII", d, shoff + i * shentsize) for i in range(shnum)]
    for s in secs:
        if s[1] != 2:  # SHT_SYMTAB
            continue
        strtab = secs[s[6]]
        for k in range(s[5] // 16):
            st_name, value, size, info, other, shndx = struct.unpack_from("<IIIBBH", d, s[4] + 16 * k)
            o = strtab[4] + st_name
            if d[o:d.index(b"\0", o)] == name:
                return d[secs[shndx][4] + value:secs[shndx][4] + value + size]
    raise SystemExit("symbol %s not found" % name.decode())


def pack(raw):
    n = len(raw)
    cost, step = [0] * (n + 1), [None] * n
    for i in range(n - 1, -1, -1):
        cost[i], step[i] = cost[i + 1] + 9, None
        for j in range(max(0, i - MAX_DIST), i):
            k = 0
            while i + k < n and k < MAX_LEN and raw[j + k] == raw[i + k]:
                k += 1
            for m in range(MIN_LEN, k + 1):
                if cost[i + m] + 13 < cost[i]:
                    cost[i], step[i] = cost[i + m] + 13, (i - j, m)
    bits, i = [], 0

    def put(v, k):
        bits.extend((v >> b) & 1 for b in range(k - 1, -1, -1))
    while i < n:
        if step[i] is None:
            put(raw[i], 9)
            i += 1
        else:
            dist, m = step[i]
            put(1, 1)
            put(dist - 1, 8)
            put(m - MIN_LEN, 4)
            i += m
    bits += [0] * (-len(bits) % 8 + 16)  # the decoder reads up to two bytes ahead
    return bytes(sum(b << (7 - k) for k, b in enumerate(bits[p:p + 8])) for p in range(0, len(bits), 8))


def unpack(data, n):  # reference decoder
    out, pos = bytearray(), 0

    def get(k):
        nonlocal pos
        v = 0
        for _ in range(k):
            v = v << 1 | (data[pos >> 3] >> (7 - (pos & 7)) & 1)
            pos += 1
        return v
    while len(out) < n:
        if get(1):
            dist, m = get(8) + 1, get(4) + MIN_LEN
            for _ in range(m):
                out.append(out[-dist])
        else:
            out.append(get(8))
    return bytes(out)


def main(src, dst):
    raw = symbol_bytes(src, b"rom_init")
    packed = pack(raw)
    assert unpack(packed, len(raw)) == raw
    with open(dst, "w") as f:
        for i in range(0, len(packed), 16):
            f.write(", ".join(str(b) for b in packed[i:i + 16]) + ",\n")
    print("%s: %d bytes of tables -> %d" % (dst, len(raw), len(packed)))


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
