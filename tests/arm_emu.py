#!/usr/bin/env python3
"""Runs the real calculator build (ARM Cortex-M7 code, linked by nwlink) in the
Unicorn CPU emulator. EADK calls are serviced here, with the same scripted input,
clock and random numbers as host/host.c, so frames can be compared pixel by pixel.
Also reports executed instructions per frame (a CPU cost estimate).

Usage: arm_emu.py app.elf --frames N --shots 10,20 --keys "30:U,50:L" --out DIR
"""
import argparse
import struct

from elftools.elf.elffile import ELFFile
from unicorn import UC_ARCH_ARM, UC_HOOK_BLOCK, UC_HOOK_CODE, UC_MODE_MCLASS, UC_MODE_THUMB, Uc
from unicorn.arm_const import UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_SP

KEYS = {"L": 0, "U": 1, "D": 2, "R": 3, "O": 4, "B": 5, "E": 52}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("elf")
    ap.add_argument("--frames", type=int, default=60)
    ap.add_argument("--shots", default="")
    ap.add_argument("--keys", default="")
    ap.add_argument("--seed", type=int, default=2463534242)
    ap.add_argument("--out", default=".")
    a = ap.parse_args()
    shots = {int(s) for s in a.shots.split(",") if s}
    script = []
    for item in filter(None, a.keys.split(",")):
        rng, k = item.split(":")
        lo, hi = (rng.split("-") + [rng])[:2]
        script.append((int(lo), int(hi), KEYS[k]))

    uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
    elf = ELFFile(open(a.elf, "rb"))
    uc.mem_map(0x90000000, 0x800000)  # external flash (both firmware slots)
    uc.mem_map(0x20000000, 0x40000)
    uc.mem_map(0x30000000, 0x10000)  # process stack
    for seg in elf.iter_segments():
        if seg["p_type"] == "PT_LOAD" and seg["p_filesz"]:
            uc.mem_write(seg["p_paddr"], seg.data())
    syms = {s.name: s["st_value"] for s in elf.get_section_by_name(".symtab").iter_symbols()}

    st = {"frame": 0, "now": 0.0, "rng": a.seed & 0xFFFFFFFF, "insns": 0, "last": 0}
    screen = bytearray(320 * 240 * 2)

    def ret(uc, r0=None, r1=None):
        if r0 is not None:
            uc.reg_write(UC_ARM_REG_R0, r0 & 0xFFFFFFFF)
        if r1 is not None:
            uc.reg_write(UC_ARM_REG_R1, r1 & 0xFFFFFFFF)
        uc.reg_write(UC_ARM_REG_PC, uc.reg_read(UC_ARM_REG_LR))

    def push_rect(uc):
        r0, r1, p = (uc.reg_read(r) for r in (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2))
        x, y, w, h = r0 & 0xFFFF, r0 >> 16, r1 & 0xFFFF, r1 >> 16
        data = uc.mem_read(p, w * h * 2)
        for j in range(h):
            o = ((y + j) * 320 + x) * 2
            screen[o:o + w * 2] = data[j * w * 2:(j + 1) * w * 2]
        ret(uc)

    def dump(f):
        with open("%s/a%05d.ppm" % (a.out, f), "wb") as fp:
            fp.write(b"P6\n320 240\n255\n")
            out = bytearray()
            for i in range(0, len(screen), 2):
                c = screen[i] | screen[i + 1] << 8
                out += bytes((((c >> 11) & 31) * 255 // 31, ((c >> 5) & 63) * 255 // 63, (c & 31) * 255 // 31))
            fp.write(out)

    def vblank(uc):
        f = st["frame"]
        if f > 0 and f in shots:
            dump(f)
        if f > 0:
            print("frame %4d: %8d instructions" % (f, st["insns"] - st["last"]))
        st["last"] = st["insns"]
        st["frame"] = f + 1
        st["now"] += 1000.0 / 60.0
        if st["frame"] > a.frames:
            uc.emu_stop()
        ret(uc, 1)

    def keyboard(uc):
        s = 0
        for lo, hi, k in script:
            if lo <= st["frame"] <= hi:
                s |= 1 << k
        ret(uc, s, s >> 32)

    def millis(uc):
        t = int(st["now"])
        ret(uc, t, t >> 32)

    def random(uc):
        r = st["rng"]
        r ^= (r << 13) & 0xFFFFFFFF
        r ^= r >> 17
        r ^= (r << 5) & 0xFFFFFFFF
        st["rng"] = r
        ret(uc, r)

    handlers = {"eadk_display_push_rect": push_rect, "eadk_display_wait_for_vblank": vblank,
                "eadk_keyboard_scan": keyboard, "eadk_timing_millis": millis, "eadk_random": random}
    for name, fn in handlers.items():
        addr = syms[name] & ~1
        uc.hook_add(UC_HOOK_CODE, lambda uc, ad, sz, ud, fn=fn: fn(uc), begin=addr, end=addr)

    ninsn = {}

    def block(uc, addr, size, ud):
        n = ninsn.get((addr, size))
        if n is None:  # count Thumb-2 instructions: 32-bit ones start with 0b11101/0b1111x
            code, i, n = uc.mem_read(addr, size), 0, 0
            while i < size:
                i += 4 if (code[i + 1] >> 3) in (0x1D, 0x1E, 0x1F) else 2
                n += 1
            ninsn[(addr, size)] = n
        st["insns"] += n
    uc.hook_add(UC_HOOK_BLOCK, block)

    uc.reg_write(UC_ARM_REG_SP, 0x30010000)
    uc.reg_write(UC_ARM_REG_LR, 0xFFFFFFFF)
    uc.emu_start(syms["_start"] | 1, 0xFFFFFFFE)


if __name__ == "__main__":
    main()
