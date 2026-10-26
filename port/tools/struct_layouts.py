#!/usr/bin/env python3
"""Generate port/gen/ng_layout32.h: convert structs the engine reads raw from its 32-bit data
files (pointers stored as 4 bytes) into the 64-bit in-memory layout.

Layouts come from the NDK clang record-layout dump for armv7a (ILP32; matches the original
Win32/x86 layout of these int/float/pointer structs, e.g. V3XCL_ITEM == 64) and aarch64 (LP64).

Conversion ops (NG_FIELD32.kind):
  0 copy   s32 bytes (s32 == s64)
  1 ptr    4-byte value zero-extended into an 8-byte slot (pointers, and unions of
           pointer|int: the int member then still reads the low 4 bytes)
  2 sint   4 -> 8 sign-extended (long)
Run: python3 port/tools/struct_layouts.py
"""
import os, pathlib, re, subprocess, sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
NDK = pathlib.Path.home() / "workspace/android-sdk/ndk/27.2.12479018"
CLANG = NDK / "toolchains/llvm/prebuilt/linux-x86_64/bin/clang"
SYSROOT = NDK / "toolchains/llvm/prebuilt/linux-x86_64/sysroot"

TYPES = ["V3XORI", "V3XOVI", "V3XTRI", "V3XTVI", "V3XMESH", "V3XPOLY", "V3XMATERIAL", "V3XNODE",
         "V3XCL", "V3XCL_ITEM", "V3XTWEEN", "V3XKEY", "V3XKEYEX", "V3XORI97", "V3XOVI97",
         "V3XLAYER97", "V3XLAYER", "V3XSCENE", "GXSPRITEFORMAT", "struct _sys_wadentry",
         "V3XVECTOR", "V3XUV", "V3XCL_MESH", "V3XCL_FACE", "V3XLIGHT", "V3XCAMERA", "SGRecordReplay", "SGRecordBuffer", "SGNetData"]
HEADERS = ["_rlx32.h", "_rlx.h", "sysresmx.h", "systools.h", "gx_struc.h", "gx_csp.h", "gx_flc.h",
           "gx_tools.h", "gx_init.h", "gx_rgb.h", "v3xdefs.h", "v3x_1.h", "v3x_2.h", "v3xtrig.h",
           "v3xcoll.h", "v3xmaps.h", "v3xrend.h", "v3xscene.h", "v3x_old.h", "lt_struc.h"]
LINE = re.compile(r"^\s*(\d+)(?::(\d+)-(\d+))? \|( +)(.*)$")


def wrapname(t):
    return re.sub(r"\W", "_", t.replace("struct ", "S_"))


def dump(target, types):
    tu = "\n".join(f'#include "{h}"' for h in HEADERS) + "\n"
    tu += "\n".join(f"struct ng_wrap_{wrapname(t)} {{ {t} v; }};" for t in types)
    tu += "\nint ng_layout_force[] = {\n" + ",\n".join(f"  (int)sizeof(struct ng_wrap_{wrapname(t)})" for t in types) + "\n};\n"
    src = pathlib.Path(os.environ.get("TMPDIR", "/tmp")) / f"ng_layout_{target}.c"
    src.write_text(tu)
    inc = ["source/rlx32/include", "source/src", "source/rlx32/src", "source/rlx32/src/linux", "port/compat", "port/sdl2"]
    cmd = [str(CLANG), f"--target={target}", f"--sysroot={SYSROOT}", "-fsyntax-only", "-w", "-fsigned-char",
           "-D__LINUX__", "-DLINUX", "-DUSE_OPENGL", "-DNG_SDL2", "-Xclang", "-fdump-record-layouts"]
    cmd += [f"-I{ROOT / i}" for i in inc] + [str(src)]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        print(r.stderr[:4000]); sys.exit(1)
    recs = {}
    for b in r.stdout.split("*** Dumping AST Record Layout")[1:]:
        rows, name, size, base = [], None, None, 0
        for l in b.strip("\n").splitlines():
            m = LINE.match(l)
            if m:
                depth = len(m.group(4))
                if name is None:
                    name = m.group(5).strip(); base = depth; continue
                rows.append([(depth - base) // 2, int(m.group(1)), m.group(2) is not None, m.group(5).strip()])
            else:
                ms = re.search(r"\[sizeof=(\d+)", l)
                if ms and size is None:
                    size = int(ms.group(1))
        if name and name not in recs:
            recs[name] = (size, rows)
    return recs


def tree(rows):
    root = {"kids": [], "depth": 0, "body": "", "off": 0}
    stack = [root]
    for d, off, bf, body in rows:
        n = {"depth": d, "off": off, "bf": bf, "body": body, "kids": []}
        while stack[-1]["depth"] >= d:
            stack.pop()
        stack[-1]["kids"].append(n)
        stack.append(n)
    return root


def tyof(body):
    return body.rsplit(" ", 1)[0] if " " in body else body


def has_ptr(n):
    return "*" in tyof(n["body"]) or any(has_ptr(k) for k in n["kids"])


def shift(n, d):
    n = dict(n); n["off"] = n["off"] + d; n["kids"] = [shift(k, d) for k in n["kids"]]; return n


class Gen:
    def __init__(self, recs32, recs64):
        self.ops, self.need, self.r32, self.r64 = [], set(), recs32, recs64

    def add(self, o32, o64, s32, s64, kind):
        if kind == 0 and s32 <= 0:
            return
        self.ops.append([o32, o64, s32, s64, kind])

    def node(self, n32, n64, e32, e64):
        o32, o64 = n32["off"], n64["off"]
        s32, s64 = e32 - o32, e64 - o64
        body = n32["body"]; ty = tyof(body)
        if s32 == s64 and not has_ptr(n32):
            self.add(o32, o64, s32, s64, 0); return
        if body.startswith("union "):
            if has_ptr(n32) and (s32, s64) == (4, 8):
                self.add(o32, o64, 4, 8, 1); return
            if has_ptr(n32):
                # tagged union of structs (e.g. V3XCL_ITEM): raw copy, caller converts by tag
                print(f"  note: tagged union copied raw ({s32}/{s64} B): {body[:60]}")
                self.add(o32, o64, min(s32, s64), min(s32, s64), 0); return
            self.add(o32, o64, min(s32, s64), min(s32, s64), 0); return
        arr = re.search(r"\[(\d+)\]$", ty)
        if arr and not n32["kids"]:
            cnt = int(arr.group(1)); el = ty[:arr.start()].strip()
            if el.endswith("*"):
                for i in range(cnt):
                    self.add(o32 + 4 * i, o64 + 8 * i, 4, 8, 1)
                return
            key = "struct ng_wrap_" + wrapname(el)
            if key not in self.r32:
                self.need.add(el); return
            es32, es64 = self.r32[key][0], self.r64[key][0]
            if es32 == es64:
                self.add(o32, o64, s32, s64, 0) if s32 == s64 else self.add(o32, o64, es32 * cnt, es64 * cnt, 0)
                return
            for i in range(cnt):
                self.record(key, o32 + es32 * i, o64 + es64 * i)
            return
        if "*" in ty:
            self.add(o32, o64, 4, 8, 1); return
        if n32["kids"]:
            self.children(n32["kids"], n64["kids"], e32, e64); return
        if re.search(r"\blong\b", ty) and s32 >= 4 and s64 >= 8:
            self.add(o32, o64, 4, 8, 1 if "unsigned" in ty else 2); return
        self.add(o32, o64, min(s32, s64), min(s32, s64), 0)

    def children(self, k32, k64, e32, e64):
        assert len(k32) == len(k64)
        for i, (a, b) in enumerate(zip(k32, k64)):
            self.node(a, b, k32[i + 1]["off"] if i + 1 < len(k32) else e32,
                      k64[i + 1]["off"] if i + 1 < len(k64) else e64)

    def record(self, key, base32, base64):
        r32, r64 = self.r32[key], self.r64[key]
        v32 = shift(tree(r32[1])["kids"][0], base32)
        v64 = shift(tree(r64[1])["kids"][0], base64)
        self.node(v32, v64, base32 + r32[0], base64 + r64[0])


def main():
    types = list(TYPES)
    for _ in range(6):
        d32 = dump("armv7a-linux-androideabi21", types)
        d64 = dump("aarch64-linux-android21", types)
        need, results = set(), {}
        for t in TYPES:
            key = "struct ng_wrap_" + wrapname(t)
            g = Gen(d32, d64)
            g.record(key, 0, 0)
            need |= g.need
            results[t] = (d32[key][0], d64[key][0], g.ops)
        new = sorted(n for n in need if n not in types)
        if not new:
            break
        print("adding element types:", new)
        types += new
    else:
        sys.exit("unresolved element types")
    out = ["/* Generated by port/tools/struct_layouts.py - do not edit. */", "#pragma once",
           "typedef struct { unsigned short o32, o64, s32, s64; unsigned char kind; } NG_FIELD32;",
           "typedef struct { const char *name; unsigned short size32, size64, n; const NG_FIELD32 *f; } NG_LAYOUT32;", ""]
    for t in TYPES:
        s32, s64, ops = results[t]
        merged = []
        for o in ops:
            if merged and o[4] == 0 and merged[-1][4] == 0 and merged[-1][0] + merged[-1][2] == o[0] and merged[-1][1] + merged[-1][3] == o[1]:
                merged[-1][2] += o[2]; merged[-1][3] += o[3]
            else:
                merged.append(list(o))
        cov32 = sum(o[2] for o in merged)
        assert cov32 <= s32, (t, cov32, s32)
        assert all(o[0] + o[2] <= s32 and o[1] + o[3] <= s64 for o in merged), t
        cn = wrapname(t)
        out.append(f"static const NG_FIELD32 ng_f32_{cn}[] = {{")
        out += ["  {%d,%d,%d,%d,%d}," % tuple(o) for o in merged]
        out.append("};")
        out.append(f"static const NG_LAYOUT32 ng_l32_{cn} = {{\"{t}\", {s32}, {s64}, {len(merged)}, ng_f32_{cn}}};")
        nptr = sum(1 for o in merged if o[4] == 1)
        print(f"{t:22s} 32-bit {s32:4d} B -> 64-bit {s64:4d} B  ops {len(merged):3d}  ptr slots {nptr:3d}  bytes {cov32}/{s32}")
    gen = ROOT / "port/gen"
    gen.mkdir(exist_ok=True)
    (gen / "ng_layout32.h").write_text("\n".join(out) + "\n")


if __name__ == "__main__":
    main()
