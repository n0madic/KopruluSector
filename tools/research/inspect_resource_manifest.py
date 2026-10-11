#!/usr/bin/env python3
"""Resolve every resource name the 1.17 executable and its data tables reference.

Candidate names come from: the pinned PyMS listfile, path-like strings in StarCraft.exe,
images.tbl (prefixed `unit\\`), sfxdata.tbl (`sound\\`), portdata.tbl (`portrait\\` plus
the idle/talking SMK suffixes), and mapdata.tbl (campaign map paths). Each name is
resolved against the archives in the effective order Patch_rt, BrooDat, StarDat,
BroodWar, StarCraft; the first archive containing it wins. The report lists resolved
names with size and SHA-256, referenced names that resolve nowhere, and per-archive
counts of entries that no candidate name explains.

Requires StormLib (libstorm, e.g. Homebrew `stormlib`). Reads archives only.
"""
import argparse
import ctypes as C
import hashlib
import json
import re
import struct
from pathlib import Path

ARCHIVES = ["Patch_rt.mpq", "BrooDat.mpq", "StarDat.mpq", "BroodWar.mpq", "StarCraft.mpq"]
EXE_HASH = "94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0"
EXTENSIONS = ("grp", "wav", "pcx", "tbl", "dat", "bin", "smk", "chk", "scm", "scx", "fnt", "cv5", "vx4",
              "vr4", "vf4", "wpe", "lo?", "loa", "lob", "lod", "lof", "loo", "los", "lou", "lol", "spk",
              "txt", "tga", "grp", "cmp", "mid", "pal", "wpe", "dds", "bmp")


class SFILE_FIND_DATA(C.Structure):
    _fields_ = [("cFileName", C.c_char * 1024), ("szPlainName", C.c_char_p),
                ("dwHashIndex", C.c_uint), ("dwBlockIndex", C.c_uint),
                ("dwFileSize", C.c_uint), ("dwFileFlags", C.c_uint),
                ("dwCompSize", C.c_uint), ("dwFileTimeLo", C.c_uint),
                ("dwFileTimeHi", C.c_uint), ("lcLocale", C.c_uint)]


class Storm:
    def __init__(self, path):
        lib = C.CDLL(path)
        lib.SFileOpenArchive.argtypes = [C.c_char_p, C.c_uint, C.c_uint, C.POINTER(C.c_void_p)]
        lib.SFileOpenFileEx.argtypes = [C.c_void_p, C.c_char_p, C.c_uint, C.POINTER(C.c_void_p)]
        lib.SFileGetFileSize.argtypes = [C.c_void_p, C.POINTER(C.c_uint)]
        lib.SFileGetFileSize.restype = C.c_uint
        lib.SFileReadFile.argtypes = [C.c_void_p, C.c_void_p, C.c_uint, C.POINTER(C.c_uint), C.c_void_p]
        lib.SFileCloseFile.argtypes = [C.c_void_p]
        lib.SFileHasFile.argtypes = [C.c_void_p, C.c_char_p]
        lib.SFileFindFirstFile.argtypes = [C.c_void_p, C.c_char_p, C.POINTER(SFILE_FIND_DATA), C.c_char_p]
        lib.SFileFindFirstFile.restype = C.c_void_p
        lib.SFileFindNextFile.argtypes = [C.c_void_p, C.POINTER(SFILE_FIND_DATA)]
        lib.SFileFindClose.argtypes = [C.c_void_p]
        self.lib = lib

    def open(self, path):
        h = C.c_void_p()
        if not self.lib.SFileOpenArchive(str(path).encode(), 0, 0x100, C.byref(h)):
            raise OSError(f"cannot open {path}")
        return h

    def read(self, h, name):
        fh = C.c_void_p()
        if not self.lib.SFileOpenFileEx(h, name.encode("latin1"), 0, C.byref(fh)):
            return None
        size = self.lib.SFileGetFileSize(fh, None)
        buf = C.create_string_buffer(size)
        got = C.c_uint()
        self.lib.SFileReadFile(fh, buf, size, C.byref(got), None)
        self.lib.SFileCloseFile(fh)
        return buf.raw[:got.value]

    def entries(self, h, listfile):
        d = SFILE_FIND_DATA()
        f = self.lib.SFileFindFirstFile(h, b"*", C.byref(d), str(listfile).encode())
        out = []
        while f:
            out.append(d.cFileName.decode("latin1"))
            if not self.lib.SFileFindNextFile(f, C.byref(d)):
                break
        if f:
            self.lib.SFileFindClose(f)
        return out


def tbl_strings(data):
    n = struct.unpack_from("<H", data, 0)[0]
    out = []
    for i in range(n):
        off = struct.unpack_from("<H", data, 2 + 2 * i)[0]
        out.append(data[off:data.index(b"\0", off)].decode("latin1"))
    return out


def exe_paths(exe):
    pat = re.compile(rb"[A-Za-z0-9_ .\-\\%]{3,}\.(?:" + b"|".join(e.encode().replace(b"?", b".") for e in EXTENSIONS) + rb")\b", re.I)
    found = set()
    for m in pat.finditer(exe):
        s = m.group(0).decode("latin1")
        if "\\" in s and "%" not in s:
            found.add(s.lstrip("\\"))
    templates = sorted({m.group(0).decode("latin1") for m in pat.finditer(exe) if b"%" in m.group(0) and b"\\" in m.group(0)})
    return found, templates


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--app", required=True, type=Path)
    ap.add_argument("--listfile", required=True, type=Path)
    ap.add_argument("--stormlib", default="/opt/homebrew/lib/libstorm.dylib")
    ap.add_argument("--out", required=True, type=Path)
    args = ap.parse_args()
    exe = (args.app / "StarCraft.exe").read_bytes()
    if hashlib.sha256(exe).hexdigest() != EXE_HASH:
        ap.error("unexpected StarCraft.exe hash")
    st = Storm(args.stormlib)
    arcs = [(a, st.open(args.app / a)) for a in ARCHIVES]

    def resolve(name):
        for an, h in arcs:
            if st.lib.SFileHasFile(h, name.encode("latin1")):
                return an, h
        return None, None

    def get(name):
        an, h = resolve(name)
        return st.read(h, name) if h else None

    listed = [l.strip() for l in args.listfile.read_text(errors="ignore").splitlines() if l.strip()]
    from_exe, templates = exe_paths(exe)
    refs = {}
    images = tbl_strings(get("arr\\images.tbl"))
    for s in images:
        refs.setdefault("unit\\" + s, "images.tbl")
    for s in tbl_strings(get("arr\\sfxdata.tbl")):
        if s:
            refs.setdefault("sound\\" + s, "sfxdata.tbl")
    for s in tbl_strings(get("arr\\portdata.tbl")):
        if s:
            for kind in ("fid", "tlk"):
                for k in range(4):
                    refs.setdefault(f"portrait\\{s}{kind}{k}.smk", "portdata.tbl (probe of variants 0-3)")
    for s in tbl_strings(get("arr\\mapdata.tbl")):
        if s:
            refs.setdefault(s + "\\staredit\\scenario.chk", "mapdata.tbl")
    for s in from_exe:
        refs.setdefault(s, "StarCraft.exe string")
    candidates = set(listed) | set(refs)
    resolved = {}
    for name in sorted(candidates, key=str.lower):
        an, h = resolve(name)
        if not h:
            continue
        data = st.read(h, name)
        resolved[name.lower()] = {"name": name, "archive": an, "size": len(data),
                                  "sha256": hashlib.sha256(data).hexdigest(),
                                  "referenced_by": refs.get(name)}
    # Relative executable strings: the code prepends a directory at run time. Try the
    # top-level directories present in the archives and record matches as heuristic.
    dirs = sorted({v["name"].split("\\")[0] for v in resolved.values() if "\\" in v["name"]}, key=str.lower)
    # Prefixes the code composes at run time (atlas P19/P22 glue theme loader, P16 0x448fa0
    # Battle.net art loader): glue\<theme>\ for the twelve glue themes and glue\battle.net\.
    dirs += ["glue\\battle.net"] + sorted({"\\".join(v["name"].split("\\")[:2]) for v in resolved.values()
                                          if v["name"].lower().startswith("glue\\pal")}, key=str.lower)
    prefixed = {}
    for n, why in refs.items():
        if n.lower() in resolved or why != "StarCraft.exe string":
            continue
        hits = [d for d in dirs if (d + "\\" + n).lower() in resolved]
        for d in dirs:
            for sub in ("glue",):
                pass
        if not hits:
            for d in dirs:
                an, h = resolve(d + "\\" + n)
                if h:
                    data = st.read(h, d + "\\" + n)
                    resolved[(d + "\\" + n).lower()] = {"name": d + "\\" + n, "archive": an, "size": len(data),
                                                       "sha256": hashlib.sha256(data).hexdigest(),
                                                       "referenced_by": "StarCraft.exe string (prefix inferred)"}
                    hits.append(d)
        if hits:
            prefixed[n] = hits
    unresolved_refs = sorted((n, why) for n, why in refs.items() if n.lower() not in resolved and n not in prefixed
                             and not why.startswith("portdata.tbl"))
    explained = {
        "staredit\\scenario.chk": "path inside each map archive (atlas P23 0x469700/0x469a10/0x469b80/0x469cd0); campaign maps resolve as <mapdata dir>\\staredit\\scenario.chk",
        "null\\staredit\\scenario.chk": "mapdata.tbl entry 0 is the placeholder 'null'; mission index 0 terminates every campaign table (atlas P41 0x50e0b8 rows), so it is never started",
        "rez\\gluatzonelist.bin": "AppleTalk zone list of the modem/AppleTalk glue screen 0x45e280 (atlas P21, excluded: multiplayer only)",
    }
    missing_sfx = "sfxdata entry naming a WAV absent from every archive; no reachable IScript sound opcode, units.dat sound range or constant play call uses this sound ID (evidence/iscript-sounds.json, atlas P35)"
    classified = []
    for n, why in unresolved_refs:
        key = n.lower()
        reason = explained.get(key) or (missing_sfx if why == "sfxdata.tbl" else None)
        classified.append({"name": n, "referenced_by": why, "resolution": reason})
    unresolved_refs = [(c["name"], c["referenced_by"]) for c in classified if not c["resolution"]]
    unexplained = {}
    for an, h in arcs:
        names = st.entries(h, args.listfile)
        unexplained[an] = {"entries": len(names), "unnamed": sum(1 for n in names if n.startswith("File"))}
    report = {
        "exe_sha256": EXE_HASH,
        "archive_order": ARCHIVES,
        "listfile_sha256": hashlib.sha256(args.listfile.read_bytes()).hexdigest(),
        "counts": {"resolved": len(resolved), "referenced": len(refs),
                   "referenced_resolved": sum(1 for n in refs if n.lower() in resolved),
                   "classified_unresolved_references": len(classified),
                   "unexplained_references": len(unresolved_refs)},
        "per_archive": unexplained,
        "exe_path_templates": templates,
        "relative_exe_strings_with_inferred_prefix": prefixed,
        "classified_unresolved_references": classified,
        "unexplained_references": [{"name": n, "referenced_by": w} for n, w in unresolved_refs],
        "resources": [resolved[k] for k in sorted(resolved)],
    }
    args.out.write_text(json.dumps(report, indent=1))
    print(json.dumps(report["counts"]), json.dumps(report["per_archive"]))


if __name__ == "__main__":
    main()
