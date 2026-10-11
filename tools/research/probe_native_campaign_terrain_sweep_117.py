#!/usr/bin/env python3
"""Run complete native terrain-cache/sweep queries on all campaign contours.

Private terrain and contour bytes are authenticated against prior original
producer/consumer reports. Real native qsort, footprint validation and movement
queries execute. Unit records and prospective deltas are explicit fixtures;
no movement dispatcher, world initialization, import or helper stub executes.
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
import probe_native_contour_fit_117 as fit
import probe_native_terrain_sweep_117 as sweep

TARGET_HASH = fit.TARGET_HASH
CONTEXT, ARRAYS, STACK, STOP = fit.CONTEXT, fit.ARRAYS, fit.STACK, fit.STOP
FLAGS, IDS, VF4, MAP = fit.FLAGS, fit.IDS, fit.VF4, fit.MAP
UNIT, SPRITE = 0x8E0000, 0x8E1000
BLOCKS = dict(fit.BLOCKS)
BLOCKS.update(sweep.BLOCKS)
MOVEMENTS = ((0, 0), (-1, 0), (0, -1), (20, 0), (0, 20), (-20, 0),
             (0, -20), (80, 80), (-80, -80), (256, 0), (0, 256), (-256, 0), (0, -256))
TYPES = (0, 7, 30, 37, 41, 65)


def inside(point, bounds):
    x, y = point
    left, top, right, bottom = bounds
    return left <= x <= right and top <= y <= bottom


def point_walkable(point, dimensions, flags, ids, records):
    x, y = point
    width, height = dimensions
    if not (0 <= x < width * 32 and 0 <= y < height * 32):
        return False
    index = y // 32 * width + x // 32
    flag = flags[index]
    if flag & 0x400000:
        return True
    if flag & 0x20000000:
        return bool(records[ids[index] & 0x7FFF][(y // 8 & 3) * 4 + (x // 8 & 3)] & 1)
    return bool(flag & 0x10000)


def source_points(uid, extents, dimensions, edges, flags, ids, records):
    """Deterministic diagnostic selection, not original AI/pathfinding policy."""
    left, up, right, down = extents
    width, height = dimensions
    candidates = []
    # Begin beside real walls so each tested shape meets nontrivial constraints.
    for side, group in enumerate(edges):
        eligible = [row for row in group if row[2] - row[1] >= 32]
        for offset in range(min(64, len(eligible))):
            row = eligible[(uid * 17 + offset * 11) % len(eligible)]
            axis, start, end = row[:3]
            middle = (start + end) // 2
            candidates.append((middle, axis + up + 1) if side == 0 else
                              (axis - right - 1, middle) if side == 1 else
                              (middle, axis - down - 1) if side == 2 else
                              (axis + left + 1, middle))
    candidates.append((width * 16, height * 16))
    candidates.extend((tx * 32 + 16, ty * 32 + 16) for ty in range(height) for tx in range(width))
    selected = []
    for x, y in candidates:
        if (x, y) in selected:
            continue
        if (left <= x and x + right < width * 32 and up <= y and y + down < height * 32 and
                point_walkable((x, y), dimensions, flags, ids, records) and fit.fits(x, y, extents, edges)):
            selected.append((x, y))
            if len(selected) == 2:
                return selected
    raise AssertionError(("No two valid diagnostic source points", uid, dimensions))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    for name in ("terrain-report", "terrain-import-report", "contours-report", "contour-fit-report",
                 "terrain-data", "campaign-data", "pyms", "static-out", "out"):
        parser.add_argument("--" + name, type=Path)
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
        witnesses[name] = [{"va": hex(row.address), "bytes": row.bytes.hex(), "mnemonic": row.mnemonic,
                            "operands": row.op_str} for row in rows]
    if args.static_out:
        args.static_out.parent.mkdir(parents=True, exist_ok=True)
        args.static_out.write_text(json.dumps({"target_sha256": TARGET_HASH, "version": "1.17.0.1",
            "evidence": "static; analytical labels", "witnesses": witnesses}, indent=2) + "\n")
    if not args.execute_original_code:
        print(json.dumps({"mode": "static only", "blocks": len(BLOCKS)}))
        return
    if any(getattr(args, name) is None for name in ("terrain_report", "terrain_import_report", "contours_report",
            "contour_fit_report", "terrain_data", "campaign_data", "pyms", "out")):
        parser.error("Execution requires all reports/directories and --out")
    dependencies = {Path(module.__file__).name: assets.file_sha(Path(module.__file__))
                    for module in (assets, generation, contours, terrain, fit, sweep)}
    provenance = json.loads(args.terrain_report.read_text())
    imported = json.loads(args.terrain_import_report.read_text())
    contour_report = json.loads(args.contours_report.read_text())
    fit_report = json.loads(args.contour_fit_report.read_text())
    for report, source in ((provenance, "inspect_terrain_assets.py"),
                           (imported, "probe_native_terrain_import_117.py"),
                           (contour_report, "probe_native_contours_117.py"),
                           (fit_report, "probe_native_contour_fit_117.py")):
        assert report.get("script_sha256", report.get("probe_script_sha256")) == dependencies[source]
    assert fit_report["all_audited_maps_included"] and fit_report["all_expected_results_matched"]
    assert contour_report["authentic_contour_maps"] == 68 and contour_report["all_expected_results_matched"]
    for report in (imported, contour_report, fit_report):
        assert report["terrain_report_sha256"] == assets.file_sha(args.terrain_report)
    assert fit_report["terrain_import_report_sha256"] == assets.file_sha(args.terrain_import_report)
    assert fit_report["contours_report_sha256"] == assets.file_sha(args.contours_report)
    assert subprocess.check_output(["git", "-C", str(args.pyms), "rev-parse", "HEAD"], text=True).strip() == fit.PYMS
    sys.path.insert(0, str(args.pyms))
    from PyMS.FileFormats.DAT.UnitsDAT import UnitsDAT
    dat_bytes = (args.campaign_data / "arr/units.dat").read_bytes()
    assert assets.sha(dat_bytes) == "da2ee6f116b77329048ec6be9e02721a527fde23d4b05b1b1336e634490abcba"
    dat = UnitsDAT()
    dat.load_data(dat_bytes)
    extents = [(row.unit_extents.left, row.unit_extents.up, row.unit_extents.right, row.unit_extents.down) for row in dat.entries]
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

    known_imports = {case["name"].removeprefix("campaign_import_"): case["observed"] for case in imported["cases"]
                     if case["name"].startswith("campaign_import_")}
    known_contours = {case["name"].removeprefix("authentic_contours_"): case for case in contour_report["cases"]
                      if case["name"].startswith("authentic_contours_")}
    known_fit = {row["name"]: row["inputs"] for row in fit_report["native_input_setups"] if "inputs" in row}
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
        flag_bytes, id_bytes = struct.pack("<" + "I" * len(flags), *flags), struct.pack("<" + "H" * len(ids), *ids)
        prior = known_imports[identity["path"]]
        assert assets.sha(id_bytes) == prior["native_megatile_ids_sha256"]
        assert assets.sha(flag_bytes) == prior["native_flags_sha256"]
        walk = generation.walk_reference(width, height, ids, records)
        assert assets.sha(bytes(v for row in walk for v in row)) == known_contours[identity["path"]]["inputs"]["native_producer_walk_sha256"]
        original_edges, unused_walk = contours.contour_reference(walk)
        uc = unicorn.Uc(unicorn.UC_ARCH_X86, unicorn.UC_MODE_32)
        for address, size in ((base, image_size), (CONTEXT, 0x100000), (STACK, 0x10000),
                              (STOP, 0x1000), (FLAGS, 0x100000), (IDS, 0x100000), (MAP, 0x100000)):
            uc.mem_map(address, size)
        uc.mem_write(base, image)
        uc.mem_write(FLAGS, flag_bytes)
        uc.mem_write(IDS, id_bytes)
        uc.mem_write(VF4, vf4)
        for address, value, size in ((0x5264B4, width, 2), (0x5264B6, height, 2),
            (0x543230, width * 32, 2), (0x543234, height * 32, 2), (0x666454, FLAGS, 4),
            (0x54323C, IDS, 4), (0x66644C, VF4, 4), (0x69B584, MAP, 4), (MAP + 0x97A1C, CONTEXT, 4)):
            put(uc, address, value, size)
        for uid, values in enumerate(extents):
            uc.mem_write(0x522C38 + uid * 8, struct.pack("<4H", *values))
        edges, setup_runs = [], []
        for side, group in enumerate(original_edges):
            assert len(group) < 15000
            address = ARRAYS + side * 0x20000
            put(uc, CONTEXT + side * 4, address)
            put(uc, CONTEXT + 0x10 + side * 2, len(group), 2)
            uc.mem_write(address, b"".join(struct.pack("<3h2B", *row) for row in group))
            setup_runs.append(call(uc, 0x4D4490, arguments=(address, len(group), 8, 0x48AD40)))
            edges.append([list(struct.unpack("<3h2B", uc.mem_read(address + i * 8, 8))) for i in range(len(group))])
        encoded = b"".join(struct.pack("<3h2B", *row) for group in edges for row in group)
        assert assets.sha(encoded) == known_contours[identity["path"]]["observed"]["native_sorted_contours_sha256"]
        assert assets.sha(encoded) == known_fit[identity["path"]]["native_contours_sha256"]
        assert assets.sha(flag_bytes) == known_fit[identity["path"]]["native_flags_sha256"]
        assert assets.sha(id_bytes) == known_fit[identity["path"]]["native_megatile_ids_sha256"]
        sources = []
        for uid in TYPES:
            left, up, right, down = extents[uid]
            inner = [up, -right, -down, left]
            for point_ordinal, position in enumerate(source_points(uid, extents[uid], (width, height), edges, flags, ids, records)):
                validation = call(uc, 0x42ED50, ecx=uid, edx=position[0], arguments=(position[1],))
                assert validation["returned"] == 1
                setup_runs.append(validation)
                sources.append({"unit_id": uid, "source_position": list(position), "original_footprint_fits": 1})
                uc.mem_write(UNIT, bytes(0x200))
                uc.mem_write(SPRITE, bytes(0x40))
                put(uc, UNIT + 0x0C, SPRITE)
                put(uc, UNIT + 0x64, uid, 2)
                uc.mem_write(SPRITE + 0x14, struct.pack("<2h", *position))
                bounds = [0, 0, 0, 0]
                for delta_ordinal, movement in enumerate(MOVEMENTS):
                    destination = (position[0] + movement[0], position[1] + movement[1])
                    old_bounds = list(bounds)
                    rebuild = not inside(destination, bounds)
                    if rebuild:
                        bounds = sweep.collision_bounds(position, inner, edges)
                    expected = int(not inside(destination, bounds) and not sweep.sweep_clear(position, destination, inner, edges))
                    run = call(uc, 0x42DA50, ecx=UNIT, arguments=movement)
                    actual_bounds = list(struct.unpack("<4h", uc.mem_read(UNIT + 0x108, 8)))
                    assert actual_bounds == bounds, ("bounds", identity["path"], uid, position, movement, old_bounds, bounds, actual_bounds, run)
                    assert run["returned"] == expected, ("sweep", identity["path"], uid, position, movement, expected, run)
                    assert run["calls"].get("build_no_collision_bounds", 0) == int(rebuild)
                    cases.append({"name": "map_{}_type_{}_source_{}_delta_{}".format(ordinal, uid, point_ordinal, delta_ordinal),
                        "passed": True, "inputs": {"map": identity["path"], "unit_id": uid, "extents": list(extents[uid]),
                            "source": list(position), "prospective_movement": list(movement), "previous_cache": old_bounds},
                        "runs": [run], "observed": {"collision": run["returned"], "safe_bounds": actual_bounds, "cache_rebuilt": rebuild},
                        "expected": {"collision": expected, "safe_bounds": bounds},
                        "oracle": "independent interval sweep and round-robin contraction on byte-authenticated native-sorted contours"})
        setups.append({"name": identity["path"], "runs": setup_runs, "inputs": {
            "chk_sha256": identity["chk_sha256"], "native_contours_sha256": assets.sha(encoded),
            "native_flags_sha256": assets.sha(flag_bytes), "native_megatile_ids_sha256": assets.sha(id_bytes),
            "directional_contour_counts": [len(group) for group in edges]}, "validated_sources": sources,
            "limits": "Recorded-native byte-identical terrain/contours; producers not rerun in this instance; unit records are fixtures"})
        print(json.dumps({"map": ordinal, "path": identity["path"], "complete_query_cases": len(cases)}), flush=True)
    result = {"target_sha256": TARGET_HASH, "version": "1.17.0.1", "probe_script_sha256": assets.file_sha(Path(__file__)),
        "dependency_hashes": dependencies, "terrain_report_sha256": assets.file_sha(args.terrain_report),
        "terrain_import_report_sha256": assets.file_sha(args.terrain_import_report), "contours_report_sha256": assets.file_sha(args.contours_report),
        "contour_fit_report_sha256": assets.file_sha(args.contour_fit_report), "units_dat_sha256": assets.sha(dat_bytes), "pyms_commit": fit.PYMS,
        "case_count": len(cases), "total_native_checks": len(cases), "all_expected_results_matched": True, "failed_cases": 0,
        "complete_function_returns": sum(len(row["runs"]) for row in cases + setups), "fragment_completions": 0,
        "authentic_maps": len(identities), "all_audited_maps_included": len(identities) == 68,
        "cache_rebuild_cases": sum(row["observed"]["cache_rebuilt"] for row in cases),
        "existing_cache_hit_cases": sum(not row["observed"]["cache_rebuilt"] for row in cases),
        "collision_cases": sum(row["observed"]["collision"] != 0 for row in cases),
        "no_helper_stubs": True, "no_import_execution": True, "full_game_or_campaign_executed": False,
        "maximum_instructions_per_execution": 10000000,
        "runtime": {"python": platform.python_version(), "architecture": platform.machine(), "unicorn": unicorn.__version__},
        "cases": cases, "native_input_setups": setups,
        "limits": "No complete world/unit creation, dynamic terrain, cache invalidation caller, local route construction, other-unit collision or movement FSM; source does not move between prospective queries"}
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({"checks": len(cases), "maps": len(identities), "complete_returns": result["complete_function_returns"]}))


if __name__ == "__main__":
    main()
