#!/usr/bin/env python3
"""Probe original 1.17 MTXM/CV5 flags and pixel height on authentic inputs.

Default mode only hashes/disassembles. Execution is explicit, private and
bounded, without helper stubs/imports or a complete original game. Megatile
flag/class producers are labeled fragments; map import and height fully return.
"""

import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import platform
import random
import struct

import capstone
import pefile
import inspect_terrain_assets as assets
import probe_native_region_generation_117 as generation

TARGET_HASH = generation.TARGET_HASH
MAP, STACK, STOP = 0x800000, 0x900000, 0xA00000
FLAGS, MEGA_IDS, VF4 = 0xC00000, 0xC40000, 0xC60000
MEGA_FLAGS, CV5, MTXM, ALTERNATE = 0xD10000, 0xD20000, 0xD40000, 0xD60000
BLOCKS = {"megatile_flags": (0x445540, 0x44566F),
          "map_terrain_import": (0x445720, 0x4457EF),
          "pixel_height": (0x445690, 0x44571D),
          "terrain_classes": (0x490E90, 0x490F44)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--terrain-report", type=Path)
    parser.add_argument("--terrain-data", type=Path)
    parser.add_argument("--campaign-data", type=Path)
    parser.add_argument("--static-out", type=Path)
    parser.add_argument("--out", type=Path)
    parser.add_argument("--execute-original-code", action="store_true")
    args = parser.parse_args()
    own_hash = assets.file_sha(Path(__file__))
    dependency_hashes = {Path(module.__file__).name: assets.file_sha(Path(module.__file__))
                         for module in (assets, generation)}
    data = args.exe.read_bytes()
    if assets.sha(data) != TARGET_HASH:
        parser.error("Unknown executable hash; reviewed addresses do not apply")
    pe = pefile.PE(data=data)
    base = pe.OPTIONAL_HEADER.ImageBase
    assert base == 0x400000 and pe.FILE_HEADER.Machine == 0x14C
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    witnesses = {}
    for name, (lo, hi) in BLOCKS.items():
        rows = list(md.disasm(pe.get_data(lo - base, hi - lo), lo))
        if not rows or rows[-1].address + rows[-1].size != hi:
            parser.error("Invalid reviewed instruction boundary: " + name)
        witnesses[name] = [{"va": hex(i.address), "bytes": i.bytes.hex(),
                            "mnemonic": i.mnemonic, "operands": i.op_str} for i in rows]
    if args.static_out:
        args.static_out.parent.mkdir(parents=True, exist_ok=True)
        args.static_out.write_text(json.dumps({"target_sha256": TARGET_HASH,
            "version": "1.17.0.1", "evidence": "static; analytical labels", "witnesses": witnesses}, indent=2) + "\n")
    if not args.execute_original_code:
        print(json.dumps({"blocks": len(BLOCKS), "target_sha256": TARGET_HASH, "mode": "static only"}))
        return
    if any(value is None for value in (args.terrain_report, args.terrain_data, args.campaign_data, args.out)):
        parser.error("Execution requires --terrain-report, --terrain-data, --campaign-data and --out")
    provenance = json.loads(args.terrain_report.read_text())
    if provenance["script_sha256"] != dependency_hashes["inspect_terrain_assets.py"]:
        parser.error("Terrain extraction source hash mismatch")
    decoded = {}
    for tileset in assets.TILESETS:
        payloads = {}
        for extension in ("cv5", "vf4"):
            payload = (args.terrain_data / "tileset" / (tileset + "." + extension)).read_bytes()
            identity = provenance["effective"]["tileset\\{}.{}".format(tileset, extension)]
            if assets.sha(payload) != identity["sha256"]:
                parser.error("Unknown original terrain input: " + tileset + "." + extension)
            payloads[extension] = payload
        decoded[tileset] = (payloads, list(struct.iter_unpack("<26H", payloads["cv5"])),
                            list(struct.iter_unpack("<16H", payloads["vf4"])))
    import unicorn
    from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDX,
        UC_X86_REG_ESP, UC_X86_REG_EBP, UC_X86_REG_EIP, UC_X86_REG_ESI)
    image = pe.get_memory_mapped_image()
    image_size = (pe.OPTIONAL_HEADER.SizeOfImage + 4095) & ~4095
    reviewed = {(int(row["va"], 16), len(bytes.fromhex(row["bytes"])))
                for rows in witnesses.values() for row in rows}
    entries = {v[0]: name for name, v in BLOCKS.items()}
    cases, native_flags = [], {}

    def put(uc, address, value, width=4):
        uc.mem_write(address, (value & ((1 << (width * 8)) - 1)).to_bytes(width, "little"))

    def get(uc, address, width=4):
        return int.from_bytes(uc.mem_read(address, width), "little")

    def machine(width, height, cv5_bytes, vf4_bytes):
        assert 0 <= width <= 256 and 0 <= height <= 256
        uc = unicorn.Uc(unicorn.UC_ARCH_X86, unicorn.UC_MODE_32)
        for address, size in ((base, image_size), (MAP, 0x100000), (STACK, 0x10000),
                              (STOP, 0x1000), (FLAGS, 0x200000)):
            uc.mem_map(address, size)
        uc.mem_write(base, image)
        uc.mem_write(CV5, cv5_bytes)
        uc.mem_write(VF4, vf4_bytes)
        for address, value, width_bytes in ((0x5264B4, width, 2), (0x5264B6, height, 2),
            (0x666458, MTXM, 4), (0x666454, FLAGS, 4), (0x54323C, MEGA_IDS, 4),
            (0x66644C, VF4, 4), (0x666448, len(vf4_bytes) // 32, 2),
            (0x6CE650, CV5, 4), (0x666968, len(cv5_bytes) // 52, 4),
            (0x666450, MEGA_FLAGS, 4), (0x66696C, ALTERNATE, 4), (0x69B584, MAP, 4)):
            put(uc, address, value, width_bytes)
        return uc

    def execute(uc, entry, expected_sp, scope):
        count, trace = 0, Counter()
        def check(cpu, va, width, unused):
            nonlocal count
            count += 1
            if va == STOP:
                cpu.emu_stop()
                return
            if (va, width) not in reviewed:
                raise RuntimeError("Unreviewed original instruction " + hex(va))
            if va in entries:
                trace[entries[va]] += 1
        hook = uc.hook_add(unicorn.UC_HOOK_CODE, check)
        try:
            uc.emu_start(entry, STOP + 1, count=5000000)
            if uc.reg_read(UC_X86_REG_EIP) != STOP or uc.reg_read(UC_X86_REG_ESP) != expected_sp:
                raise RuntimeError("Native return/stack contract failed: " + hex(entry))
        finally:
            uc.hook_del(hook)
        return {"scope": scope, "entry": hex(entry), "returned": uc.reg_read(UC_X86_REG_EAX),
                "instructions": count, "calls": dict(trace)}

    def call(uc, entry, ecx=0, edx=0):
        sp = STACK + 0xF000
        put(uc, sp, STOP)
        uc.reg_write(UC_X86_REG_ESP, sp)
        uc.reg_write(UC_X86_REG_ECX, ecx)
        uc.reg_write(UC_X86_REG_EDX, edx)
        return execute(uc, entry, sp + 4, "complete original function")

    def frame(uc, local_size, saved):
        bp = STACK + 0xE000
        uc.mem_write(bp - 0x100, bytes(0x110))
        put(uc, bp + 4, STOP)
        uc.reg_write(UC_X86_REG_EBP, bp)
        uc.reg_write(UC_X86_REG_ESP, bp - local_size - saved * 4)
        return bp

    def mega_fragment(uc, records):
        bp = frame(uc, 0x14, 0)
        uc.reg_write(UC_X86_REG_EAX, MEGA_FLAGS)
        run = execute(uc, 0x445562, bp + 8, "post-allocation megatile flag fragment")
        actual = list(struct.unpack("<" + "I" * len(records), uc.mem_read(MEGA_FLAGS, len(records) * 4)))
        expected = [generation.megatile_flags_reference(record) for record in records]
        assert actual == expected
        return run, actual

    def classes_fragment(uc, width, height, flags):
        bp = frame(uc, 0xC, 2)
        uc.reg_write(UC_X86_REG_ESI, FLAGS)
        run = execute(uc, 0x490E9F, bp + 8, "post-walk terrain-class fragment")
        actual = b"".join(bytes(uc.mem_read(MAP + 0xC + y * 512, width * 2)) for y in range(height))
        expected = struct.pack("<" + "H" * len(flags), *[generation.class_from_flags(value) for value in flags])
        assert actual == expected
        return run, actual

    def expected_import(values, cv5, megatile_flags, initial):
        flags, ids = [], []
        for value, old in zip(values, initial):
            group, cell = value >> 4 & 0x7FF, value & 15
            if group >= len(cv5):
                break
            mega = cv5[group][10 + cell]
            merged = old & 0xFFFF | ((cv5[group][1] & 0xD8FA) << 16) | megatile_flags[mega & 0x7FFF]
            if value & 0x8000:
                merged |= 0x400000
                mega |= 0x8000
            flags.append(merged)
            ids.append(mega)
        return flags, ids

    def initialize_input(uc, values, initial):
        uc.mem_write(MTXM, struct.pack("<" + "H" * len(values), *values))
        uc.mem_write(FLAGS, struct.pack("<" + "I" * len(initial), *initial))
        uc.mem_write(MEGA_IDS, struct.pack("<" + "H" * len(values), *([0xBEEF] * len(values))))

    def record(name, runs, observed, inputs=None):
        cases.append({"name": name, "runs": runs, "inputs": inputs, "observed": observed,
                      "passed": True, "oracle": "whole-output comparison with independent bit/record specification"})

    for tileset in assets.TILESETS:
        payloads, cv5, records = decoded[tileset]
        uc = machine(8, 8, payloads["cv5"], payloads["vf4"])
        run, flags = mega_fragment(uc, records)
        native_flags[tileset] = flags
        record("retail_megatiles_" + tileset, [run],
               {"records": len(records), "native_output_sha256": assets.sha(struct.pack("<" + "I" * len(flags), *flags)),
                "flag_histogram": {hex(k): v for k, v in sorted(Counter(flags).items())}},
               {ext + "_sha256": assets.sha(payloads[ext]) for ext in payloads})

    rng = random.Random(1717)
    height_maps = {}
    for index, identity in enumerate(provenance["campaigns"]):
        chk = (args.campaign_data / identity["path"].replace("\\", "/")).read_bytes()
        if assets.sha(chk) != identity["chk_sha256"]:
            parser.error("Unknown CHK identity: " + identity["path"])
        chunks = assets.sections(chk)
        width, height = identity["width"], identity["height"]
        values = [x[0] for x in struct.iter_unpack("<H", chunks[b"MTXM"][0])]
        tileset = identity["tileset"]
        payloads, cv5, records = decoded[tileset]
        uc = machine(width, height, payloads["cv5"], payloads["vf4"])
        uc.mem_write(MEGA_FLAGS, struct.pack("<" + "I" * len(native_flags[tileset]), *native_flags[tileset]))
        initial = [0xFFFF] * len(values)
        initialize_input(uc, values, initial)
        run = call(uc, 0x445720)
        assert run["returned"] == 1
        expected_flags, expected_ids = expected_import(values, cv5, native_flags[tileset], initial)
        actual_flags_bytes = bytes(uc.mem_read(FLAGS, len(values) * 4))
        actual_ids_bytes = bytes(uc.mem_read(MEGA_IDS, len(values) * 2))
        assert actual_flags_bytes == struct.pack("<" + "I" * len(values), *expected_flags)
        assert actual_ids_bytes == struct.pack("<" + "H" * len(values), *expected_ids)
        classified, raster_bytes = classes_fragment(uc, width, height, expected_flags)
        record("campaign_import_" + identity["path"], [run, classified],
               {"cells": len(values), "native_flags_sha256": assets.sha(actual_flags_bytes),
                "native_megatile_ids_sha256": assets.sha(actual_ids_bytes),
                "native_region_classes_sha256": assets.sha(raster_bytes),
                "class_histogram": {hex(k): v for k, v in sorted(Counter(generation.class_from_flags(v) for v in expected_flags).items())}},
               {"chk_sha256": identity["chk_sha256"], "width": width, "height": height,
                "tileset": tileset, "initial_low_visibility_word": 65535})
        if tileset not in height_maps:
            height_maps[tileset] = (uc, width, height, values)
        if (index + 1) % 8 == 0:
            print(json.dumps({"campaign_imports_complete": index + 1, "of": len(provenance["campaigns"])}), flush=True)

    for tileset, (uc, width, height, values) in height_maps.items():
        _, cv5, vf4 = decoded[tileset]
        for ordinal in range(32):
            tile_x, tile_y = rng.randrange(width), rng.randrange(height)
            mini_x, mini_y = ordinal % 4, ordinal // 4 % 4
            x, y = tile_x * 32 + mini_x * 8 + 4, tile_y * 32 + mini_y * 8 + 4
            use_alternate = ordinal >= 16
            alt = values[rng.randrange(len(values))] if use_alternate else 0
            put(uc, ALTERNATE + (tile_y * width + tile_x) * 2, alt, 2)
            raw = alt or values[tile_y * width + tile_x]
            mega = cv5[raw >> 4 & 0x7FF][10 + (raw & 15)]
            mini_flag = vf4[mega][mini_y * 4 + mini_x]
            expected = 2 if mini_flag & 4 else int(bool(mini_flag & 2))
            run = call(uc, 0x445690, x, y)
            assert run["returned"] == expected
            record("retail_height_{}_{}".format(tileset, ordinal), [run], run["returned"],
                   {"pixel": [x, y], "alternate_tile_word": alt, "selected_tile_word": raw,
                    "selected_vf4_word": mini_flag, "expected_height": expected})

    # Header-bit masking, retained low word, CV5 filter and partial failure.
    records = [[((i < count) | (2 if mode == 1 else 4 if mode == 2 else 8 if mode == 3 else 0))
                for i in range(16)] for count in (0, 1, 12, 13, 16) for mode in range(4)]
    cv5 = [tuple([0, flags] + [0] * 8 + [(group + cell) % len(records) for cell in range(16)])
           for group, flags in enumerate((0, 0xFFFF, 0xD8FA, 0x2705, 0x40, 0x8000, 0x20, 0x100))]
    cv5_bytes = b"".join(struct.pack("<26H", *row) for row in cv5)
    vf4_bytes = b"".join(struct.pack("<16H", *row) for row in records)
    for bad_index, high_bit in ((None, False), (None, True), (0, False), (1, False), (31, True), (63, True)):
        uc = machine(8, 8, cv5_bytes, vf4_bytes)
        generated, flags = mega_fragment(uc, records)
        values = [((i % 8) << 4) | (i % 16) | (0x8000 if high_bit else 0) for i in range(64)]
        if bad_index is not None:
            values[bad_index] = (8 << 4) | (0x8000 if high_bit else 0)
        initial = [(0xAABB0000 | i * 73) for i in range(64)]
        initialize_input(uc, values, initial)
        run = call(uc, 0x445720)
        expected_flags, expected_ids = expected_import(values, cv5, flags, initial)
        assert run["returned"] == int(bad_index is None)
        actual_flags = list(struct.unpack("<64I", uc.mem_read(FLAGS, 256)))
        actual_ids = list(struct.unpack("<64H", uc.mem_read(MEGA_IDS, 128)))
        assert actual_flags == expected_flags + initial[len(expected_flags):]
        assert actual_ids == expected_ids + [0xBEEF] * (64 - len(expected_ids))
        record("synthetic_import_{}_{}".format(bad_index, high_bit), [generated, run],
               {"flags": actual_flags, "ids": actual_ids, "written_cells": len(expected_flags), "returned": run["returned"]},
               {"tile_words": values, "initial_flags": initial, "cv5_records": cv5, "vf4_records": records})
    for width, height in ((0, 0), (0, 8), (8, 0)):
        uc = machine(width, height, cv5_bytes, vf4_bytes)
        run = call(uc, 0x445720)
        assert run["returned"] == 1
        record("empty_dimensions_{}_{}".format(width, height), [run], run["returned"])

    # Every low VF4 bit combination, precedence, ignored high bits, ordinary
    # header masking, and the alternate-tile zero sentinel use real returns.
    height_cv5 = [tuple([0] * 10 + [mega] * 16) for mega in (0, 1)]
    height_vf4 = [list(range(16)), [value | 0xF000 for value in range(16)]]
    uc = machine(8, 8, b"".join(struct.pack("<26H", *row) for row in height_cv5),
                 b"".join(struct.pack("<16H", *row) for row in height_vf4))
    for mode in range(4):
        raw = 0x8000 if mode & 1 else 0
        alternate = 0x8010 if mode & 2 else 0
        put(uc, MTXM, raw, 2)
        put(uc, ALTERNATE, alternate, 2)
        for mini in range(16):
            x, y = (mini % 4) * 8 + 7, (mini // 4) * 8 + 7
            value = height_vf4[int(bool(alternate))][mini]
            expected = 2 if value & 4 else int(bool(value & 2))
            run = call(uc, 0x445690, x, y)
            assert run["returned"] == expected
            record("synthetic_height_{}_{}".format(mode, mini), [run], run["returned"],
                   {"pixel": [x, y], "ordinary_tile_word": raw,
                    "alternate_tile_word": alternate, "selected_vf4_word": value,
                    "expected_height": expected})

    complete = sum(run["scope"] == "complete original function" for case in cases for run in case["runs"])
    fragments = sum(run["scope"] != "complete original function" for case in cases for run in case["runs"])
    result = {"target_sha256": TARGET_HASH, "version": "1.17.0.1", "probe_script_sha256": own_hash,
        "dependency_hashes": dependency_hashes, "terrain_report_sha256": assets.file_sha(args.terrain_report),
        "case_count": len(cases), "total_native_checks": len(cases), "all_expected_results_matched": True,
        "failed_cases": 0, "complete_function_returns": complete, "fragment_completions": fragments,
        "authentic_campaign_imports": len(provenance["campaigns"]),
        "authentic_campaign_cells": provenance["total_campaign_cells"],
        "authentic_vf4_records": sum(len(value[2]) for value in decoded.values()),
        "runtime": {"python": platform.python_version(), "architecture": platform.machine(),
                    "unicorn": unicorn.__version__, "pefile": pefile.__version__, "capstone": capstone.__version__},
        "maximum_instructions_per_execution": 5000000, "no_helper_stubs": True,
        "no_windows_imports_executed": True, "full_original_game_or_campaign_executed": False,
        "evidence": "complete map import/height and explicit allocation-dependent fragments; authentic hash-verified CV5/VF4/MTXM",
        "cases": cases, "limits": "CHK chunk dispatch/allocation, dynamic doodad mutation, contours and authentic full campaign graphs remain separate."}
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({"checks": len(cases), "complete_returns": complete, "fragments": fragments,
                      "all_matched": True, "output": str(args.out)}))


if __name__ == "__main__":
    main()
