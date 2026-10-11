#!/usr/bin/env python3
"""Read the local Pylon power-mask GRP chain from the effective MPQ overlay.

StormLib is an open-source archive reader, not original-game execution. Keep
the original GRP in scratch; only hashes, format metadata and provenance go
into project evidence.
"""

import argparse
import ctypes as c
import hashlib
import json
from pathlib import Path
import struct


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--stormlib", required=True, type=Path)
    parser.add_argument("--app", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--grp-out", required=True, type=Path)
    args = parser.parse_args()
    lib = c.CDLL(str(args.stormlib))
    U, H = c.c_uint32, c.c_void_p
    lib.SFileOpenArchive.argtypes = [c.c_char_p, U, U, c.POINTER(H)]
    lib.SFileOpenArchive.restype = c.c_bool
    lib.SFileOpenFileEx.argtypes = [H, c.c_char_p, U, c.POINTER(H)]
    lib.SFileOpenFileEx.restype = c.c_bool
    lib.SFileGetFileSize.argtypes = [H, c.POINTER(U)]
    lib.SFileGetFileSize.restype = U
    lib.SFileReadFile.argtypes = [H, c.c_void_p, U, c.POINTER(U), c.c_void_p]
    lib.SFileReadFile.restype = c.c_bool
    lib.SFileCloseFile.argtypes = [H]
    lib.SFileCloseArchive.argtypes = [H]
    handles = []
    provenance = {}
    try:
        for name in ("Patch_rt.mpq", "BrooDat.mpq", "StarDat.mpq", "BroodWar.mpq", "StarCraft.mpq"):
            handle = H()
            if not lib.SFileOpenArchive(str(args.app / name).encode(), 0, 0x100, c.byref(handle)):
                raise IOError(name)
            handles.append((name, handle))

        def read(path):
            for name, archive in handles:
                handle = H()
                if not lib.SFileOpenFileEx(archive, path.encode("ascii"), 0, c.byref(handle)):
                    continue
                try:
                    high = U()
                    size = lib.SFileGetFileSize(handle, c.byref(high))
                    if high.value or size == 0xFFFFFFFF:
                        raise ValueError("Unsupported MPQ size")
                    data, got = c.create_string_buffer(size), U()
                    if not lib.SFileReadFile(handle, data, size, c.byref(got), None) or got.value != size:
                        raise IOError(path)
                    data = data.raw[:size]
                    provenance[path] = {"archive": name, "bytes": size, "sha256": hashlib.sha256(data).hexdigest()}
                    return data
                finally:
                    lib.SFileCloseFile(handle)
            raise FileNotFoundError(path)

        sprites = read("arr\\sprites.dat")
        images = read("arr\\images.dat")
        table = read("arr\\images.tbl")
        image_id = struct.unpack_from("<H", sprites, 321 * 2)[0]
        filename_id = struct.unpack_from("<I", images, image_id * 4)[0]
        count = struct.unpack_from("<H", table)[0]
        if not 1 <= filename_id <= count:
            raise ValueError("Bad images.tbl reference")
        offset = struct.unpack_from("<H", table, 2 + (filename_id - 1) * 2)[0]
        filename = table[offset:table.index(b"\0", offset)].decode("ascii")
        path = "unit\\" + filename
        grp = read(path)
        if hashlib.sha256(grp).hexdigest() != "cd98ce60f5afb8167896cc099f0e9650edf2dc03bcc8c14fda90a667bff3ae08":
            raise ValueError("Different power GRP profile; do not apply the recorded mask")
        frames, width, height = struct.unpack_from("<3H", grp)
        x, y, frame_width, frame_height, data_offset = struct.unpack_from("<4BI", grp, 6)
        result = {"scope": "Static archive/format inspection only", "sprite_id": 321,
                  "image_id": image_id, "filename_tbl_id": filename_id, "grp_path": path,
                  "provenance": provenance,
                  "grp_header": {"frames": frames, "width": width, "height": height},
                  "frame0": {"x": x, "y": y, "width": frame_width, "height": frame_height, "data_offset": data_offset}}
        args.grp_out.parent.mkdir(parents=True, exist_ok=True)
        args.grp_out.write_bytes(grp)
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(json.dumps(result, indent=2) + "\n")
        print(json.dumps({"grp_path": path, "frames": frames, "frame_dimensions": [frame_width, frame_height]}))
    finally:
        for _, archive in handles:
            lib.SFileCloseArchive(archive)


if __name__ == "__main__":
    main()
