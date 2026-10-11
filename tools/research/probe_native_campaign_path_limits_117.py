#!/usr/bin/env python3
"""Diagnose original path limits on recorded authentic native graph snapshots.

Static by default. Explicit private execution reconstructs the documented
read fields from hash-verified original-generated snapshots, reruns complete
native paths, and compares the exact earlier outputs. No imports or stubs.
"""

import argparse
from collections import Counter
import json
from pathlib import Path
import platform
import struct

import capstone
import pefile
import inspect_terrain_assets as assets
import probe_native_region_generation_117 as generation
import probe_native_campaign_regions_117 as campaign

TARGET_HASH = generation.TARGET_HASH
MAP, ARRAY, CONTEXT, STACK, STOP = 0x800000, 0x8B0000, 0x8F9000, 0x900000, 0xA00000
NAMES = ("region_long_path", "heap_change_priority", "heap_sift_down",
         "approximate_distance", "region_group", "native_stack_probe")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--regions-report", type=Path)
    parser.add_argument("--snapshots", type=Path)
    parser.add_argument("--static-out", type=Path)
    parser.add_argument("--out", type=Path)
    parser.add_argument("--execute-original-code", action="store_true")
    args = parser.parse_args()
    data = args.exe.read_bytes()
    if assets.sha(data) != TARGET_HASH:
        parser.error("Unknown executable hash")
    pe = pefile.PE(data=data)
    base = pe.OPTIONAL_HEADER.ImageBase
    assert base == 0x400000 and pe.FILE_HEADER.Machine == 0x14C
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    witnesses, allowed = {}, set()
    for name in NAMES:
        lo, hi = generation.BLOCKS[name]
        rows = list(md.disasm(pe.get_data(lo - base, hi - lo), lo))
        assert rows[-1].address + rows[-1].size == hi
        witnesses[name] = [{"va": hex(i.address), "bytes": i.bytes.hex(), "mnemonic": i.mnemonic,
                            "operands": i.op_str} for i in rows]
        allowed.update((i.address, i.size) for i in rows)
    if args.static_out:
        args.static_out.parent.mkdir(parents=True, exist_ok=True)
        args.static_out.write_text(json.dumps({"target_sha256": TARGET_HASH, "version": "1.17.0.1",
            "evidence": "static; analytical labels", "witnesses": witnesses}, indent=2) + "\n")
    if not args.execute_original_code:
        print(json.dumps({"mode": "static only", "blocks": len(NAMES)}))
        return
    if any(value is None for value in (args.regions_report, args.snapshots, args.out)):
        parser.error("Execution requires --regions-report, --snapshots and --out")
    dependencies = {Path(module.__file__).name: assets.file_sha(Path(module.__file__))
                    for module in (assets, generation, campaign)}
    original = json.loads(args.regions_report.read_text())
    if original["probe_script_sha256"] != dependencies["probe_native_campaign_regions_117.py"]:
        parser.error("Stale native campaign region source")
    if not original["all_expected_results_matched"] or not original["all_audited_maps_included"]:
        parser.error("Incomplete original region evidence")
    graph_cases = [case for case in original["cases"] if case["name"].startswith("campaign_graph_")]
    import unicorn
    from unicorn.x86_const import (UC_X86_REG_ECX, UC_X86_REG_EAX, UC_X86_REG_EDX,
                                  UC_X86_REG_ESP, UC_X86_REG_EIP)
    image = pe.get_memory_mapped_image()
    image_size = (pe.OPTIONAL_HEADER.SizeOfImage + 4095) & ~4095
    cases = []
    for prior in original["cases"]:
        if not prior["name"].startswith("campaign_path_") or prior["observed"]["reached_target"]:
            continue
        ordinal = int(prior["name"].split("_")[2])
        snapshot_bytes = (args.snapshots / (str(ordinal).zfill(2) + ".json")).read_bytes()
        if assets.sha(snapshot_bytes) != graph_cases[ordinal]["observed"]["snapshot_sha256"]:
            parser.error("Unknown original-generated graph snapshot")
        snapshot = json.loads(snapshot_bytes)
        uc = unicorn.Uc(unicorn.UC_ARCH_X86, unicorn.UC_MODE_32)
        for address, size in ((base, image_size), (MAP, 0x100000), (STACK, 0x10000), (STOP, 0x1000)):
            uc.mem_map(address, size)
        uc.mem_write(base, image)

        def put(address, value, width=4):
            uc.mem_write(address, (value & ((1 << (width * 8)) - 1)).to_bytes(width, "little"))

        def get(address, width=4):
            return int.from_bytes(uc.mem_read(address, width), "little")

        put(0x69B584, MAP)
        put(MAP, len(snapshot["regions"]), 2)
        cursor = ARRAY
        for r in snapshot["regions"]:
            a = MAP + 0x449FC + r["id"] * 64
            for offset, value, size in ((0, r["flags"], 2), (2, r["group"], 2), (4, r["tiles"], 2),
                (6, r["ground_neighbors"], 1), (7, len(r["neighbors"]), 1), (0xC, cursor, 4)):
                put(a + offset, value, size)
            uc.mem_write(a + 0x10, struct.pack("<2i", *r["center_fixed8"]))
            values = r["neighbors"]
            uc.mem_write(cursor, struct.pack("<" + "H" * len(values), *values))
            cursor += len(values) * 2
        assert cursor < CONTEXT
        for offset, key in ((0x160, "source"), (0x162, "target")):
            put(CONTEXT + offset, prior["inputs"][key], 2)
        pixels = {}
        for offset, key in ((4, "source"), (8, "target")):
            point = next((x * 32 + 16, y * 32 + 16) for y, row in enumerate(snapshot["raster"])
                         for x, value in enumerate(row) if value == prior["inputs"][key])
            pixels[key] = list(point)
            put(CONTEXT + offset, point[0], 2)
            put(CONTEXT + offset + 2, point[1], 2)
        sp = STACK + 0xF000
        put(sp, STOP)
        uc.reg_write(UC_X86_REG_ESP, sp)
        uc.reg_write(UC_X86_REG_ECX, CONTEXT)
        count, branches = 0, Counter()
        def check(cpu, address, size, unused):
            nonlocal count
            if address == STOP:
                cpu.emu_stop()
                return
            count += 1
            if (address, size) not in allowed:
                raise RuntimeError("Unreviewed original instruction " + hex(address))
            if address == 0x491803:
                branches["capacity_stop"] += 1
                branches["open_count_at_stop_{}".format(cpu.reg_read(UC_X86_REG_EDX))] += 1
        hook = uc.hook_add(unicorn.UC_HOOK_CODE, check)
        try:
            uc.emu_start(0x491300, STOP + 1, count=2000000)
            assert uc.reg_read(UC_X86_REG_EIP) == STOP and uc.reg_read(UC_X86_REG_ESP) == sp + 4
        finally:
            uc.hook_del(hook)
        stored = get(CONTEXT + 0x86, 2)
        path = [get(CONTEXT + 0x20 + i * 2, 2) for i in range(stored)]
        assert path == prior["observed"]["path"]
        assert uc.reg_read(UC_X86_REG_EAX) == prior["observed"]["returned"]
        assert get(CONTEXT + 0x1C, 1) == prior["observed"]["partial"]
        observed = {"path": path, "stored_count": stored, "used_nodes": get(CONTEXT + 0x164, 2),
            "peak_open_nodes": get(CONTEXT + 0x168, 2), "full_count_low_byte": get(CONTEXT + 0x154, 1),
            "partial": get(CONTEXT + 0x1C, 1), "returned": uc.reg_read(UC_X86_REG_EAX),
            "capacity_stop_branches": dict(branches), "instructions": count}
        if stored < 50:
            assert observed["used_nodes"] == 350 and branches["capacity_stop"] == 1
            assert observed["peak_open_nodes"] < 125
        else:
            assert not branches and observed["full_count_low_byte"] > 50
        cases.append({"name": prior["name"], "passed": True, "observed": observed,
            "inputs": {**prior["inputs"], "pixels": pixels, "snapshot_sha256": assets.sha(snapshot_bytes),
                       "map": graph_cases[ordinal]["name"].removeprefix("campaign_graph_")},
            "oracle": "exact recorded original path/return plus native counters and distinguishing capacity branch"})
    result = {"target_sha256": TARGET_HASH, "version": "1.17.0.1", "probe_script_sha256": assets.file_sha(Path(__file__)),
        "dependency_hashes": dependencies, "campaign_regions_report_sha256": assets.file_sha(args.regions_report),
        "case_count": len(cases), "total_native_checks": len(cases), "all_expected_results_matched": True,
        "failed_cases": 0, "complete_function_returns": len(cases), "fragment_completions": 0,
        "runtime": {"python": platform.python_version(), "architecture": platform.machine(), "unicorn": unicorn.__version__},
        "no_helper_stubs": True, "no_import_execution": True, "full_game_or_campaign_executed": False,
        "maximum_instructions_per_execution": 2000000, "cases": cases,
        "evidence": "complete native path replay on reconstructed read fields from hash-verified original-generated graph snapshots",
        "limits": "Does not rerun graph creation or establish local movement; separate private graph pointers and zero temporary fields."}
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({"checks": len(cases), "all_matched": True, "out": str(args.out)}))


if __name__ == "__main__":
    main()
