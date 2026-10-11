#!/usr/bin/env python3
"""Read original tilesets/campaign terrain into scratch and publish aggregates.

Uses host open-source StormLib for MPQ reads; never launches original code.
Retail payload is written only outside the project. Reference decoding and
sample pixel hashes are static/derived evidence, not original renderer proof.
"""

import argparse
from collections import Counter
import ctypes as c
import hashlib
import json
from pathlib import Path
import struct

TILESETS = ("badlands", "platform", "install", "ashworld", "jungle", "desert", "ice", "twilight")
EXTENSIONS = ("cv5", "vx4", "vr4", "vf4", "wpe")


def sha(data):
    return hashlib.sha256(data).hexdigest()


def file_sha(path):
    result = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            result.update(chunk)
    return result.hexdigest()


def sections(data):
    output = {}
    offset = 0
    while offset < len(data):
        if offset + 8 > len(data):
            raise ValueError("Truncated CHK header")
        name, size = struct.unpack_from("<4sI", data, offset)
        offset += 8
        if offset + size > len(data):
            raise ValueError("Truncated CHK section")
        output.setdefault(name, []).append(data[offset:offset + size])
        offset += size
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app", type=Path, required=True)
    parser.add_argument("--stormlib", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--campaign-data", type=Path, required=True)
    parser.add_argument("--scratch", type=Path, required=True)
    parser.add_argument("--pyms", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    if args.scratch.resolve() == root or root in args.scratch.resolve().parents:
        parser.error("Retail asset scratch must be outside the project")
    manifest = json.loads(args.manifest.read_text())
    args.scratch.mkdir(parents=True, exist_ok=True)
    lib = c.CDLL(str(args.stormlib))
    handle, uint = c.c_void_p, c.c_uint32
    lib.SFileOpenArchive.argtypes = [c.c_char_p, uint, uint, c.POINTER(handle)]
    lib.SFileOpenArchive.restype = c.c_bool
    lib.SFileOpenFileEx.argtypes = [handle, c.c_char_p, uint, c.POINTER(handle)]
    lib.SFileOpenFileEx.restype = c.c_bool
    lib.SFileGetFileSize.argtypes = [handle, c.POINTER(uint)]
    lib.SFileGetFileSize.restype = uint
    lib.SFileReadFile.argtypes = [handle, c.c_void_p, uint, c.POINTER(uint), c.c_void_p]
    lib.SFileReadFile.restype = c.c_bool
    lib.SFileCloseFile.argtypes = [handle]
    lib.SFileCloseArchive.argtypes = [handle]
    def read(archive, name):
        f = handle()
        if not lib.SFileOpenFileEx(archive, name.encode("ascii"), 0, c.byref(f)):
            return None
        try:
            high = uint()
            size = lib.SFileGetFileSize(f, c.byref(high))
            if high.value or size == 0xFFFFFFFF:
                raise ValueError("Unexpected MPQ file size: " + name)
            data, got = c.create_string_buffer(size), uint()
            if not lib.SFileReadFile(f, data, size, c.byref(got), None) or got.value != size:
                raise IOError("Incomplete MPQ read: " + name)
            return data.raw[:size]
        finally:
            lib.SFileCloseFile(f)
    opened, physical, effective, contents = [], [], {}, {}
    try:
        for identity in manifest["archives"]:
            path = args.app / identity["name"]
            if path.stat().st_size != identity["bytes"] or file_sha(path) != identity["sha256"]:
                raise ValueError("Archive identity mismatch: " + str(path))
            h = handle()
            if not lib.SFileOpenArchive(str(path).encode(), 0, 0x100, c.byref(h)):
                raise IOError("Cannot open original archive: " + str(path))
            opened.append((identity["name"], h))
        for tileset in TILESETS:
            for extension in EXTENSIONS:
                name = "tileset\\{}.{}".format(tileset, extension)
                for archive, h in opened:
                    payload = read(h, name)
                    if payload is None:
                        continue
                    row = {"archive": archive, "path": name, "bytes": len(payload), "sha256": sha(payload)}
                    physical.append(row)
                    if name not in effective:
                        effective[name] = row
                        contents[(tileset, extension)] = payload
                        target = args.scratch / "tileset" / (tileset + "." + extension)
                        target.parent.mkdir(parents=True, exist_ok=True)
                        target.write_bytes(payload)
                if name not in effective:
                    raise ValueError("Missing effective tileset resource: " + name)
    finally:
        for _, h in opened:
            lib.SFileCloseArchive(h)

    decoded, summaries = {}, []
    for tileset in TILESETS:
        blobs = {ext: contents[(tileset, ext)] for ext in EXTENSIONS}
        for ext, size in (("cv5", 52), ("vx4", 32), ("vr4", 64), ("vf4", 32)):
            if not blobs[ext] or len(blobs[ext]) % size:
                raise ValueError("Invalid record size: {}.{}".format(tileset, ext))
        if len(blobs["wpe"]) != 1024:
            raise ValueError("Invalid 256*4 palette size: " + tileset)
        cv5 = list(struct.iter_unpack("<26H", blobs["cv5"]))
        vx4 = list(struct.iter_unpack("<16H", blobs["vx4"]))
        vf4 = list(struct.iter_unpack("<16H", blobs["vf4"]))
        vr4 = [blobs["vr4"][i:i + 64] for i in range(0, len(blobs["vr4"]), 64)]
        palette = [blobs["wpe"][i:i + 4] for i in range(0, 1024, 4)]
        decoded[tileset] = (cv5, vx4, vf4, vr4, palette)
        bad_cv5 = [(i, j, value) for i, record in enumerate(cv5) for j, value in enumerate(record[10:]) if value >= len(vf4) or value >= len(vx4)]
        bad_vx4 = [(i, j, value) for i, record in enumerate(vx4) for j, value in enumerate(record) if value >> 1 >= len(vr4)]
        def sample(mega):
            indices = bytearray(1024)
            for cell, ref in enumerate(vx4[mega]):
                mini = vr4[ref >> 1]
                for y in range(8):
                    for x in range(8):
                        indices[((cell // 4) * 8 + y) * 32 + (cell % 4) * 8 + x] = mini[y * 8 + (7 - x if ref & 1 else x)]
            rgb = b"".join(palette[index][:3] for index in indices)
            return {"megatile": mega, "palette_indices_sha256": sha(indices), "rgb_sha256": sha(rgb),
                    "unique_palette_indices": len(set(indices)), "derived_pixels": True}
        samples = [sample(index) for index in sorted(set((0, len(vx4) // 2, len(vx4) - 1))) if all(ref >> 1 < len(vr4) for ref in vx4[index])]
        flag_hist = Counter(value for record in vf4 for value in record)
        summaries.append({"tileset": tileset, "cv5_records": len(cv5), "vx4_records": len(vx4),
            "vf4_records": len(vf4), "vr4_records": len(vr4), "palette_records": len(palette),
            "cv5_bad_megatile_references": len(bad_cv5), "cv5_bad_reference_examples": bad_cv5[:16],
            "vx4_bad_minitile_references": len(bad_vx4), "vx4_bad_reference_examples": bad_vx4[:16],
            "vx4_horizontal_flip_references": sum(bool(v & 1) for row in vx4 for v in row),
            "vf4_word_histogram": {hex(value): count for value, count in sorted(flag_hist.items())},
            "vf4_bit_counts": {hex(1 << bit): sum(count for value, count in flag_hist.items() if value >> bit & 1) for bit in range(16)},
            "palette_fourth_byte_histogram": dict(sorted(Counter(entry[3] for entry in palette).items())),
            "sample_megatiles": samples})

    campaigns = []
    for identity in manifest["campaign_found"]:
        path = args.campaign_data / identity["path"].replace("\\", "/")
        payload = path.read_bytes()
        if sha(payload) != identity["sha256"]:
            raise ValueError("CHK identity mismatch: " + str(path))
        chunks = sections(payload)
        if any(len(chunks.get(tag, [])) != 1 for tag in (b"ERA ", b"DIM ", b"MTXM")):
            raise ValueError("Missing/duplicate terrain section needs native selection contract: " + identity["path"])
        era, = struct.unpack("<H", chunks[b"ERA "][0])
        width, height = struct.unpack("<2H", chunks[b"DIM "][0])
        mtxm = chunks[b"MTXM"][0]
        if not 0 <= era < 8 or len(mtxm) != width * height * 2:
            raise ValueError("Noncanonical terrain dimensions/era: " + identity["path"])
        values = [x[0] for x in struct.iter_unpack("<H", mtxm)]
        tileset = TILESETS[era]
        cv5, vx4, vf4, _, _ = decoded[tileset]
        invalid, mega_counts, cv5_flags = [], Counter(), Counter()
        for value in values:
            group, cell = (value >> 4) & 0x7FF, value & 15
            if group >= len(cv5):
                invalid.append({"tile": value, "reason": "CV5 group outside table"})
                continue
            mega = cv5[group][10 + cell]
            if mega >= len(vf4) or mega >= len(vx4):
                invalid.append({"tile": value, "megatile": mega, "reason": "Megatile outside tables"})
                continue
            mega_counts[mega] += 1
            cv5_flags[cv5[group][1]] += 1
        campaigns.append({"path": identity["path"], "chk_sha256": identity["sha256"],
            "era": era, "tileset": tileset, "width": width, "height": height,
            "mtxm_sha256": sha(mtxm), "cells": len(values), "unique_tile_ids": len(set(values)),
            "used_cv5_groups": len(set((value >> 4) & 0x7FF for value in values)), "used_megatiles": len(mega_counts),
            "raw_high_bit_cells": sum(bool(value & 0x8000) for value in values),
            "megatile_histogram": dict(sorted(mega_counts.items())), "cv5_word1_histogram": dict(sorted(cv5_flags.items())),
            "bad_reference_cells": len(invalid), "bad_reference_examples": invalid[:16],
            "original_map_import_executed": False})

    schema_paths = ["PyMS/FileFormats/Tileset/" + name + ".py" for name in ("CV5", "VX4", "VR4", "VF4")]
    schema_paths.append("PyMS/FileFormats/Palette.py")
    report = {"evidence": "static archive/campaign decoding and derived sample pixels; no original instructions executed",
        "generated_by": "tools/research/inspect_terrain_assets.py",
        "script_sha256": file_sha(Path(__file__)), "manifest_sha256": file_sha(args.manifest),
        "stormlib_sha256": file_sha(args.stormlib), "archive_order": [x["name"] for x in manifest["archives"]],
        "schema_sources_sha256": {path: file_sha(args.pyms / path) for path in schema_paths},
        "physical_files": physical, "effective": effective, "tilesets": summaries, "campaigns": campaigns,
        "total_campaign_cells": sum(row["cells"] for row in campaigns),
        "bad_campaign_reference_cells": sum(row["bad_reference_cells"] for row in campaigns),
        "limits": "Static sizes/references and derived pixels do not prove native overlay/import, rendering, doodad mutation or generated campaign pathing."}
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({"tilesets": len(summaries), "effective_files": len(effective),
                      "physical_files": len(physical), "campaigns": len(campaigns),
                      "campaign_cells": report["total_campaign_cells"],
                      "bad_campaign_references": report["bad_campaign_reference_cells"], "output": str(args.out)}))


if __name__ == "__main__":
    main()
