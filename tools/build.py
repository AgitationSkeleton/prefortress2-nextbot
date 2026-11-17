#!/usr/bin/env python3
"""Incremental, parallel build of the pf2nextbot Metamod:Source plugin.

  Windows: MSVC x86 (Win32)       -> build/pf2nextbot.dll
  Linux:   Clang/GCC -m32 (i386)  -> build/pf2nextbot_i486.so

AMBuild is not used; this applies the same flags the Metamod hl2sdk manifest uses for sdk2013.

  python -I tools/build.py [clean] [-j N] [--only substr]

Dependencies (see README "Building"):
  HL2SDK    alliedmodders/hl2sdk, branch sdk2013
  MMSOURCE  alliedmodders/metamod-source (1.12 branch; plugin API 16)
Each is taken from the environment variable of that name, else from tools/local.json
({"hl2sdk": "...", "mmsource": "..."}, git-ignored), else from deps/hl2sdk and deps/metamod-source.

Linux: CXX selects the compiler (default clang++). Clang is required for the MSVC-style
__declspec(property) accessors in src/shim, so the compiler is invoked with -fdeclspec.
LINUX_SYSROOT, if set, is passed as --sysroot (for a locally assembled 32-bit sysroot).
"""
import concurrent.futures as cf, glob, json, os, shlex, shutil, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
IS_WIN = sys.platform == "win32"


def dep_path(env_name, key, default):
    if os.environ.get(env_name):
        return os.environ[env_name]
    local = os.path.join(ROOT, "tools", "local.json")
    if os.path.exists(local):
        try:
            v = json.load(open(local)).get(key)
            if v:
                return v
        except ValueError:
            pass
    return os.path.join(ROOT, "deps", default)


SDK = dep_path("HL2SDK", "hl2sdk", "hl2sdk")                  # alliedmodders hl2sdk, branch sdk2013
MMS = dep_path("MMSOURCE", "mmsource", "metamod-source")
OBJ = os.path.join(ROOT, "build", "obj" if IS_WIN else "obj-linux")
OUT = os.path.join(ROOT, "build", "pf2nextbot.dll" if IS_WIN else "pf2nextbot_i486.so")

# Source roots. Order of INCLUDES matters: shim first so "cbase.h" etc. resolve to our game layer.
SRC_DIRS = ["src", "third_party/valve"]
INCLUDES = [
    "src/mm_override", "src/shim", "src", "src/game",
    "third_party/valve/NextBot", "third_party/valve/NextBot/Path", "third_party/valve/NextBot/Player",
    "third_party/valve/NextBot/Behavior", "third_party/valve/nav", "third_party/valve/tf",
    "third_party/valve/tf/nav_mesh", "third_party/valve/shared", "third_party/valve",
]
SDK_INCLUDES = ["public", "public/engine", "public/mathlib", "public/vstdlib", "public/tier0", "public/tier1",
                "public/game/server", "public/toolframework", "common"]
COMMON_DEFINES = ("SOURCE_ENGINE=9 SE_SDK2013=9 SE_EPISODEONE=1 SE_ORANGEBOX=3 SE_CSS=6 SE_HL2DM=7 SE_DODS=8 SE_TF2=12 "
                  "SE_LEFT4DEAD=13 SE_NUCLEARDAWN=14 SE_LEFT4DEAD2=16 SE_ALIENSWARM=17 SE_PORTAL2=18 SE_CSGO=23 "
                  "GAME_DLL RAD_TELEMETRY_DISABLED NDEBUG PF2NB NEXT_BOT TF_DLL").split()

# ---- Windows (MSVC) --------------------------------------------------------------------------------
WIN_DEFINES = COMMON_DEFINES + ("COMPILER_MSVC COMPILER_MSVC32 WIN32 _WINDOWS "
                                "_CRT_SECURE_NO_DEPRECATE _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_DEPRECATE").split()
CFLAGS = ["/nologo", "/c", "/MT", "/EHsc", "/TP", "/std:c++17", "/W3", "/O2", "/Oy-", "/Zi", "/GR",
          "/wd4005", "/wd4996", "/wd4244", "/wd4305", "/wd4267", "/wd4018", "/wd4800", "/wd4838", "/wd4091",
          "/Zc:threadSafeInit-", "/MP1"]
LIBS = [os.path.join(SDK, "lib", "public", "%s.lib" % l) for l in ("mathlib", "tier1", "tier0", "vstdlib")] + \
       ["legacy_stdio_definitions.lib", "kernel32.lib", "user32.lib", "advapi32.lib", "ole32.lib"]

# ---- Linux (Clang/GCC, i386) -----------------------------------------------------------------------
# Defines follow hl2sdk-manifests SdkHelpers (COMPILER_GCC _LINUX LINUX POSIX GNUC) plus the sdk2013
# manifest's linux defines (NO_HOOK_MALLOC NO_MALLOC_OVERRIDE). stricmp & co. come from basetypes.h.
LINUX_DEFINES = COMMON_DEFINES + "COMPILER_GCC _LINUX LINUX POSIX GNUC NO_HOOK_MALLOC NO_MALLOC_OVERRIDE".split()
LINUX_CFLAGS = ["-c", "-m32", "-march=pentium4", "-msse2", "-mfpmath=sse", "-fPIC", "-std=gnu++17", "-O2", "-g",
                "-fno-omit-frame-pointer", "-fno-strict-aliasing", "-fvisibility=hidden",
                "-fvisibility-inlines-hidden", "-fno-threadsafe-statics", "-fdeclspec",
                "-w"]   # Valve's 2013-era code is noisy under a modern compiler; the Windows build keeps /W3
# Export only CreateInterface (tier1's, via PLUGIN_EXPOSE): every game-class name in src/shim also exists
# in PF2's server.so, so nothing else may be visible or interposable.
LINUX_LDFLAGS = ["-m32", "-shared", "-static-libgcc", "-static-libstdc++", "-Wl,--no-undefined",
                 "-Wl,-Bsymbolic", "-Wl,-z,noexecstack"]
LINUX_VERSION_SCRIPT = "{ global: CreateInterface; local: *; };\n"


def vc_env():
    cache = os.path.join(ROOT, "build", "vcenv.json")
    if os.path.exists(cache):
        return json.load(open(cache))
    # Already inside an x86 developer prompt (e.g. CI with msvc-dev-cmd)?
    if os.environ.get("VSCMD_ARG_TGT_ARCH") == "x86" and shutil.which("cl"):
        return dict(os.environ)
    vcvars = os.environ.get("VCVARSALL")
    if not vcvars:
        vswhere = os.path.join(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"),
                               "Microsoft Visual Studio", "Installer", "vswhere.exe")
        if os.path.exists(vswhere):
            inst = subprocess.run([vswhere, "-latest", "-products", "*", "-requires",
                                   "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath"],
                                  capture_output=True, text=True).stdout.strip().splitlines()
            if inst:
                vcvars = os.path.join(inst[0], "VC", "Auxiliary", "Build", "vcvarsall.bat")
    if not vcvars or not os.path.exists(vcvars):
        sys.exit("vcvarsall.bat not found: install VS 2022 Build Tools (C++ x86) or set VCVARSALL")
    out = subprocess.run('cmd /c ""%s" x86 >nul && set"' % vcvars, capture_output=True, text=True, shell=True).stdout
    env = dict(l.split("=", 1) for l in out.splitlines() if "=" in l)
    os.makedirs(os.path.dirname(cache), exist_ok=True)
    json.dump(env, open(cache, "w"))
    return env


def sources():
    for d in SRC_DIRS:
        for base, _, files in os.walk(os.path.join(ROOT, d)):
            for f in files:
                if f.endswith(".cpp"):
                    yield os.path.join(base, f)


def objname(src):
    rel = os.path.relpath(src, ROOT).replace("\\", "/")
    return os.path.join(OBJ, rel.replace("/", "__")[:-4] + (".obj" if IS_WIN else ".o"))


def headers_mtime():
    m = 0
    for d in SRC_DIRS:
        for base, _, files in os.walk(os.path.join(ROOT, d)):
            for f in files:
                if f.endswith((".h", ".inl", ".inc")):
                    m = max(m, os.path.getmtime(os.path.join(base, f)))
    return m


def include_args(flag):
    inc = []
    for i in INCLUDES:
        inc += [flag, os.path.join(ROOT, i)]
    for i in SDK_INCLUDES:
        inc += [flag, os.path.join(SDK, i)]
    inc += [flag, os.path.join(MMS, "core"), flag, os.path.join(MMS, "core", "sourcehook")]
    return inc


def linux_cxx():
    cxx = shlex.split(os.environ.get("CXX", "clang++"))
    if os.environ.get("LINUX_SYSROOT"):
        cxx += ["--sysroot=" + os.environ["LINUX_SYSROOT"]]
    return cxx


def compile_one(env, src):
    obj = objname(src)
    if IS_WIN:
        cmd = [shutil.which("cl", path=env["PATH"])] + CFLAGS + ["/D" + d for d in WIN_DEFINES] + include_args("/I") + \
              [src, "/Fo" + obj, "/Fd" + obj[:-4] + ".pdb"]
    else:
        cmd = linux_cxx() + LINUX_CFLAGS + ["-D" + d for d in LINUX_DEFINES] + include_args("-I") + [src, "-o", obj]
    r = subprocess.run(cmd, env=env, capture_output=True, text=True, errors="replace")
    return src, r.returncode, r.stdout + r.stderr


def error_lines(rc, out):
    if IS_WIN:
        return [l for l in out.splitlines() if (" error " in l or "fatal error" in l or (rc and "warning" not in l))
                and not l.strip().endswith(".cpp")]
    return [l for l in out.splitlines() if "error" in l or (rc and "note:" not in l and "warning" not in l)]


def link(env, objs):
    if IS_WIN:
        cmd = [shutil.which("link", path=env["PATH"]), "/nologo", "/DLL", "/DEBUG", "/OPT:REF", "/SUBSYSTEM:WINDOWS",
               "/MACHINE:X86", "/OUT:" + OUT, "/PDB:" + OUT[:-4] + ".pdb", "/NODEFAULTLIB:libcmtd"] + objs + LIBS
    else:
        lib = os.path.join(SDK, "lib", "public", "linux32")
        if not os.path.isdir(lib):
            lib = os.path.join(SDK, "lib", "public", "linux")
        # Like AMBuild: link the engine's tier0/vstdlib by soname from a staging dir.
        stage = os.path.join(ROOT, "build", "linux-libs")
        os.makedirs(stage, exist_ok=True)
        for so in ("libtier0_srv.so", "libvstdlib_srv.so"):
            shutil.copy2(os.path.join(lib, so), os.path.join(stage, so))
        vs = os.path.join(ROOT, "build", "pf2nextbot.version")
        with open(vs, "w") as f:
            f.write(LINUX_VERSION_SCRIPT)
        cmd = linux_cxx() + LINUX_LDFLAGS + ["-Wl,--version-script=" + vs, "-o", OUT] + objs + \
              [os.path.join(lib, "mathlib.a"), os.path.join(lib, "tier1.a"),
               "-L" + stage, "-l:libtier0_srv.so", "-l:libvstdlib_srv.so", "-lm", "-ldl", "-lpthread"]
    r = subprocess.run(cmd, env=env, capture_output=True, text=True, errors="replace")
    if r.returncode:
        out = r.stdout + r.stderr
        lines = [l for l in out.splitlines() if "error" in l or "undefined" in l]
        print("LINK FAILED (%d errors)" % len(lines))
        for l in (lines or out.splitlines())[:60]:
            print("   ", l)
        sys.exit(1)
    print("linked", OUT, os.path.getsize(OUT))


def main():
    args = sys.argv[1:]
    if "clean" in args:
        for f in os.listdir(OBJ) if os.path.isdir(OBJ) else []:
            os.remove(os.path.join(OBJ, f))
    for p, what in ((SDK, "HL2SDK"), (MMS, "MMSOURCE")):
        if not os.path.isdir(os.path.join(p, "public" if what == "HL2SDK" else "core")):
            sys.exit("%s not found at %s (set %s)" % (what, p, what))
    jobs = int(args[args.index("-j") + 1]) if "-j" in args else os.cpu_count()
    only = args[args.index("--only") + 1] if "--only" in args else None
    os.makedirs(OBJ, exist_ok=True)
    env = vc_env() if IS_WIN else dict(os.environ)
    if "PATH" not in env:   # cmd's `set` may spell it Path
        env["PATH"] = next((v for k, v in env.items() if k.upper() == "PATH"), "")
    hm = headers_mtime()
    todo = []
    srcs = list(sources())
    for s in srcs:
        if only and only not in s.replace("\\", "/"):
            continue
        o = objname(s)
        if not os.path.exists(o) or os.path.getmtime(o) < max(os.path.getmtime(s), hm):
            todo.append(s)
    failed = 0
    with cf.ThreadPoolExecutor(jobs) as ex:
        for src, rc, out in ex.map(lambda s: compile_one(env, s), todo):
            if rc:
                failed += 1
                print("FAIL", os.path.relpath(src, ROOT))
                for l in error_lines(rc, out)[:25]:
                    print("   ", l.replace(ROOT + os.sep, ""))
    print("compiled %d, failed %d" % (len(todo), failed))
    if failed or only:
        sys.exit(1 if failed else 0)
    link(env, [objname(s) for s in srcs])


if __name__ == "__main__":
    main()
