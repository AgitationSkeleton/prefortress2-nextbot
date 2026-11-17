#!/usr/bin/env python3
"""Build the release zips (drop-in layout for the PF2 game directory).

  python tools/package.py plugin <version> <windows|linux> [--binary PATH] [--smx PATH] [--out DIR]
  python tools/package.py navs <version> <nav-dir> [--out DIR]

plugin: addons/metamod/pf2nextbot.vdf, addons/pf2nextbot/bin/<binary>, addons/pf2nextbot/data/*.cfg,
        addons/sourcemod/{plugins/pf2_hunted_classrestrict.smx, configs/hunted_classrestrict.cfg},
        cfg/pf2nextbot.example.cfg, and the license files under addons/pf2nextbot/.
navs:   addons/pf2nextbot/nav/*.nav
"""
import glob, os, re, sys, zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
VDF = '"Metamod Plugin"\n{\n\t"alias"\t"pf2nextbot"\n\t"file"\t"addons/pf2nextbot/bin/pf2nextbot"\n}\n'
BINARY = {"windows": "pf2nextbot.dll", "linux": "pf2nextbot_i486.so"}


def opt(args, name, default=None):
    return args[args.index(name) + 1] if name in args else default


def add(z, src, arc):
    z.write(src, arc)
    print("  %-60s %9d" % (arc, os.path.getsize(src)))


def plugin(args):
    version, plat = args[0], args[1]
    if plat not in BINARY:
        sys.exit("platform must be windows or linux")
    binary = opt(args, "--binary", os.path.join(ROOT, "build", BINARY[plat]))
    smx = opt(args, "--smx", os.path.join(ROOT, "build", "pf2_hunted_classrestrict.smx"))
    out = opt(args, "--out", os.path.join(ROOT, "dist"))
    for p in (binary, smx):
        if not os.path.isfile(p):
            sys.exit("missing " + p)
    os.makedirs(out, exist_ok=True)
    path = os.path.join(out, "prefortress2-nextbot-%s-%s.zip" % (version, plat))
    print(path)
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("addons/metamod/pf2nextbot.vdf", VDF)
        print("  addons/metamod/pf2nextbot.vdf")
        add(z, binary, "addons/pf2nextbot/bin/" + BINARY[plat])
        for f in sorted(glob.glob(os.path.join(ROOT, "package", "addons", "pf2nextbot", "data", "*.cfg"))):
            add(z, f, "addons/pf2nextbot/data/" + os.path.basename(f))
        add(z, smx, "addons/sourcemod/plugins/pf2_hunted_classrestrict.smx")
        add(z, os.path.join(ROOT, "sourcemod", "configs", "hunted_classrestrict.cfg"),
            "addons/sourcemod/configs/hunted_classrestrict.cfg")
        add(z, os.path.join(ROOT, "package", "cfg", "pf2nextbot.example.cfg"), "cfg/pf2nextbot.example.cfg")
        add(z, os.path.join(ROOT, "LICENSE"), "addons/pf2nextbot/LICENSE")
        add(z, os.path.join(ROOT, "thirdpartylegalnotices.txt"), "addons/pf2nextbot/thirdpartylegalnotices.txt")
        add(z, os.path.join(ROOT, "README.md"), "addons/pf2nextbot/README.md")


def navs(args):
    version, navdir = args[0], args[1]
    out = opt(args, "--out", os.path.join(ROOT, "dist"))
    files = sorted(glob.glob(os.path.join(navdir, "*.nav")))
    if not files:
        sys.exit("no .nav files in " + navdir)
    for f in files:
        if not re.fullmatch(r"[A-Za-z0-9_.\-]+\.nav", os.path.basename(f)):
            sys.exit("unexpected nav file name: " + os.path.basename(f))
    os.makedirs(out, exist_ok=True)
    path = os.path.join(out, "prefortress2-nextbot-navmeshes-%s.zip" % version)
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as z:
        for f in files:
            z.write(f, "addons/pf2nextbot/nav/" + os.path.basename(f))
    print(path, len(files), "meshes", os.path.getsize(path))


if __name__ == "__main__":
    if len(sys.argv) < 4 or sys.argv[1] not in ("plugin", "navs"):
        sys.exit(__doc__)
    (plugin if sys.argv[1] == "plugin" else navs)(sys.argv[2:])
