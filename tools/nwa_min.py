#!/usr/bin/env python3
"""Shrink a NumWorks .nwa (relocatable ARM ELF) without changing what gets installed.

- Resolves Thumb branches between functions of the same .text section
  (they are position independent), keeping only relocations the installer needs.
- Keeps only the symbols those relocations and the loader reference.
- Merges the symbol and section name tables and drops everything else.

Usage: nwa_min.py in.nwa out.nwa
"""
import struct
import sys

R_ARM_THM_CALL, R_ARM_THM_JUMP24 = 10, 30
SHT_SYMTAB, SHT_STRTAB, SHT_REL, SHT_NOBITS = 2, 3, 9, 8
KEEP_GLOBALS = {b"main", b"eadk_app_name", b"eadk_app_icon", b"eadk_api_level"}


def main(src, dst):
    d = bytearray(open(src, "rb").read())
    assert d[:4] == b"\x7fELF" and d[4] == 1 and d[5] == 1, "ELF32 LE expected"
    e_shoff, = struct.unpack_from("<I", d, 0x20)
    e_shentsize, e_shnum, e_shstrndx = struct.unpack_from("<HHH", d, 0x2E)
    secs = []
    for i in range(e_shnum):
        f = struct.unpack_from("<IIIIIIIIII", d, e_shoff + i * e_shentsize)
        secs.append(dict(zip("name type flags addr off size link info align entsize".split(), f)))
    shstr = secs[e_shstrndx]

    def secname(s):
        o = shstr["off"] + s["name"]
        return bytes(d[o:d.index(b"\0", o)])

    for s in secs:
        s["sname"] = secname(s)
    symtab_i = next(i for i, s in enumerate(secs) if s["type"] == SHT_SYMTAB)
    symtab = secs[symtab_i]
    strtab = secs[symtab["link"]]
    syms = []
    for i in range(symtab["size"] // 16):
        name, value, size, info, other, shndx = struct.unpack_from("<IIIBBH", d, symtab["off"] + 16 * i)
        o = strtab["off"] + name
        syms.append(dict(name=bytes(d[o:d.index(b"\0", o)]), value=value, size=size, info=info, other=other, shndx=shndx))
    text_i = next(i for i, s in enumerate(secs) if s["sname"] == b".text")
    text = secs[text_i]

    # 1. resolve intra-.text Thumb branches, keep the rest
    kept_rel = {}
    for i, s in enumerate(secs):
        if s["type"] != SHT_REL:
            continue
        target = s["info"]
        out = []
        for k in range(s["size"] // 8):
            off, info = struct.unpack_from("<II", d, s["off"] + 8 * k)
            typ, si = info & 255, info >> 8
            sym = syms[si]
            if target == text_i and typ in (R_ARM_THM_CALL, R_ARM_THM_JUMP24) and sym["shndx"] == text_i:
                p = text["off"] + off
                h, l = struct.unpack_from("<HH", d, p)
                # decode the addend held in the instruction (BL/B.W encoding T4)
                sbit = (h >> 10) & 1
                j1, j2 = (l >> 13) & 1, (l >> 11) & 1
                i1, i2 = 1 - (j1 ^ sbit), 1 - (j2 ^ sbit)
                imm = (sbit << 24) | (i1 << 23) | (i2 << 22) | ((h & 0x3FF) << 12) | ((l & 0x7FF) << 1)
                if sbit:
                    imm -= 1 << 25
                dest = (sym["value"] & ~1) + imm - off
                assert -(1 << 24) <= dest < (1 << 24) and dest % 2 == 0
                v = dest & ((1 << 25) - 1)
                sbit = (v >> 24) & 1
                i1, i2 = (v >> 23) & 1, (v >> 22) & 1
                j1, j2 = (1 - i1) ^ sbit, (1 - i2) ^ sbit
                h = (h & 0xF800) | (sbit << 10) | ((v >> 12) & 0x3FF)
                l = (l & 0xD000) | (j1 << 13) | (j2 << 11) | ((v >> 1) & 0x7FF)
                if typ == R_ARM_THM_CALL and not (sym["value"] & 1):
                    raise SystemExit("BL to ARM code not supported")
                struct.pack_into("<HH", d, p, h, l)
            else:
                out.append((off, typ, si))
        kept_rel[i] = out

    # 2. symbols still needed
    need = set()
    for rels in kept_rel.values():
        need.update(si for _, _, si in rels)
    for i, s in enumerate(syms):
        if s["name"] in KEEP_GLOBALS and s["shndx"] != 0:
            need.add(i)
    need.discard(0)

    # 3. new section list: drop symtab/strtab/shstrtab (rebuilt) and empty rel sections
    keep = [i for i, s in enumerate(secs) if i and s["type"] not in (SHT_SYMTAB, SHT_STRTAB)
            and not (s["type"] == SHT_REL and not kept_rel[i])]
    newidx = {old: n + 1 for n, old in enumerate(keep)}
    n_symtab = len(keep) + 1
    n_strtab = n_symtab + 1

    # one string table for symbol and section names; longer names first so
    # shorter ones can reuse their tails (".rel.text" holds ".text")
    names_needed = {syms[i]["name"] for i in need if (syms[i]["info"] & 15) != 3}
    names_needed |= {secs[i]["sname"] for i in keep} | {b".symtab", b".strtab"}
    strtab_b = bytearray(b"\0")
    for b in sorted(names_needed - {b""}, key=lambda b: (-len(b), b)):
        if strtab_b.find(b + b"\0") < 0:
            strtab_b.extend(b + b"\0")

    def addstr(b):
        return strtab_b.find(b + b"\0") if b else 0

    # symbols: null, locals first, then globals
    order = sorted(need, key=lambda i: (syms[i]["info"] >> 4) != 0)
    symmap = {0: 0}
    symbytes = bytearray(16)
    nlocal = 1
    for i in order:
        s = syms[i]
        sh = s["shndx"]
        if 0 < sh < 0xFF00:
            sh = newidx[sh]
        nm = 0 if (s["info"] & 15) == 3 else addstr(s["name"])  # section symbols are unnamed
        symbytes += struct.pack("<IIIBBH", nm, s["value"], s["size"], s["info"], s["other"], sh)
        symmap[i] = len(symmap)
        if (s["info"] >> 4) == 0:
            nlocal += 1

    names = {i: addstr(secs[i]["sname"]) for i in keep}
    rel_names = {}
    nm_symtab, nm_strtab = addstr(b".symtab"), addstr(b".strtab")

    body = bytearray(52)
    headers = [struct.pack("<IIIIIIIIII", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)]

    def align(a):
        while len(body) % a:
            body.append(0)

    for i in keep:
        s = secs[i]
        a = max(s["align"], 1)
        if s["type"] == SHT_REL:
            data = b"".join(struct.pack("<II", off, (symmap[si] << 8) | typ) for off, typ, si in kept_rel[i])
            link, info = n_symtab, newidx[s["info"]]
            a = 4
        else:
            data = b"" if s["type"] == SHT_NOBITS else bytes(d[s["off"]:s["off"] + s["size"]])
            link, info = s["link"], s["info"]
        align(a)
        off = len(body)
        body += data
        size = s["size"] if s["type"] == SHT_NOBITS else len(data)
        headers.append(struct.pack("<IIIIIIIIII", names[i], s["type"], s["flags"], s["addr"], off, size, link, info,
                                   a, s["entsize"] if s["type"] != SHT_REL else 8))
    align(4)
    off = len(body)
    body += symbytes
    headers.append(struct.pack("<IIIIIIIIII", nm_symtab, SHT_SYMTAB, 0, 0, off, len(symbytes), n_strtab, nlocal, 4, 16))
    off = len(body)
    body += strtab_b
    headers.append(struct.pack("<IIIIIIIIII", nm_strtab, SHT_STRTAB, 0, 0, off, len(strtab_b), 0, 0, 1, 0))
    align(4)
    shoff = len(body)
    body += b"".join(headers)
    hdr = bytearray(d[:52])
    struct.pack_into("<I", hdr, 0x20, shoff)
    struct.pack_into("<HHH", hdr, 0x2E, 40, len(headers), n_strtab)
    body[:52] = hdr
    open(dst, "wb").write(body)
    print("%s: %d -> %d bytes (%d relocations kept, %d symbols)" % (dst, len(d), len(body),
          sum(len(v) for v in kept_rel.values()), len(symmap)))


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
