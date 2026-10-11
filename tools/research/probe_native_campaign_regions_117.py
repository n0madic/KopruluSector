#!/usr/bin/env python3
"""Build original 1.17 region graphs from hash-verified campaign terrain.

Explicit private execution only; imported allocations are not executed or
stubbed. Reviewed fragments prepare authentic flags/walk/classes; complete
native graph/rank/path helpers then return. Original payload stays in scratch.
"""

import argparse
from collections import Counter
import json
from pathlib import Path
import platform
import random
import struct

import capstone
import pefile
import inspect_terrain_assets as assets
import probe_native_region_generation_117 as generation
import probe_native_terrain_import_117 as terrain

TARGET_HASH = generation.TARGET_HASH
MAP, RECT, PATH_CONTEXT = 0x800000, 0x8F0000, 0x8F9000
STACK, STOP, WALK = 0x900000, 0xA00000, 0xB00000
FLAGS, MEGA_IDS, VF4 = terrain.FLAGS, terrain.MEGA_IDS, terrain.VF4
MEGA_FLAGS, CV5, MTXM = terrain.MEGA_FLAGS, terrain.CV5, terrain.MTXM
CONVERTED = 0xC90000
LIMIT = 100000000


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--terrain-report", type=Path)
    parser.add_argument("--terrain-data", type=Path)
    parser.add_argument("--campaign-data", type=Path)
    parser.add_argument("--scratch", type=Path)
    parser.add_argument("--map-limit", type=int)
    parser.add_argument("--static-out", type=Path)
    parser.add_argument("--out", type=Path)
    parser.add_argument("--execute-original-code", action="store_true")
    args = parser.parse_args()
    data = args.exe.read_bytes()
    if assets.sha(data) != TARGET_HASH:
        parser.error("Unknown executable hash; reviewed addresses do not apply")
    pe = pefile.PE(data=data)
    base = pe.OPTIONAL_HEADER.ImageBase
    assert base == 0x400000 and pe.FILE_HEADER.Machine == 0x14C
    blocks = dict(generation.BLOCKS)
    blocks.update(terrain.BLOCKS)
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    witnesses = {}
    reviewed = {}
    for name, (lo, hi) in blocks.items():
        rows = list(md.disasm(pe.get_data(lo - base, hi - lo), lo))
        if not rows or rows[-1].address + rows[-1].size != hi:
            parser.error("Invalid instruction boundary: " + name)
        witnesses[name] = [{"va": hex(i.address), "bytes": i.bytes.hex(),
                           "mnemonic": i.mnemonic, "operands": i.op_str} for i in rows]
        reviewed.update({i.address: i.size for i in rows})
    if args.static_out:
        args.static_out.parent.mkdir(parents=True, exist_ok=True)
        args.static_out.write_text(json.dumps({"target_sha256": TARGET_HASH,
            "version": "1.17.0.1", "evidence": "static; analytical labels",
            "witnesses": witnesses, "jump_tables_excluded_from_execution": generation.TABLES}, indent=2) + "\n")
    if not args.execute_original_code:
        print(json.dumps({"mode": "static only", "blocks": len(blocks)}))
        return
    if any(value is None for value in (args.terrain_report, args.terrain_data, args.campaign_data, args.scratch, args.out)):
        parser.error("Execution requires terrain inputs, --scratch and --out")
    root = Path(__file__).resolve().parents[2]
    if args.scratch.resolve() == root or root in args.scratch.resolve().parents:
        parser.error("Original-derived region snapshots must remain outside the project")
    args.scratch.mkdir(parents=True, exist_ok=True)
    dependencies = {Path(module.__file__).name: assets.file_sha(Path(module.__file__))
                    for module in (assets, generation, terrain)}
    provenance = json.loads(args.terrain_report.read_text())
    if provenance["script_sha256"] != dependencies["inspect_terrain_assets.py"]:
        parser.error("Stale terrain asset report")
    decoded = {}
    for tileset in assets.TILESETS:
        payloads = {}
        for ext in ("cv5", "vf4"):
            payload = (args.terrain_data / "tileset" / (tileset + "." + ext)).read_bytes()
            if assets.sha(payload) != provenance["effective"]["tileset\\{}.{}".format(tileset, ext)]["sha256"]:
                parser.error("Unknown terrain input: " + tileset + "." + ext)
            payloads[ext] = payload
        decoded[tileset] = (payloads, list(struct.iter_unpack("<16H", payloads["vf4"])))
    import unicorn
    from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDX,
        UC_X86_REG_ESP, UC_X86_REG_EBP, UC_X86_REG_EIP, UC_X86_REG_ESI, UC_X86_REG_EBX)
    image = pe.get_memory_mapped_image()
    image_size = (pe.OPTIONAL_HEADER.SizeOfImage + 4095) & ~4095
    entries = {lo: name for name, (lo, hi) in blocks.items()}
    cases = []

    def put(uc, address, value, width=4):
        uc.mem_write(address, (value & ((1 << (width * 8)) - 1)).to_bytes(width, "little"))

    def get(uc, address, width=4):
        return int.from_bytes(uc.mem_read(address, width), "little")

    def region(index):
        return MAP + 0x449FC + index * 64

    def machine(width, height, payloads):
        uc = unicorn.Uc(unicorn.UC_ARCH_X86, unicorn.UC_MODE_32)
        for address, size in ((base, image_size), (MAP, 0x100000), (STACK, 0x10000),
                              (STOP, 0x1000), (WALK, 0x100000), (FLAGS, 0x200000)):
            uc.mem_map(address, size)
        uc.mem_write(base, image)
        uc.mem_write(CV5, payloads["cv5"])
        uc.mem_write(VF4, payloads["vf4"])
        for address, value, size in ((0x5264B4, width, 2), (0x5264B6, height, 2),
            (0x666458, MTXM, 4), (0x666454, FLAGS, 4), (0x54323C, MEGA_IDS, 4),
            (0x66644C, VF4, 4), (0x666448, len(payloads["vf4"]) // 32, 2),
            (0x6CE650, CV5, 4), (0x666968, len(payloads["cv5"]) // 52, 4),
            (0x666450, MEGA_FLAGS, 4), (0x69B584, MAP, 4), (0x69B55C, WALK, 4),
            (0x6D319C, 0, 4), (0x50F310, 0, 4)):
            put(uc, address, value, size)
        put(uc, RECT, 0)
        put(uc, RECT + 4, width | height << 16)
        return uc

    def execute(uc, address, end, expected_sp, scope):
        count, trace, cache = 0, Counter(), {}
        def check(cpu, va, size, unused):
            nonlocal count
            if va == end:
                cpu.emu_stop()
                return
            key = (va, size)
            if key not in cache:
                cursor, names, length = va, [], 0
                while cursor < va + size:
                    width = reviewed.get(cursor)
                    if width is None or cursor + width > va + size:
                        raise RuntimeError("Unreviewed original basic block at " + hex(cursor))
                    if cursor in entries:
                        names.append(entries[cursor])
                    cursor += width
                    length += 1
                cache[key] = (length, names)
            length, names = cache[key]
            count += length
            trace.update(names)
        hook = uc.hook_add(unicorn.UC_HOOK_BLOCK, check)
        try:
            uc.emu_start(address, end + 1, count=LIMIT)
            if uc.reg_read(UC_X86_REG_EIP) != end or uc.reg_read(UC_X86_REG_ESP) != expected_sp:
                raise RuntimeError("Native return/fragment contract failed: " + hex(address))
        finally:
            uc.hook_del(hook)
        return {"scope": scope, "entry": hex(address), "stop": hex(end),
                "returned": uc.reg_read(UC_X86_REG_EAX), "instructions": count,
                "calls": dict(trace), "allowlist": "every instruction boundary in each executed basic block"}

    def call(uc, address, ecx=MAP, edx=0):
        sp = STACK + 0xF000
        put(uc, sp, STOP)
        uc.reg_write(UC_X86_REG_ESP, sp)
        uc.reg_write(UC_X86_REG_ECX, ecx)
        uc.reg_write(UC_X86_REG_EDX, edx)
        return execute(uc, address, STOP, sp + 4, "complete original function")

    def frame(uc, local, saved):
        bp = STACK + 0xE000
        uc.mem_write(bp - 0x500, bytes(0x510))
        put(uc, bp + 4, STOP)
        uc.reg_write(UC_X86_REG_EBP, bp)
        uc.reg_write(UC_X86_REG_ESP, bp - local - saved * 4)
        return bp

    identities = provenance["campaigns"][:args.map_limit] if args.map_limit else provenance["campaigns"]
    rng = random.Random(1171717)
    for ordinal, identity in enumerate(identities):
        payloads, records = decoded[identity["tileset"]]
        chk = (args.campaign_data / identity["path"].replace("\\", "/")).read_bytes()
        if assets.sha(chk) != identity["chk_sha256"]:
            parser.error("Unknown CHK input: " + identity["path"])
        chunks = assets.sections(chk)
        width, height = identity["width"], identity["height"]
        uc = machine(width, height, payloads)
        uc.mem_write(MTXM, chunks[b"MTXM"][0])
        bp = frame(uc, 0x14, 0)
        uc.reg_write(UC_X86_REG_EAX, MEGA_FLAGS)
        runs = [execute(uc, 0x445562, STOP, bp + 8, "post-allocation megatile flag fragment")]
        runs.append(call(uc, 0x445720))
        assert runs[-1]["returned"] == 1
        ids = list(struct.unpack("<" + "H" * (width * height), uc.mem_read(MEGA_IDS, width * height * 2)))
        sp = STACK + 0xF000
        uc.mem_write(sp, struct.pack("<4I", 0, 0, 0, STOP))
        uc.reg_write(UC_X86_REG_ESP, sp)
        uc.reg_write(UC_X86_REG_EAX, CONVERTED)
        runs.append(execute(uc, 0x48AD9F, STOP, sp + 16, "post-allocation VF4 conversion fragment"))
        assert bytes(uc.mem_read(CONVERTED, len(records) * 16)) == bytes(
            0 if value & 1 else 128 for row in records for value in row)
        bp = frame(uc, 0x10, 3)
        put(uc, bp - 0x10, WALK)
        put(uc, bp - 0xC, 1024 - width * 4)
        uc.reg_write(UC_X86_REG_EBX, WALK)
        uc.reg_write(UC_X86_REG_EAX, CONVERTED)
        runs.append(execute(uc, 0x48B20C, 0x48B2A4, bp - 0x1C, "post-allocation/pre-free walk copy fragment"))
        runs.append(execute(uc, 0x48B2B8, STOP, bp + 8, "post-free walk direction fragment"))
        walk = [list(uc.mem_read(WALK + y * 1024, width * 4)) for y in range(height * 4)]
        assert walk == generation.walk_reference(width, height, ids, records)
        bp = frame(uc, 0xC, 2)
        uc.reg_write(UC_X86_REG_ESI, FLAGS)
        runs.append(execute(uc, 0x490E9F, STOP, bp + 8, "post-walk class fragment"))
        classes = [list(struct.unpack("<" + "H" * width, uc.mem_read(MAP + 0xC + y * 512, width * 2)))
                   for y in range(height)]
        runs.append(call(uc, 0x490F50))
        runs.append(call(uc, 0x48F060, edx=RECT))
        assert runs[-1]["returned"] == 1
        runs.append(call(uc, 0x48F0E0))
        n = get(uc, MAP, 2)
        assert 1 <= n <= 5000
        rs = []
        for index in range(n):
            a = region(index)
            count = get(uc, a + 7, 1)
            neighbors = list(struct.unpack("<" + "H" * count, uc.mem_read(get(uc, a + 0xC), count * 2))) if count else []
            rs.append({"id": index, "flags": get(uc, a, 2), "group": get(uc, a + 2, 2),
                "tiles": get(uc, a + 4, 2), "ground_neighbors": get(uc, a + 6, 1), "neighbors": neighbors,
                "center_fixed8": list(struct.unpack("<2i", uc.mem_read(a + 0x10, 8))),
                "bounds_pixels": list(struct.unpack("<4h", uc.mem_read(a + 0x18, 8))),
                "cut_rank": get(uc, a + 0x20, 1)})
            assert len(neighbors) == len(set(neighbors)) and all(value < n for value in neighbors)
            assert rs[-1]["ground_neighbors"] <= len(neighbors)
        grid = [list(struct.unpack("<" + "H" * width, uc.mem_read(MAP + 0xC + y * 512, width * 2))) for y in range(height)]
        split_count = (get(uc, MAP + 8) - (MAP + 0x2000C)) // 6
        assert 0 <= split_count <= 25000
        splits = [list(struct.unpack("<3H", uc.mem_read(MAP + 0x2000C + i * 6, 6))) for i in range(split_count)]
        assert rs[0]["group"] == 0x4000
        bottom_split_cells = []
        for x, value in enumerate(grid[-1]):
            if value != 0:
                bottom_split_cells.append({"x": x, "value": value,
                    "split": splits[value - 0x2000] if 0x2000 <= value < 0x2000 + split_count else None})
        if bottom_split_cells:
            print(json.dumps({"bottom_row_replaced_cells": bottom_split_cells[:12],
                              "total": len(bottom_split_cells)}), flush=True)
        first_tiles = {}
        for y, row in enumerate(grid):
            for x, index in enumerate(row):
                assert index < n or 0x2000 <= index < 0x2000 + split_count
                if index < n:
                    assert index == 0 or rs[index]["flags"] == classes[y][x]
                    first_tiles.setdefault(index, (x * 32 + 16, y * 32 + 16))
        for r in rs:
            assert all(index != 0 and rs[index]["flags"] != 0x1FFD for index in r["neighbors"][:r["ground_neighbors"]])
        snapshot = {"width": width, "height": height, "regions": rs, "raster": grid, "splits": splits}
        encoded = json.dumps(snapshot, separators=(",", ":")).encode()
        (args.scratch / (str(ordinal).zfill(2) + ".json")).write_bytes(encoded)
        observed = {"region_count": n, "split_count": split_count,
            "bottom_split_cell_count": len(bottom_split_cells),
            "group_count": len({r["group"] for r in rs}), "ground_region_count": sum(r["flags"] != 0x1FFD for r in rs),
            "region_class_histogram": {hex(k): v for k, v in sorted(Counter(r["flags"] for r in rs).items())},
            "maximum_neighbors": max(len(r["neighbors"]) for r in rs),
            "walk_sha256": assets.sha(bytes(value for row in walk for value in row)),
            "snapshot_sha256": assets.sha(encoded)}
        cases.append({"name": "campaign_graph_" + identity["path"], "runs": runs,
            "inputs": {"chk_sha256": identity["chk_sha256"], "tileset": identity["tileset"],
                       "width": width, "height": height, "initial_low_flag_word": 0},
            "observed": observed, "passed": True,
            "oracle": "independent walk bits plus structural assertions and original graph observation; no independent full graph oracle"})
        eligible = [r for r in rs if r["id"] in first_tiles and r["flags"] != 0x1FFD and r["id"] != 0]
        pairs = []
        for attempt in range(100):
            if len(eligible) < 2 or len(pairs) >= 4:
                break
            a, b = rng.sample(eligible, 2)
            if a["group"] == b["group"] and (a["id"], b["id"]) not in pairs:
                pairs.append((a["id"], b["id"]))
        for source, target in pairs:
            uc.mem_write(PATH_CONTEXT, bytes(0x174))
            for offset, point in ((4, first_tiles[source]), (8, first_tiles[target])):
                put(uc, PATH_CONTEXT + offset, point[0], 2)
                put(uc, PATH_CONTEXT + offset + 2, point[1], 2)
            put(uc, PATH_CONTEXT + 0x160, source, 2)
            put(uc, PATH_CONTEXT + 0x162, target, 2)
            run = call(uc, 0x491300, ecx=PATH_CONTEXT)
            count = get(uc, PATH_CONTEXT + 0x86, 2)
            assert count <= 50
            path = [get(uc, PATH_CONTEXT + 0x20 + i * 2, 2) for i in range(count)]
            partial = get(uc, PATH_CONTEXT + 0x1C, 1)
            assert path and path[0] == source
            assert all(b in rs[a]["neighbors"][:rs[a]["ground_neighbors"]] for a, b in zip(path, path[1:]))
            assert all(get(uc, region(r["id"]) + 8) == 0 for r in rs)
            cases.append({"name": "campaign_path_{}_{}_{}".format(ordinal, source, target), "runs": [run],
                "inputs": {"source": source, "target": target}, "passed": True,
                "observed": {"path": path, "partial": partial, "returned": run["returned"], "reached_target": path[-1] == target},
                "oracle": "legal graph edges, cleared temporary backrefs, recorded original return/path; bounded partial paths allowed"})
        print(json.dumps({"graphs_complete": ordinal + 1, "of": len(identities), "map": identity["path"],
                          "regions": n, "splits": split_count}), flush=True)
    complete = sum(run["scope"] == "complete original function" for case in cases for run in case["runs"])
    fragments = sum(run["scope"] != "complete original function" for case in cases for run in case["runs"])
    result = {"target_sha256": TARGET_HASH, "version": "1.17.0.1", "probe_script_sha256": assets.file_sha(Path(__file__)),
        "dependency_hashes": dependencies, "terrain_report_sha256": assets.file_sha(args.terrain_report),
        "case_count": len(cases), "total_native_checks": len(cases), "all_expected_results_matched": True,
        "failed_cases": 0, "complete_function_returns": complete, "fragment_completions": fragments,
        "authentic_graph_count": len(identities), "all_audited_maps_included": len(identities) == len(provenance["campaigns"]),
        "runtime": {"python": platform.python_version(), "architecture": platform.machine(), "unicorn": unicorn.__version__},
        "maximum_instructions_per_execution": LIMIT, "no_helper_stubs": True,
        "no_windows_import_execution": True, "full_game_or_campaign_executed": False,
        "evidence": "authentic map regions and paths; explicit allocation-dependent flags/walk/class fragments",
        "cases": cases, "limits": "Contours, dynamic doodads, loader defaults, large failure branches and local movement remain separate."}
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({"checks": len(cases), "complete": complete, "fragments": fragments, "out": str(args.out)}))


if __name__ == "__main__":
    main()
