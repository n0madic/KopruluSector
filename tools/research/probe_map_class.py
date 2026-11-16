#!/usr/bin/env python3
"""The original's class of every map in a 1.17 install's maps folder, from its own code in the full-game emulator.

A map's class (the map list entry's +0x260, LoadMapEntryInfo 0x45d870) is OpenMapArchiveWithCondition 0x469560's:
the type of the first condition file the archive holds (ProbeConditionFile 0x469650 over the table 0x4f1020:
Ladder_*.cnd, Tourn_1.cnd, ...), kept only when VerifyMapSignature 0x46b0d0 finds the map's strong signature valid
(VerifyNgisSignature 0x49d460: SHA-1 over the file and its upper-cased name, RSA-2048 with the executable's public
key). Only storm.dll's DllMain runs before; StarCraft.exe is mapped, its function called on the main thread for each
map (as probe_storm_read.py calls Storm's), after the two big numbers its dynamic initializers would have made.

Writes tests/golden/oracle/map_classes.json (gitignored, as every oracle golden): {"maps": {path: class}}, the path
relative to the install with '/' separators. test_res.map_classes_are_the_originals compares Koprulu's
(kp_map_class.c) with it.

Run from the repository with the private venv (Unicorn):
  koprulu-private/venv/bin/python tools/research/probe_map_class.py [--data DIR] [--out PATH]
"""
from __future__ import annotations

import argparse
import json
import logging
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from sc_emulator import process, vfs  # noqa: E402
from unicorn import x86_const as X  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
OPEN_MAP_ARCHIVE_WITH_CONDITION = 0x469560  # __fastcall(path, int* condition, keep open, HANDLE* archive), ret 8
SBIG_NEW = 0x4d02e6  # the executable's thunk of Storm's SBigNew(handle*)
SIGNATURE_KEY = (0x6a61bc, 0x6a61c0)  # the big numbers N and E (DynInit_RsaModulus 0x402480, 0x402460)
MAP_SUFFIXES = (".scm", ".scx")
log = logging.getLogger("probe_map_class")


def map_class(m, path: str) -> int | None:
    """The original's class of the map at its guest path, None when the archive does not open."""
    cond, handle = m.alloc(4), m.alloc(4)
    m.w32(cond, 0xFFFFFFFF)
    guest = m.alloc_str(path.encode("latin-1") + b"\0")
    ok = m.call(OPEN_MAP_ARCHIVE_WITH_CONDITION, 0, handle, regs={X.UC_X86_REG_ECX: guest, X.UC_X86_REG_EDX: cond})
    return m.r32(cond) if ok else None


def main() -> int:
    logging.basicConfig(level=logging.INFO, format="%(message)s")
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--data", type=Path, default=process.DEFAULT_GAME_DIR, help="a 1.17 install with its maps folder")
    ap.add_argument("--out", type=Path, default=ROOT / "tests/golden/oracle/map_classes.json")
    a = ap.parse_args()
    m = process.build(process.Config(game_dir=a.data))
    for handle in SIGNATURE_KEY:
        m.call(SBIG_NEW, handle)
    maps = sorted(p for p in (a.data / "maps").rglob("*") if p.suffix.lower() in MAP_SUFFIXES and p.is_file())
    classes: dict[str, int] = {}
    t0 = time.monotonic()
    for p in maps:
        rel = p.relative_to(a.data).as_posix()
        c = map_class(m, vfs.INSTALL_DIR + "\\" + rel.replace("/", "\\"))
        if c is None:
            log.warning("%s: the archive does not open", rel)
            continue
        classes[rel] = c
        if c:
            log.info("%s: class %d", rel, c)
    log.info("%d maps, %d of a class, %.1f s", len(classes), sum(1 for c in classes.values() if c), time.monotonic() - t0)
    a.out.parent.mkdir(parents=True, exist_ok=True)
    a.out.write_text(json.dumps({"install": "bw-1.17.0.1", "maps": classes}, indent=1, sort_keys=True) + "\n")
    log.info("wrote %s", a.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
