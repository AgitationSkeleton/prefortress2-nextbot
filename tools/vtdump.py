#!/usr/bin/env python3
"""Dump a class vtable from PF2's unstripped Linux server.so, with demangled names.

Windows (MSVC) index is normally the Linux index minus 1, because GCC emits two
destructor slots where MSVC emits one. Overloaded virtuals can be reordered by
MSVC, so treat overloads with care and validate at runtime.

  python -I vtdump.py <server.so> <ClassName> [filter]
Requires GNU binutils (readelf, c++filt) on PATH, or set BINUTILS to their directory.
"""
import os, shutil, struct, subprocess, sys, re

BIN = os.environ.get("BINUTILS")


def tool(name):
    name = name.replace(".exe", "")
    found = shutil.which(name, path=BIN) if BIN else shutil.which(name)
    if not found:
        sys.exit("%s not found (install binutils or set BINUTILS)" % name)
    return found


def run(*a):
    return subprocess.run([tool(a[0])] + list(a[1:]), capture_output=True, text=True, errors="replace").stdout


def main():
    so, cls = sys.argv[1], sys.argv[2]
    flt = sys.argv[3].lower() if len(sys.argv) > 3 else None
    data = open(so, "rb").read()
    # sections: name -> (addr, offset)
    secs = []
    for m in re.finditer(r"\]\s+(\S+)\s+\S+\s+([0-9a-f]{8})\s+([0-9a-f]{6,8})\s+([0-9a-f]{6,8})", run("readelf.exe", "-SW", so)):
        secs.append((m.group(1), int(m.group(2), 16), int(m.group(3), 16), int(m.group(4), 16)))

    def v2o(addr):
        for n, a, o, s in secs:
            if a and a <= addr < a + s:
                return addr - a + o
        return None

    syms, byaddr = {}, {}
    for line in run("readelf.exe", "-sW", so).splitlines():
        p = line.split()
        if len(p) >= 8 and p[0].endswith(":"):
            try:
                val, size = int(p[1], 16), int(p[2])
            except ValueError:
                continue
            syms[p[7]] = (val, size)
            if p[3] == "FUNC" and val:
                byaddr.setdefault(val, p[7])
    relsym = {}
    for line in run("readelf.exe", "-rW", so).splitlines():
        p = line.split()
        if len(p) >= 5 and p[2] == "R_386_32":
            relsym[int(p[0], 16)] = p[4]
    mangled = "_ZTV%d%s" % (len(cls), cls)
    if mangled not in syms:
        sys.exit("no vtable symbol " + mangled)
    va, size = syms[mangled]
    off = v2o(va)
    names = []
    for i in range(2, size // 4):          # skip offset-to-top, typeinfo
        slot = va + i * 4
        if slot in relsym:
            names.append(relsym[slot])
        else:
            ptr = struct.unpack_from("<I", data, off + i * 4)[0]
            names.append(byaddr.get(ptr, "0x%08x" % ptr))
    dem = subprocess.run([tool("c++filt")], input="\n".join(names), capture_output=True, text=True).stdout.splitlines()
    for i, d in enumerate(dem):
        if flt and flt not in d.lower():
            continue
        print("linux %4d  win~%4d  %s" % (i, i - 1, d))


if __name__ == "__main__":
    main()
