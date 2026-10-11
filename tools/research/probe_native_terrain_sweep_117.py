#!/usr/bin/env python3
"""Probe complete original terrain sweep/cache consumers on explicit fixtures.

Native instructions, stock DAT extents and declared contour fixtures execute
without helpers/imports. Geometry comparison follows pinned OpenBW; it does not
claim authentic world setup, local path search or movement-state completion.
"""

import argparse
from bisect import bisect_left, bisect_right
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
import probe_native_contour_fit_117 as fit

TARGET_HASH = fit.TARGET_HASH
CONTEXT, ARRAYS, UNIT, SPRITE, RECT = 0x800000, 0x810000, 0x8E0000, 0x8E1000, 0x8E2000
STACK, STOP, MAP = 0x900000, 0xA00000, 0xB00000
BLOCKS = {"movement_terrain_cache": (0x42DA50, 0x42DB56),
          "build_no_collision_bounds": (0x48B9E0, 0x48BEB9),
          "terrain_sweep": (0x48BEC0, 0x48C2FB),
          "unit_extents": fit.BLOCKS["unit_extents"]}


def expanded(row, inner):
    axis, start, end, direction, info = row
    return axis + inner[direction], start + inner[info & 3], end + inner[info >> 2 & 3]


def sweep_clear(source, destination, inner, edges):
    x0, y0 = source
    x1, y1 = destination
    for side, group in enumerate(edges):
        for row in group:
            axis, start, end = expanded(row, inner)
            if side == 0:
                reached = y1 < y0 and row[0] <= y0 - inner[0] and axis >= y1
                low, high = sorted((x0, x1))
            elif side == 1:
                reached = x1 > x0 and row[0] >= x0 - inner[1] and axis <= x1
                low, high = sorted((y0, y1))
            elif side == 2:
                reached = y1 > y0 and row[0] >= y0 - inner[2] and axis <= y1
                low, high = sorted((x0, x1))
            else:
                reached = x1 < x0 and row[0] <= x0 - inner[3] and axis >= x1
                low, high = sorted((y0, y1))
            if reached and start <= high and end >= low:
                # Original vertical-wall branches compare against the opposite
                # span endpoints before corner flags. This differs from the
                # symmetric pinned OpenBW model: any overlap rejects here.
                if side in (1, 3) or row[4] & 0x30 != 0x30 or start <= low or end >= high:
                    return False
    return True


def collision_bounds(position, inner, edges):
    """Round-robin nearest-edge contraction, independently expressed geometry."""
    x, y = position
    bounds = [x - 128, y - 128, x + 128, y + 128]
    cursors = [bisect_right([row[0] for row in edges[0]], y - inner[0]),
               bisect_left([row[0] for row in edges[1]], x - inner[1]),
               bisect_left([row[0] for row in edges[2]], y - inner[2]),
               bisect_right([row[0] for row in edges[3]], x - inner[3])]
    while True:
        done = True
        for side, group in enumerate(edges):
            index = cursors[side]
            if side in (0, 3):
                if index == 0:
                    continue
                index -= 1
                cursors[side] = index
            else:
                if index == len(group):
                    continue
                cursors[side] += 1
            done = False
            axis, start, end = expanded(group[index], inner)
            if side == 0:
                if axis < bounds[1]:
                    cursors[side] = 0
                elif end >= bounds[0] and start <= bounds[2]:
                    if x < start and y - axis < start - x:
                        bounds[2] = start - 1
                    elif x > end and y - axis < x - end:
                        bounds[0] = end + 1
                    else:
                        bounds[1] = axis + 1
            elif side == 1:
                if axis > bounds[2]:
                    cursors[side] = len(group)
                elif end >= bounds[1] and start <= bounds[3]:
                    if y < start and axis - x < start - y:
                        bounds[3] = start - 1
                    elif y > end and axis - x < y - end:
                        bounds[1] = end + 1
                    else:
                        bounds[2] = axis - 1
            elif side == 2:
                if axis > bounds[3]:
                    cursors[side] = len(group)
                elif end >= bounds[0] and start <= bounds[2]:
                    if x < start and axis - y < start - x:
                        bounds[2] = start - 1
                    elif x > end and axis - y < x - end:
                        bounds[0] = end + 1
                    else:
                        bounds[3] = axis - 1
            else:
                if axis < bounds[0]:
                    cursors[side] = 0
                elif end >= bounds[1] and start <= bounds[3]:
                    if y < start and x - axis < start - y:
                        bounds[3] = start - 1
                    elif y > end and x - axis < y - end:
                        bounds[1] = end + 1
                    else:
                        bounds[0] = axis + 1
        if done:
            return bounds


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--units-dat", type=Path)
    parser.add_argument("--pyms", type=Path)
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
    witnesses, reviewed = {}, {}
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
    if any(value is None for value in (args.units_dat, args.pyms, args.out)):
        parser.error("Execution requires stock --units-dat, --pyms and --out")
    dat_bytes = args.units_dat.read_bytes()
    assert assets.sha(dat_bytes) == "da2ee6f116b77329048ec6be9e02721a527fde23d4b05b1b1336e634490abcba"
    assert subprocess.check_output(["git", "-C", str(args.pyms), "rev-parse", "HEAD"], text=True).strip() == fit.PYMS
    sys.path.insert(0, str(args.pyms))
    from PyMS.FileFormats.DAT.UnitsDAT import UnitsDAT
    dat = UnitsDAT()
    dat.load_data(dat_bytes)
    extents = [(row.unit_extents.left, row.unit_extents.up, row.unit_extents.right, row.unit_extents.down) for row in dat.entries]
    import unicorn
    from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_ESP, UC_X86_REG_EIP
    image = pe.get_memory_mapped_image()
    image_size = (pe.OPTIONAL_HEADER.SizeOfImage + 4095) & ~4095
    uc = unicorn.Uc(unicorn.UC_ARCH_X86, unicorn.UC_MODE_32)
    for address, size in ((base, image_size), (CONTEXT, 0x100000), (STACK, 0x10000), (STOP, 0x1000), (MAP, 0x100000)):
        uc.mem_map(address, size)
    uc.mem_write(base, image)
    for uid, values in enumerate(extents):
        uc.mem_write(0x522C38 + uid * 8, struct.pack("<4H", *values))
    entries = {lo: name for name, (lo, hi) in BLOCKS.items()}
    cases = []

    def put(address, value, width=4):
        uc.mem_write(address, (value & ((1 << (width * 8)) - 1)).to_bytes(width, "little"))

    def call(entry, arguments=(), ecx=CONTEXT):
        sp = STACK + 0xF000
        uc.mem_write(sp, struct.pack("<" + "I" * (len(arguments) + 1), STOP,
                                    *(value & 0xFFFFFFFF for value in arguments)))
        uc.reg_write(UC_X86_REG_ESP, sp)
        uc.reg_write(UC_X86_REG_ECX, ecx)
        count, trace = 0, Counter()
        def check(cpu, address, size, unused):
            nonlocal count
            count += 1
            if address == STOP:
                cpu.emu_stop()
            elif reviewed.get(address) != size:
                raise RuntimeError("Unreviewed original instruction " + hex(address))
            elif address in entries:
                trace[entries[address]] += 1
        hook = uc.hook_add(unicorn.UC_HOOK_CODE, check)
        try:
            uc.emu_start(entry, STOP, count=100000)
            assert uc.reg_read(UC_X86_REG_EIP) == STOP
            assert uc.reg_read(UC_X86_REG_ESP) == sp + 4 + len(arguments) * 4
        finally:
            uc.hook_del(hook)
        return {"entry": hex(entry), "scope": "complete original function", "instructions": count,
                "calls": dict(trace), "returned": uc.reg_read(UC_X86_REG_EAX)}

    def set_edges(edges):
        for side, rows in enumerate(edges):
            address = ARRAYS + side * 0x10000
            rows = sorted(rows, key=lambda row: row[:2])
            put(CONTEXT + side * 4, address)
            put(CONTEXT + 0x10 + side * 2, len(rows), 2)
            if rows:
                uc.mem_write(address, b"".join(struct.pack("<3h2B", *row) for row in rows))

    left, up, right, down = extents[0]
    inner = [up, -right, -down, left]
    init = call(0x4891C0, arguments=(0,))
    assert list(struct.unpack("<4h", uc.mem_read(CONTEXT + 0x20, 8))) == inner
    cases.append({"name": "stock_Marine_extent_setup", "passed": True, "runs": [init], "observed": {"inner": inner}})
    for side in range(4):
        for info in range(64):
            edges = [[], [], [], []]
            edges[side] = [[400, 380, 420, side, info]]
            set_edges(edges)
            for ordinal, span in enumerate(((400, 400), (350, 450), (450, 350), (350, 399), (401, 450),
                                            (380, 420), (381, 419), (379, 421), (390, 410))):
                if side == 0:
                    source, destination = (span[0], 450), (span[1], 350)
                elif side == 1:
                    source, destination = (350, span[0]), (450, span[1])
                elif side == 2:
                    source, destination = (span[0], 350), (span[1], 450)
                else:
                    source, destination = (450, span[0]), (350, span[1])
                expected = int(sweep_clear(source, destination, inner, edges))
                run = call(0x48BEC0, arguments=source + destination)
                assert run["returned"] == expected, (side, info, span, expected, run)
                cases.append({"name": "sweep_direction_{}_info_{}_span_{}".format(side, info, ordinal), "passed": True,
                    "inputs": {"source": list(source), "destination": list(destination), "edge": edges[side][0]},
                    "runs": [run], "observed": {"clear": run["returned"]}, "expected": {"clear": expected}})
    # Actual movement cache composes original extents, bound contraction and sweep.
    edges = [[[350, 300, 500, 0, 0x3D]], [[450, 320, 480, 1, 0x32]],
             [[450, 280, 500, 2, 0x3D]], [[350, 330, 470, 3, 0x32]]]
    set_edges(edges)
    put(0x69B584, MAP)
    put(MAP + 0x97A1C, CONTEXT)
    put(UNIT + 0xC, SPRITE)
    movements = ((0, 0), (-1, 0), (0, -1), (40, 0), (0, 40), (-40, 0), (0, -40),
                 (100, 0), (0, 100), (-100, 0), (0, -100), (100, 100), (-100, -100))
    for uid in (0, 7, 30, 37, 41, 65, 106, 160):
        left, up, right, down = extents[uid]
        inner = [up, -right, -down, left]
        for position in ((400, 400), (310, 400), (490, 400), (400, 310), (400, 490),
                         (64, 64), (8, 9), (4095, 4095), (8191, 8191)):
            put(UNIT + 0x64, uid, 2)
            uc.mem_write(SPRITE + 0x14, struct.pack("<2h", *position))
            bounds = collision_bounds(position, inner, edges)
            uc.mem_write(UNIT + 0x108, struct.pack("<4h", 0, 0, 0, 0))
            cached = [0, 0, 0, 0]
            for ordinal, movement in enumerate(movements):
                destination = tuple(position[i] + movement[i] for i in range(2))
                inside_cache = cached[0] <= destination[0] <= cached[2] and cached[1] <= destination[1] <= cached[3]
                if not inside_cache:
                    cached = bounds
                inside_new = cached[0] <= destination[0] <= cached[2] and cached[1] <= destination[1] <= cached[3]
                expected = int(not inside_new and not sweep_clear(position, destination, inner, edges))
                run = call(0x42DA50, arguments=movement, ecx=UNIT)
                actual_bounds = list(struct.unpack("<4h", uc.mem_read(UNIT + 0x108, 8)))
                assert actual_bounds == cached, (uid, position, movement, cached, actual_bounds)
                assert run["returned"] == expected, (uid, position, movement, expected, run)
                assert (run["calls"].get("build_no_collision_bounds", 0) == 0) == inside_cache
                cases.append({"name": "movement_cache_type_{}_position_{}_step_{}".format(uid, position, ordinal),
                    "passed": True, "inputs": {"unit_id": uid, "position": list(position), "movement": list(movement)},
                    "runs": [run], "observed": {"collision": run["returned"], "bounds": actual_bounds,
                    "used_existing_cache": inside_cache}, "expected": {"collision": expected, "bounds": cached}})
    conflicts = []
    for side in range(4):
        case = next(row for row in cases if row["name"] == "sweep_direction_{}_info_48_span_1".format(side))
        conflicts.append({"case": case["name"], "symmetric_pinned_OpenBW_clear": 1,
                          "native_clear": case["observed"]["clear"], "matches_symmetric_model": side in (0, 2)})
    result = {"target_sha256": TARGET_HASH, "version": "1.17.0.1", "probe_script_sha256": assets.file_sha(Path(__file__)),
        "dependency_hashes": {Path(module.__file__).name: assets.file_sha(Path(module.__file__)) for module in (assets, fit)},
        "units_dat_sha256": assets.sha(dat_bytes), "pyms_commit": fit.PYMS, "case_count": len(cases),
        "total_native_checks": len(cases), "all_expected_results_matched": True, "failed_cases": 0,
        "complete_function_returns": sum(len(case["runs"]) for case in cases), "fragment_completions": 0,
        "no_helper_stubs": True, "no_import_execution": True, "full_game_or_campaign_executed": False,
        "maximum_instructions_per_execution": 100000,
        "runtime": {"python": platform.python_version(), "architecture": platform.machine(), "unicorn": unicorn.__version__},
        "cases": cases, "reference_conflicts": conflicts,
        "limits": "Synthetic contours/private units; coordinate fixtures8..8191 plus signed cache bounds/destinations; no real terrain setup, unit collision or full movement FSM"}
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({"checks": len(cases), "complete_returns": result["complete_function_returns"]}))


if __name__ == "__main__":
    main()
