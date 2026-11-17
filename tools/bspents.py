#!/usr/bin/env python3
"""Dump a Source BSP's entity lump (lump 0) as a list of key/value dicts.

  python -I bspents.py <map.bsp> [classname-substring ...]   -> prints matching entities
Used to derive class-restriction defaults for PF2 hunted maps (tools/hunted_rules.py).
"""
import lzma, re, struct, sys


def lump(path, index):
    d = open(path, "rb").read()
    ident, version = struct.unpack_from("<4si", d, 0)
    if ident != b"VBSP":
        raise ValueError("not a VBSP file")
    off, length = struct.unpack_from("<ii", d, 8 + index * 16)   # lump_t: fileofs, filelen
    raw = d[off:off + length]
    if raw[:4] == b"LZMA":   # Valve LZMA lump: 'LZMA', actualSize, lzmaSize, 5 property bytes, data
        actual, lzsize = struct.unpack_from("<II", raw, 4)
        props = raw[12:17]
        raw = lzma.decompress(props + struct.pack("<Q", actual) + raw[17:17 + lzsize], format=lzma.FORMAT_ALONE)
    return raw


def entities(path):
    text = lump(path, 0).decode("latin-1")
    ents = []
    for block in re.findall(r"\{(.*?)\}", text, re.S):
        e = {}
        for k, v in re.findall(r'"([^"]*)"\s+"([^"]*)"', block):
            e.setdefault(k, v) if k not in e else e.update({k + "#%d" % len(e): v})
        ents.append(e)
    return ents


if __name__ == "__main__":
    want = [w.lower() for w in sys.argv[2:]]
    for e in entities(sys.argv[1]):
        c = e.get("classname", "").lower()
        if not want or any(w in c for w in want):
            print(e)
