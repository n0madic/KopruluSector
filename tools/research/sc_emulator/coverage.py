"""Execution coverage with a native (C) Unicorn block hook.

Every executed translation block marks its start address in a byte map per image
(StarCraft.exe and storm.dll). Function coverage is derived afterwards: a function is
"executed" when a block started at its entry address. Blocks that start outside every
atlas function body (entry .. entry+size) are reported as executed addresses unknown
to the atlas.
"""
from __future__ import annotations

import ctypes
import hashlib
import json
import logging
import subprocess
from pathlib import Path

from unicorn import UC_HOOK_BLOCK

log = logging.getLogger(__name__)
SRC = Path(__file__).resolve().parent / "native" / "coverage.c"
BUILD_DIR = Path("~/koprulu-private/emu/build")
ATLAS_PRIVATE = Path("~/koprulu-private/work/atlas117.json")
ATLAS_EVIDENCE = Path(__file__).resolve().parents[3] / "docs" / "research" / "evidence" / "atlas"

HOOK_CB = ctypes.CFUNCTYPE(None, ctypes.c_void_p, ctypes.c_uint64, ctypes.c_uint32, ctypes.c_void_p)


def _build_library() -> ctypes.CDLL:
    BUILD_DIR.mkdir(parents=True, exist_ok=True)
    tag = hashlib.sha256(SRC.read_bytes()).hexdigest()[:12]
    lib = BUILD_DIR / f"libsccov-{tag}.dylib"
    if not lib.exists():
        subprocess.run(["cc", "-O2", "-shared", "-fPIC", "-o", str(lib), str(SRC)], check=True)
    return ctypes.CDLL(str(lib))


HOOK_LO, HOOK_HI = 0x00400000, 0x7DFFFFFF  # all guest code except the HLE stub region


class Coverage:
    """Native block hook: coverage maps for the given images plus the block-count budget
    the machine uses for deterministic time slices."""

    def __init__(self, m, regions: list[tuple[str, int, int]]):
        from unicorn.unicorn_py3.unicorn import uclib
        self.m = m
        self.lib = _build_library()
        raw = ctypes.CDLL(uclib._name)
        self.lib.cov_add_region.argtypes = [ctypes.c_uint64, ctypes.c_uint64, ctypes.c_void_p]
        self.lib.cov_blocks_executed.restype = ctypes.c_uint64
        self.lib.cov_set_budget.argtypes = [ctypes.c_uint64]
        self.lib.cov_reset.argtypes = [ctypes.c_void_p]
        self.lib.cov_set_enabled.argtypes = [ctypes.c_int]
        self.lib.cov_reset(ctypes.cast(raw.uc_emu_stop, ctypes.c_void_p))
        self.maps: dict[str, tuple[int, int, ctypes.Array]] = {}
        for name, base, size in regions:
            buf = (ctypes.c_uint8 * size)()
            self.lib.cov_add_region(base, size, ctypes.addressof(buf))
            self.maps[name] = (base, size, buf)
        cb = ctypes.cast(self.lib.cov_block_hook, ctypes.c_void_p)
        lo, hi = HOOK_LO, HOOK_HI
        hh = ctypes.c_size_t()
        # uc_hook_add(uc, &hh, type, callback, user_data, begin, end): a private CDLL object so the
        # binding's own prototype for uc_hook_add stays untouched
        fn = raw.uc_hook_add
        fn.restype = ctypes.c_int
        fn.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_int, ctypes.c_void_p, ctypes.c_void_p,
                       ctypes.c_uint64, ctypes.c_uint64]
        err = fn(m.uc._uch, ctypes.byref(hh), UC_HOOK_BLOCK, cb, None, lo, hi)
        if err:
            raise RuntimeError(f"uc_hook_add failed: {err}")
        self.hook = hh

    def blocks_executed(self) -> int:
        return self.lib.cov_blocks_executed()

    def set_budget(self, absolute: int):
        self.lib.cov_set_budget(absolute)

    def set_enabled(self, on: bool):
        self.lib.cov_set_enabled(1 if on else 0)

    def block_starts(self, name: str) -> list[int]:
        base, size, buf = self.maps[name]
        raw = bytes(buf)
        out = []
        i = raw.find(1)
        while i >= 0:
            out.append(base + i)
            i = raw.find(1, i + 1)
        return out

    def clear(self):
        for base, size, buf in self.maps.values():
            ctypes.memset(buf, 0, size)


def load_atlas() -> dict[int, dict]:
    """Atlas functions keyed by address: private sizes merged with project statuses."""
    sizes = {int(x["addr"], 16): x for x in json.loads(ATLAS_PRIVATE.read_text())}
    out: dict[int, dict] = {}
    for f in sorted(ATLAS_EVIDENCE.glob("*.json")):
        if f.name == "packages.json":
            continue
        for e in json.loads(f.read_text()):
            a = int(e["addr"], 16)
            out[a] = {"name": e.get("name"), "status": e.get("status"), "package": f.stem,
                      "size": sizes.get(a, {}).get("size", 0), "relevance": e.get("relevance")}
    for a, x in sizes.items():
        out.setdefault(a, {"name": None, "status": None, "package": None, "size": x["size"], "relevance": None})
    return out


def summarize(block_starts: set[int], atlas: dict[int, dict], image_lo=0x401000, image_hi=0x4F0000) -> dict:
    exe_blocks = sorted(b for b in block_starts if image_lo <= b < image_hi)
    funcs = sorted(a for a in atlas if a in block_starts)
    starts = sorted(atlas)
    import bisect
    outside = []
    for b in exe_blocks:
        i = bisect.bisect_right(starts, b) - 1
        if i < 0:
            outside.append(b)
            continue
        a = starts[i]
        if b >= a + max(atlas[a]["size"], 1):
            outside.append(b)
    excluded = [a for a in funcs if atlas[a]["status"] == "excluded"]
    by_status: dict[str, int] = {}
    for a in funcs:
        by_status[str(atlas[a]["status"])] = by_status.get(str(atlas[a]["status"]), 0) + 1
    return {"executed_functions": len(funcs), "executed_blocks_exe": len(exe_blocks), "by_status": by_status,
            "executed_excluded": [{"addr": hex(a), "name": atlas[a]["name"], "package": atlas[a]["package"]}
                                  for a in excluded],
            "blocks_outside_atlas_bodies": [hex(b) for b in outside]}
