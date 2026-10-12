#!/usr/bin/env python3
"""Read one MPQ file with the original storm.dll (1.17 installation) inside the full-game
emulator's machine, sector by sector, and compare it with StormLib.

Question it answers: is a file that StormLib cannot decompress also unreadable for the
original Storm (a damaged archive entry), or does Storm decode it (a StormLib difference)?
Only storm.dll runs (DllMain, then SFileOpenArchive / SFileOpenFileEx / SFileGetFileSize /
SFileSetFilePointer / SFileReadFile); StarCraft.exe is mapped but not started.

Run from tools/research with the private venv (Unicorn):
  ~/koprulu-private/venv/bin/python probe_storm_read.py \\
      --archive StarCraft.mpq --file 'campaign\\protoss\\protoss10\\staredit\\wav\\PAB05ura.wav'
"""
from __future__ import annotations

import argparse
import hashlib
import json
import logging
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from sc_emulator import process  # noqa: E402
from sc_emulator.machine import EmulationStop  # noqa: E402

ORDINALS = {"SFileOpenArchive": 266, "SFileOpenFileEx": 268, "SFileGetFileSize": 265,
            "SFileSetFilePointer": 271, "SFileReadFile": 269, "SFileCloseFile": 253}
SECTOR = 4096


def main() -> int:
    logging.basicConfig(level=logging.WARNING)
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--archive", required=True)
    ap.add_argument("--file", required=True)
    args = ap.parse_args()
    m = process.build(process.Config())
    storm = m.images["storm.dll"]
    fn = {name: storm.export(o) for name, o in ORDINALS.items()}
    handle = m.alloc(4)
    path = m.alloc_str(f"{process.vfs.INSTALL_DIR}\\{args.archive}".encode() + b"\0")
    if not m.call(fn["SFileOpenArchive"], path, 0, 0, handle):
        raise SystemExit("SFileOpenArchive failed")
    archive = m.r32(handle)
    fh = m.alloc(4)
    name = m.alloc_str(args.file.encode("latin1") + b"\0")
    if not m.call(fn["SFileOpenFileEx"], archive, name, 0, fh):
        raise SystemExit("SFileOpenFileEx failed")
    file = m.r32(fh)
    size = m.call(fn["SFileGetFileSize"], file, 0)
    buf, got = m.alloc(SECTOR), m.alloc(4)
    sectors, data = [], bytearray()
    for index in range((size + SECTOR - 1) // SECTOR):
        m.call(fn["SFileSetFilePointer"], file, index * SECTOR, 0, 0)
        m.w32(got, 0)
        want = min(SECTOR, size - index * SECTOR)
        try:
            ok = m.call(fn["SFileReadFile"], file, buf, want, got, 0)
        except EmulationStop as stop:
            # Storm reports a read error through SErrDisplayError, which terminates the process.
            sectors.append({"index": index, "ok": False, "read": 0, "fatal": str(stop)})
            break
        n = m.r32(got)
        chunk = m.read(buf, n) if n else b""
        sectors.append({"index": index, "ok": bool(ok), "read": n, "sha256": hashlib.sha256(chunk).hexdigest()})
        data += chunk if ok and n == want else bytes(want)
    from converter.mpq.stormlib import Archive, library  # noqa: F401  (StormLib comparison)
    import ctypes
    lib = library()
    stormlib = []
    with Archive(Path(process.DEFAULT_GAME_DIR) / args.archive) as a:
        h = ctypes.c_void_p()
        from converter.mpq.stormlib import _encode
        lib.SFileOpenFileEx(a._handle, _encode(args.file), 0, ctypes.byref(h))
        lib.SFileSetFilePointer.argtypes = [ctypes.c_void_p, ctypes.c_long, ctypes.c_void_p, ctypes.c_uint]
        for index in range(len(sectors)):
            lib.SFileSetFilePointer(h, index * SECTOR, None, 0)
            b, g = ctypes.create_string_buffer(SECTOR), ctypes.c_uint()
            want = min(SECTOR, size - index * SECTOR)
            ok = lib.SFileReadFile(h, b, want, ctypes.byref(g), None)
            stormlib.append({"ok": bool(ok) and g.value == want,
                             "sha256": hashlib.sha256(b.raw[: g.value]).hexdigest() if g.value else None})
    differing = [s["index"] for s, l in zip(sectors, stormlib) if "fatal" not in s
                 if (s["ok"] and s["read"] == min(SECTOR, size - s["index"] * SECTOR)) != l["ok"]
                 or (l["ok"] and s["sha256"] != l["sha256"])]
    report = {"archive": args.archive, "file": args.file, "size": size,
              "storm_fatal": next((s["fatal"] for s in sectors if "fatal" in s), None),
              "storm_sectors_read": len(sectors),
              "storm_failed_sectors": [s["index"] for s in sectors if not s["ok"] or
                                       s["read"] != min(SECTOR, size - s["index"] * SECTOR)],
              "stormlib_failed_sectors": [i for i, l in enumerate(stormlib) if not l["ok"]],
              "sectors_differing_between_storm_and_stormlib": differing,
              "storm_sha256_with_failed_sectors_zeroed": hashlib.sha256(bytes(data)).hexdigest()}
    print(json.dumps(report, indent=1))
    return 0


if __name__ == "__main__":
    sys.exit(main())
