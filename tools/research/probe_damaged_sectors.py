#!/usr/bin/env python3
"""Tell a damaged archive entry from a damaged release: one MPQ file's stored sectors against another copy of the same
file (Remastered's), sector by sector (docs/research/campaigns.md, "Damaged archive entry"; issue #2).

For each sector it reports whether StormLib reads it and whether it equals the other copy's, then compresses the other
copy's sector again with StormLib under the stored compression mask and encrypts it with the file's key: a sector that
comes out equal to the stored bytes was that copy's, and where only a tail comes out equal, the bytes before it are
broken (the encryption's state at a byte depends on every byte before it in the sector). The broken bytes' archive
offsets and their 2,048-byte disc blocks are printed.

Run with the private venv (StormLib, CascLib's Python reader):
  koprulu-private/venv/bin/python tools/research/probe_damaged_sectors.py \\
      --archive koprulu-private/retail/extracted/app/StarCraft.mpq \\
      --file 'campaign\\protoss\\protoss10\\staredit\\wav\\PAB05ura.wav' --scr /Applications/StarCraft
"""
from __future__ import annotations

import argparse
import ctypes
import hashlib
import json
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from converter.casc.storage import CascStorage  # noqa: E402
from converter.mpq import stormlib  # noqa: E402

SECTOR = 4096
DISC_BLOCK = 2048
FILE_ENCRYPTED = 0x10000
# SFileInfoClass (StormLib.h): the archive's header offset; the file's offset, compressed size, flags and key
MPQ_HEADER_OFFSET, FILE_BYTE_OFFSET, FILE_COMPRESSED_SIZE, FILE_FLAGS, FILE_KEY = 5, 49, 52, 53, 54


def crypt_table() -> list[int]:
    table, seed = [0] * 0x500, 0x00100001
    for i in range(0x100):
        for j in range(i, i + 0x500, 0x100):
            seed = (seed * 125 + 3) % 0x2AAAAB
            high = (seed & 0xFFFF) << 16
            seed = (seed * 125 + 3) % 0x2AAAAB
            table[j] = high | (seed & 0xFFFF)
    return table


TABLE = crypt_table()


def crypt(data: bytes, key: int, decrypt: bool) -> bytes:
    """MPQ's block cipher over the whole dwords; a trailing 1-3 bytes stay as they are."""
    out, seed = bytearray(data), 0xEEEEEEEE
    for i in range(len(data) // 4):
        seed = (seed + TABLE[0x400 + (key & 0xFF)]) & 0xFFFFFFFF
        value = struct.unpack_from("<I", data, 4 * i)[0]
        result = value ^ ((key + seed) & 0xFFFFFFFF)
        plain = result if decrypt else value
        key = ((((~key) << 0x15) + 0x11111111) | (key >> 0x0B)) & 0xFFFFFFFF
        seed = (plain + seed + (seed << 5) + 3) & 0xFFFFFFFF
        struct.pack_into("<I", out, 4 * i, result)
    return bytes(out)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--archive", required=True)
    ap.add_argument("--file", required=True)
    ap.add_argument("--scr", required=True, help="a StarCraft: Remastered install holding the other copy under SD/")
    args = ap.parse_args()
    lib = stormlib.library()
    lib.SFileGetFileInfo.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_void_p, ctypes.c_uint, ctypes.c_void_p]
    lib.SFileGetFileInfo.restype = ctypes.c_bool
    lib.SFileSetFilePointer.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_void_p, ctypes.c_uint]
    lib.SFileSetFilePointer.restype = ctypes.c_uint
    lib.SCompCompress.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_int), ctypes.c_void_p, ctypes.c_int,
                                  ctypes.c_uint, ctypes.c_int, ctypes.c_int]
    lib.SCompCompress.restype = ctypes.c_int

    def info(handle, cls: int, kind):
        value = kind()
        if not lib.SFileGetFileInfo(handle, cls, ctypes.byref(value), ctypes.sizeof(value), None):
            raise SystemExit(f"SFileGetFileInfo {cls} failed")
        return value.value

    with CascStorage(args.scr) as storage:
        other = storage.read("SD/" + args.file.lower().replace("\\", "/"))
    sectors = []
    with stormlib.Archive(args.archive) as archive:
        handle = ctypes.c_void_p()
        if not lib.SFileOpenFileEx(archive._handle, stormlib._encode(args.file), 0, ctypes.byref(handle)):
            raise SystemExit(f"{args.file} not in {args.archive}")
        base = info(archive._handle, MPQ_HEADER_OFFSET, ctypes.c_ulonglong) + info(handle, FILE_BYTE_OFFSET,
                                                                                  ctypes.c_ulonglong)
        flags, key = info(handle, FILE_FLAGS, ctypes.c_uint), info(handle, FILE_KEY, ctypes.c_uint)
        size = lib.SFileGetFileSize(handle, None)
        for index in range((size + SECTOR - 1) // SECTOR):
            want = min(SECTOR, size - index * SECTOR)
            lib.SFileSetFilePointer(handle, index * SECTOR, None, 0)
            buf, got = ctypes.create_string_buffer(want), ctypes.c_uint()
            ok = lib.SFileReadFile(handle, buf, want, ctypes.byref(got), None) and got.value == want
            sectors.append({"index": index, "reads": bool(ok),
                            "equal": bool(ok) and buf.raw == other[index * SECTOR:index * SECTOR + want]})
        lib.SFileCloseFile(handle)
    raw = Path(args.archive).read_bytes()
    count = len(sectors)
    table = raw[base:base + 4 * (count + 1)]
    if flags & FILE_ENCRYPTED:
        table = crypt(table, (key - 1) & 0xFFFFFFFF, decrypt=True)
    offsets = struct.unpack(f"<{count + 1}I", table)

    def stored(i: int) -> bytes:
        return raw[base + offsets[i]:base + offsets[i + 1]]

    def plain(i: int) -> bytes:
        return crypt(stored(i), (key + i) & 0xFFFFFFFF, decrypt=True) if flags & FILE_ENCRYPTED else stored(i)

    # the compression mask most sectors that read have (a broken sector's own first byte may be broken)
    masks = [plain(s["index"])[0] for s in sectors if s["reads"] and len(stored(s["index"])) < SECTOR]
    usual = max(sorted(set(masks)), key=masks.count)
    out, n = ctypes.create_string_buffer(2 * SECTOR), ctypes.c_int()
    for s in sectors:
        i, data = s["index"], stored(s["index"])
        if len(data) >= SECTOR:
            s["again"] = "stored uncompressed"
            continue
        source = other[i * SECTOR:(i + 1) * SECTOR]
        best = None
        for mask in dict.fromkeys((plain(i)[0], usual)):
            n.value = 2 * SECTOR
            if not lib.SCompCompress(out, ctypes.byref(n), source, len(source), mask, 0, 0):
                continue
            made = out.raw[:n.value]
            made = crypt(made, (key + i) & 0xFFFFFFFF, decrypt=False) if flags & FILE_ENCRYPTED else made
            if len(made) != len(data):
                best = best or (mask, None)
                continue
            diff = [k for k in range(len(data)) if made[k] != data[k]]
            if best is None or best[1] is None or len(diff) < len(best[1]):
                best = (mask, diff)
        if best is None or best[1] is None:
            s["again"] = "another length"
        elif not best[1]:
            s["again"] = "equal"
        else:
            s["again"] = {"mask": best[0], "first_differing": best[1][0], "last_differing": best[1][-1],
                          "length": len(data)}
    broken = []
    for s in sectors:
        if s["reads"]:
            continue
        i, again = s["index"], s["again"]
        first = base + offsets[i]
        last = base + offsets[i + 1] - 1
        if isinstance(again, dict):  # equal again past the last differing byte: only the bytes before are broken
            first, last = first + again["first_differing"], first + again["last_differing"]
        broken.append({"sector": i, "from": hex(first), "to": hex(last),
                       "disc_blocks": sorted({first // DISC_BLOCK, last // DISC_BLOCK})})
    report = {"archive": args.archive, "archive_sha256": hashlib.sha256(raw).hexdigest(), "file": args.file,
              "size": size, "other_sha256": hashlib.sha256(other).hexdigest(), "other_size": len(other),
              "sectors": count, "equal_sectors": sum(s["equal"] for s in sectors),
              "unreadable_sectors": [s["index"] for s in sectors if not s["reads"]],
              "readable_but_different": [s["index"] for s in sectors if s["reads"] and not s["equal"]],
              "usual_mask": usual,
              "compressed_again_equal": [s["index"] for s in sectors if s["again"] == "equal"],
              "compressed_again_otherwise": {s["index"]: s["again"] for s in sectors if s["again"] != "equal"},
              "broken_bytes": broken}
    print(json.dumps(report, indent=1))
    return 0


if __name__ == "__main__":
    sys.exit(main())
