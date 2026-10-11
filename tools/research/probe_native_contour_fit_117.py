#!/usr/bin/env python3
"""Execute complete original 1.17 footprint queries on authenticated contours.

Terrain arrays and contour geometry are reconstructed, then checked byte for
byte against recorded original producers. Native qsort and fit consumers run;
the contour allocator, a whole world and movement are not claimed.
"""

import argparse
from collections import Counter
import json
from pathlib import Path
import platform
import struct
import subprocess
import sys

import capstone
import pefile
import inspect_terrain_assets as assets
import probe_native_region_generation_117 as generation
import probe_native_contours_117 as contours
import probe_native_terrain_import_117 as terrain

TARGET_HASH = generation.TARGET_HASH
PYMS = "bfc5d3aad0b5614a5aff72c223f8efa00afddfa4"
CONTEXT, ARRAYS, STACK, STOP = 0x800000, 0x810000, 0x900000, 0xA00000
FLAGS, IDS, VF4, MAP = 0xB00000, 0xC00000, 0xC40000, 0xD00000
BLOCKS = {"unit_footprint": (0x42ED50, 0x42EDF1),
          "rectangle_footprint": (0x42EC70, 0x42ED43),
          "pixel_walkability": (0x42EA10, 0x42EAAA),
          "unit_extents": (0x4891C0, 0x48926D),
          "explicit_extents": (0x489160, 0x4891B3),
          "contour_fit": (0x48B780, 0x48B9D4)}
BLOCKS.update({name: contours.BLOCKS[name] for name in
               ("native_qsort", "native_shortsort", "native_swap", "native_security_cookie", "contour_compare")})


def fits(x, y, extents, edges):
    """Independent interval-overlap geometry, without native binary searches."""
    left, up, right, down = extents
    for side, records in enumerate(edges):
        for axis, start, end, direction, info in records:
            if side == 0:
                hit = axis < y <= axis + up and start - right <= x <= end + left
            elif side == 1:
                hit = axis - right <= x < axis and start - down <= y <= end + up
            elif side == 2:
                hit = axis - down <= y < axis and start - right <= x <= end + left
            else:
                hit = axis < x <= axis + left and start - down <= y <= end + up
            if hit:
                return False
    return True


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--static-out", type=Path)
    parser.add_argument("--out", type=Path)
    parser.add_argument("--terrain-report", type=Path)
    parser.add_argument("--terrain-import-report", type=Path)
    parser.add_argument("--contours-report", type=Path)
    parser.add_argument("--terrain-data", type=Path)
    parser.add_argument("--campaign-data", type=Path)
    parser.add_argument("--pyms", type=Path)
    parser.add_argument("--map-limit", type=int)
    parser.add_argument("--execute-original-code", action="store_true")
    args = parser.parse_args()
    data = args.exe.read_bytes()
    if assets.sha(data) != TARGET_HASH:
        parser.error("Unknown executable hash")
    pe = pefile.PE(data=data)
    base = pe.OPTIONAL_HEADER.ImageBase
    assert base == 0x400000 and pe.FILE_HEADER.Machine == 0x14C
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    reviewed, witnesses = {}, {}
    for name, (lo, hi) in BLOCKS.items():
        rows = list(md.disasm(pe.get_data(lo - base, hi - lo), lo))
        assert rows[-1].address + rows[-1].size == hi
        reviewed.update({row.address: row.size for row in rows})
        witnesses[name] = [{"va": hex(row.address), "bytes": row.bytes.hex(),
                           "mnemonic": row.mnemonic, "operands": row.op_str} for row in rows]
    if args.static_out:
        args.static_out.parent.mkdir(parents=True, exist_ok=True)
        args.static_out.write_text(json.dumps({"target_sha256": TARGET_HASH, "version": "1.17.0.1",
            "evidence": "static; analytical labels", "witnesses": witnesses}, indent=2) + "\n")
    if not args.execute_original_code:
        print(json.dumps({"mode": "static only", "blocks": len(BLOCKS)}))
        return
    if any(getattr(args, name) is None for name in ("terrain_report", "terrain_import_report", "contours_report",
            "terrain_data", "campaign_data", "pyms", "out")):
        parser.error("Execution requires all input reports/directories and --out")
    dependencies = {Path(module.__file__).name: assets.file_sha(Path(module.__file__))
                    for module in (assets, generation, contours, terrain)}
    provenance = json.loads(args.terrain_report.read_text())
    imported = json.loads(args.terrain_import_report.read_text())
    contour_report = json.loads(args.contours_report.read_text())
    for report, source in ((provenance, "inspect_terrain_assets.py"),
                           (imported, "probe_native_terrain_import_117.py"),
                           (contour_report, "probe_native_contours_117.py")):
        assert report.get("script_sha256", report.get("probe_script_sha256")) == dependencies[source]
    assert contour_report["authentic_contour_maps"] == 68 and contour_report["all_expected_results_matched"]
    for report in (imported, contour_report):
        assert report["terrain_report_sha256"] == assets.file_sha(args.terrain_report)
    assert subprocess.check_output(["git", "-C", str(args.pyms), "rev-parse", "HEAD"], text=True).strip() == PYMS
    sys.path.insert(0, str(args.pyms))
    from PyMS.FileFormats.DAT.UnitsDAT import UnitsDAT
    dat_bytes = (args.campaign_data / "arr/units.dat").read_bytes()
    assert assets.sha(dat_bytes) == "da2ee6f116b77329048ec6be9e02721a527fde23d4b05b1b1336e634490abcba"
    dat = UnitsDAT()
    dat.load_data(dat_bytes)
    extents = [(entry.unit_extents.left, entry.unit_extents.up,
                entry.unit_extents.right, entry.unit_extents.down) for entry in dat.entries]
    assert len(extents) == 228
    import unicorn
    from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_ESP, UC_X86_REG_EIP
    image = pe.get_memory_mapped_image()
    image_size = (pe.OPTIONAL_HEADER.SizeOfImage + 4095) & ~4095
    entries = {lo: name for name, (lo, hi) in BLOCKS.items()}
    cases, setups = [], []

    def put(uc, address, value, width=4):
        uc.mem_write(address, (value & ((1 << (width * 8)) - 1)).to_bytes(width, "little"))

    def call(uc, entry, arguments=(), ecx=0, edx=0):
        sp = STACK + 0xF000
        uc.mem_write(sp, struct.pack("<" + "I" * (len(arguments) + 1), STOP,
                                    *(value & 0xFFFFFFFF for value in arguments)))
        uc.reg_write(UC_X86_REG_ESP, sp)
        uc.reg_write(UC_X86_REG_ECX, ecx & 0xFFFFFFFF)
        uc.reg_write(UC_X86_REG_EDX, edx & 0xFFFFFFFF)
        count, trace, cache = 0, Counter(), {}
        def check(cpu, address, size, unused):
            nonlocal count
            if address == STOP:
                cpu.emu_stop()
                return
            key = (address, size)
            if key not in cache:
                cursor, names, length = address, [], 0
                while cursor < address + size:
                    width = reviewed.get(cursor)
                    if width is None or cursor + width > address + size:
                        raise RuntimeError("Unreviewed original instruction " + hex(cursor))
                    if cursor in entries:
                        names.append(entries[cursor])
                    cursor += width
                    length += 1
                cache[key] = length, names
            length, names = cache[key]
            count += length
            trace.update(names)
        hook = uc.hook_add(unicorn.UC_HOOK_BLOCK, check)
        try:
            uc.emu_start(entry, STOP, count=10000000)
            assert uc.reg_read(UC_X86_REG_EIP) == STOP
            expected_sp = sp + 4 if entry == 0x4D4490 else sp + 4 + len(arguments) * 4
            assert uc.reg_read(UC_X86_REG_ESP) == expected_sp
        finally:
            uc.hook_del(hook)
        return {"entry": hex(entry), "scope": "complete original function", "instructions": count,
                "calls": dict(trace), "returned": uc.reg_read(UC_X86_REG_EAX)}

    def prepare(width, height, flags, ids, vf4, edges):
        uc = unicorn.Uc(unicorn.UC_ARCH_X86, unicorn.UC_MODE_32)
        for address, size in ((base, image_size), (CONTEXT, 0x100000), (STACK, 0x10000),
                              (STOP, 0x1000), (FLAGS, 0x100000), (IDS, 0x100000), (MAP, 0x100000)):
            uc.mem_map(address, size)
        uc.mem_write(base, image)
        uc.mem_write(FLAGS, struct.pack("<" + "I" * len(flags), *flags))
        uc.mem_write(IDS, struct.pack("<" + "H" * len(ids), *ids))
        uc.mem_write(VF4, vf4)
        for address, value, size in ((0x5264B4, width, 2), (0x5264B6, height, 2),
            (0x543230, width * 32, 2), (0x543234, height * 32, 2), (0x666454, FLAGS, 4),
            (0x54323C, IDS, 4), (0x66644C, VF4, 4), (0x69B584, MAP, 4), (MAP + 0x97A1C, CONTEXT, 4)):
            put(uc, address, value, size)
        for uid, values in enumerate(extents):
            uc.mem_write(0x522C38 + uid * 8, struct.pack("<4H", *values))
        runs, actual = [], []
        for direction, records in enumerate(edges):
            assert len(records) < 15000
            address = ARRAYS + direction * 0x20000
            put(uc, CONTEXT + direction * 4, address)
            put(uc, CONTEXT + 0x10 + direction * 2, len(records), 2)
            if records:
                uc.mem_write(address, b"".join(struct.pack("<3h2B", *row) for row in records))
            runs.append(call(uc, 0x4D4490, arguments=(address, len(records), 8, 0x48AD40)))
            rows = [list(struct.unpack("<3h2B", uc.mem_read(address + i * 8, 8))) for i in range(len(records))]
            assert sorted(map(tuple, rows)) == sorted(map(tuple, records))
            assert [row[:2] for row in rows] == sorted(row[:2] for row in rows)
            actual.append(rows)
        return uc, actual, runs

    def query(uc, name, uid, position, dimensions, edges, flags, ids, records):
        x, y = position
        left, up, right, down = extents[uid]
        width, height = dimensions
        inside = left <= x and x + right < width * 32 and up <= y and y + down < height * 32
        pixel = False
        if inside:
            index = (y // 32) * width + x // 32
            flag = flags[index]
            pixel = bool(flag & 0x400000) or (bool(records[ids[index] & 0x7FFF][((y // 8) & 3) * 4 + ((x // 8) & 3)] & 1)
                    if flag & 0x20000000 else bool(flag & 0x10000))
        expected = int(inside and pixel and fits(x, y, extents[uid], edges))
        run = call(uc, 0x42ED50, ecx=uid, edx=x, arguments=(y,))
        assert run["returned"] == expected, (name, uid, position, extents[uid], expected, run)
        cases.append({"name": name, "passed": True, "inputs": {"unit_id": uid, "position": list(position),
            "extents": list(extents[uid]), "map_dimensions": list(dimensions)}, "runs": [run],
            "observed": {"fits": run["returned"]}, "expected": {"fits": expected},
            "oracle": "map bounds, original-flag/VF4 point gate and independent contour interval overlaps"})

    # Stock rows and directional equality/endpoints use deliberately synthetic walls.
    edges = [[[200, 160, 240, direction, 0]] for direction in range(4)]
    records = [[1] * 16]
    flags, ids = [0x1FFFF] * 1024, [0] * 1024
    uc, edges, runs = prepare(32, 32, flags, ids, struct.pack("<16H", *records[0]), edges)
    setups.append({"name": "synthetic_stock_wall_setup", "runs": runs, "limits": "Synthetic walls, real stock extents"})
    for uid, (left, up, right, down) in enumerate(extents):
        for ordinal, pos in enumerate(((200, 200), (200, 200 + up), (200 - right, 200),
                                       (200, 200 - down), (200 + left, 200), (500, 500), (0, 0))):
            query(uc, "stock_extent_{}_point_{}".format(uid, ordinal), uid, pos, (32, 32), edges, flags, ids, records)
        run = call(uc, 0x4891C0, ecx=CONTEXT, arguments=(uid,))
        observed = list(struct.unpack("<4h", uc.mem_read(CONTEXT + 0x20, 8)))
        assert observed == [up, -right, -down, left]
        outer = list(struct.unpack("<4h", uc.mem_read(CONTEXT + 0x30, 8)))
        assert outer == [up + 1, -right - 1, -down - 1, left + 1]
        cases.append({"name": "stock_extent_initializer_{}".format(uid), "passed": True, "runs": [run],
                      "inputs": {"unit_id": uid}, "observed": {"inner": observed, "outer": outer}})
    for radius in (0, 1, 7, 8, 31, 127):
        run = call(uc, 0x4891C0, ecx=CONTEXT, arguments=(0x8000 | radius,))
        inner = list(struct.unpack("<4h", uc.mem_read(CONTEXT + 0x20, 8)))
        outer = list(struct.unpack("<4h", uc.mem_read(CONTEXT + 0x30, 8)))
        assert inner == [radius, -radius, -radius, radius]
        assert outer == [radius + 1, -radius - 1, -radius - 1, radius + 1]
        cases.append({"name": "radius_extent_initializer_{}".format(radius), "passed": True, "runs": [run],
                      "inputs": {"encoded_type": 0x8000 | radius}, "observed": {"inner": inner, "outer": outer}})
    for width, height in ((1, 1), (16, 16), (17, 23), (32, 32), (64, 40)):
        box = ((width - 1) // 2, (height - 1) // 2, width - width // 2 - 1, height - height // 2 - 1)
        for ordinal, (x, y) in enumerate(((200, 200), (200, 200 + box[1]), (200 - box[2], 200),
                                        (200, 200 - box[3]), (200 + box[0], 200), (500, 500), (0, 0))):
            inside = x - width // 2 >= 0 and x - width // 2 + width < 1024 and y - height // 2 >= 0 and y - height // 2 + height < 1024
            expected = int(inside and fits(x, y, box, edges))
            run = call(uc, 0x42EC70, ecx=x, edx=y, arguments=(width, height))
            assert run["returned"] == expected, ("rectangle", width, height, x, y, expected, run)
            cases.append({"name": "rectangle_{}_{}_{}".format(width, height, ordinal), "passed": True, "runs": [run],
                "inputs": {"position": [x, y], "width": width, "height": height}, "observed": {"fits": run["returned"]}})

    known_imports = {case["name"].removeprefix("campaign_import_"): case for case in imported["cases"]
                     if case["name"].startswith("campaign_import_")}
    known_contours = {case["name"].removeprefix("authentic_contours_"): case for case in contour_report["cases"]
                      if case["name"].startswith("authentic_contours_")}
    decoded = {}
    for tileset in assets.TILESETS:
        payloads = {}
        for extension in ("cv5", "vf4"):
            payload = (args.terrain_data / "tileset" / (tileset + "." + extension)).read_bytes()
            assert assets.sha(payload) == provenance["effective"]["tileset\\{}.{}".format(tileset, extension)]["sha256"]
            payloads[extension] = payload
        decoded[tileset] = (list(struct.iter_unpack("<26H", payloads["cv5"])),
                            list(struct.iter_unpack("<16H", payloads["vf4"])), payloads["vf4"])
    identities = provenance["campaigns"][:args.map_limit] if args.map_limit else provenance["campaigns"]
    for ordinal, identity in enumerate(identities):
        chk = (args.campaign_data / identity["path"].replace("\\", "/")).read_bytes()
        assert assets.sha(chk) == identity["chk_sha256"]
        cv5, records, vf4 = decoded[identity["tileset"]]
        width, height = identity["width"], identity["height"]
        raw = [row[0] for row in struct.iter_unpack("<H", assets.sections(chk)[b"MTXM"][0])]
        ids = [cv5[value >> 4 & 0x7FF][10 + (value & 15)] | (value & 0x8000) for value in raw]
        flags = [0xFFFF | (cv5[value >> 4 & 0x7FF][1] & 0xD8FA) << 16 |
                 generation.megatile_flags_reference(records[mega & 0x7FFF]) | (0x400000 if value & 0x8000 else 0)
                 for value, mega in zip(raw, ids)]
        prior = known_imports[identity["path"]]["observed"]
        assert assets.sha(struct.pack("<" + "H" * len(ids), *ids)) == prior["native_megatile_ids_sha256"]
        assert assets.sha(struct.pack("<" + "I" * len(flags), *flags)) == prior["native_flags_sha256"]
        walk = generation.walk_reference(width, height, ids, records)
        assert assets.sha(bytes(v for row in walk for v in row)) == known_contours[identity["path"]]["inputs"]["native_producer_walk_sha256"]
        original_edges, unused_walk = contours.contour_reference(walk)
        uc, edges, runs = prepare(width, height, flags, ids, vf4, original_edges)
        encoded = b"".join(struct.pack("<3h2B", *row) for group in edges for row in group)
        assert assets.sha(encoded) == known_contours[identity["path"]]["observed"]["native_sorted_contours_sha256"]
        setups.append({"name": identity["path"], "runs": runs, "inputs": {"chk_sha256": identity["chk_sha256"],
            "native_contours_sha256": assets.sha(encoded), "native_flags_sha256": prior["native_flags_sha256"],
            "native_megatile_ids_sha256": prior["native_megatile_ids_sha256"]}, "limits": "Recorded-native byte-identical terrain/contours; producer not rerun in this instance"})
        for uid in (0, 7, 30, 37, 41, 65, 106, 160):
            dims = extents[uid]
            points = [(width * 16, height * 16)]
            for side, group in enumerate(edges):
                edge = next(row for row in group if row[2] - row[1] >= 16 and
                            32 <= row[0] <= (height * 32 - 32 if side in (0, 2) else width * 32 - 32))
                axis, start, end = edge[:3]
                middle = (start + end) // 2
                boundary = axis + dims[1] if side == 0 else axis - dims[2] if side == 1 else axis - dims[3] if side == 2 else axis + dims[0]
                for delta in (-1, 0, 1):
                    points.append((middle, boundary + delta) if side in (0, 2) else (boundary + delta, middle))
            for index, point in enumerate(points):
                query(uc, "campaign_{}_type_{}_point_{}".format(ordinal, uid, index), uid, point,
                      (width, height), edges, flags, ids, records)
        print(json.dumps({"map": ordinal, "path": identity["path"], "complete_query_cases": len(cases)}), flush=True)
    result = {"target_sha256": TARGET_HASH, "version": "1.17.0.1", "probe_script_sha256": assets.file_sha(Path(__file__)),
        "dependency_hashes": dependencies, "terrain_report_sha256": assets.file_sha(args.terrain_report),
        "terrain_import_report_sha256": assets.file_sha(args.terrain_import_report), "contours_report_sha256": assets.file_sha(args.contours_report),
        "units_dat_sha256": assets.sha(dat_bytes), "pyms_commit": PYMS,
        "case_count": len(cases), "total_native_checks": len(cases), "all_expected_results_matched": True,
        "failed_cases": 0, "complete_function_returns": sum(len(row["runs"]) for row in cases + setups),
        "fragment_completions": 0, "authentic_maps": len(identities), "all_audited_maps_included": len(identities) == 68,
        "no_helper_stubs": True, "no_import_execution": True, "full_game_or_campaign_executed": False,
        "maximum_instructions_per_execution": 10000000,
        "runtime": {"python": platform.python_version(), "architecture": platform.machine(), "unicorn": unicorn.__version__},
        "cases": cases, "native_input_setups": setups,
        "limits": "No full world/contour allocation, dynamic unit/doodad changes, local route construction or movement; stock extents are caller data"}
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({"checks": len(cases), "maps": len(identities), "complete_returns": result["complete_function_returns"]}))


if __name__ == "__main__":
    main()
