#!/usr/bin/env python3
"""Recover original map-bounds, pair and swept-unit collision consumers.

Stock extents and explicit sorted spatial/pair fixtures execute complete native
functions. No unit-creation, spatial insertion, movement FSM or imports run.
"""

import argparse
from collections import Counter
import itertools
import json
from pathlib import Path
import platform
import struct
import subprocess
import sys

import capstone
import pefile
import inspect_terrain_assets as assets
import probe_native_terrain_sweep_117 as terrain_sweep
import probe_native_trigger_unit_counts_117 as counts

TARGET_HASH = terrain_sweep.TARGET_HASH
POOL, SPRITES, STACK, STOP = 0x5DA3A0, 0x800000, 0x900000, 0xA00000
CONTEXT, ARRAYS, MAP = 0x810000, 0x820000, 0xB00000
BLOCKS = dict(terrain_sweep.BLOCKS)
BLOCKS.update({"movement_terrain_or_bounds": (0x42D990, 0x42DA2E),
              "movement_unit_collision": (0x42D900, 0x42D98F),
              "collidable_mover_gate": (0x42DA30, 0x42DA44),
              "swept_spatial_query": (0x4217F0, 0x421BDA),
              "collision_pair": (0x42EBF0, 0x42EC67),
              "collision_hostility": (0x4290E0, 0x429110),
              "spatial_release": counts.BLOCKS["spatial_release"],
              "unit_pool_base": counts.BLOCKS["unit_pool_base"]})


def pair_allowed(blocker, mover, relation):
    if blocker["status"] & 0x400000 and relation == 0:
        return False
    if mover["type"] == 35:
        if not blocker["status"] & 2:
            return False
    elif blocker["type"] == 35:
        return False
    if mover["status"] & 0x800000 and not blocker["status"] & 2:
        return bool(blocker["status"] & 0x800000 and
                    mover["order"] != 84 and blocker["order"] == 82)
    return True


def swept_indices(units, axes, movement):
    """Inclusive swept edge crossings; preserve duplicates and supplied ties."""
    source = units[0]
    dx, dy = movement if not source.get("hidden") else (0, 0)
    left, top, right, bottom = source["bounds"]
    result = []
    for axis, delta in enumerate((dx, dy)):
        if delta == 0:
            continue
        edge = (right if delta > 0 else left) if axis == 0 else (bottom if delta > 0 else top)
        target = edge + delta
        candidates = axes[axis] if delta > 0 else reversed(axes[axis])
        for coordinate, index, high in candidates:
            if index in (0, 1) or not min(edge, target) <= coordinate <= max(edge, target):
                continue
            other = units[index - 1]["bounds"]
            low, upper = (top + dy, bottom + dy) if axis == 0 else (left + dx, right + dx)
            if other[axis ^ 1] <= upper and other[(axis ^ 1) + 2] >= low:
                result.append(index)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    for name in ("units-dat", "pyms", "static-out", "out"):
        parser.add_argument("--" + name, type=Path)
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
        assert rows[-1].address + rows[-1].size == hi, name
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
        parser.error("Execution requires --units-dat, --pyms and --out")
    assert subprocess.check_output(["git", "-C", str(args.pyms), "rev-parse", "HEAD"], text=True).strip() == counts.PYMS
    sys.path.insert(0, str(args.pyms))
    from PyMS.FileFormats.DAT.UnitsDAT import UnitsDAT
    dat_bytes = args.units_dat.read_bytes()
    assert assets.sha(dat_bytes) == "da2ee6f116b77329048ec6be9e02721a527fde23d4b05b1b1336e634490abcba"
    dat = UnitsDAT()
    dat.load_data(dat_bytes)
    extents = [(row.unit_extents.left, row.unit_extents.up, row.unit_extents.right, row.unit_extents.down) for row in dat.entries]
    import unicorn
    from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_ESP, UC_X86_REG_EIP
    uc = unicorn.Uc(unicorn.UC_ARCH_X86, unicorn.UC_MODE_32)
    for address, size in ((base, (pe.OPTIONAL_HEADER.SizeOfImage + 4095) & ~4095),
                          (SPRITES, 0x100000), (STACK, 0x10000), (STOP, 0x1000), (MAP, 0x100000)):
        uc.mem_map(address, size)
    uc.mem_write(base, pe.get_memory_mapped_image())
    entries = {lo: name for name, (lo, hi) in BLOCKS.items()}
    cases = []

    def put(address, value, width=4):
        uc.mem_write(address, (value & ((1 << (width * 8)) - 1)).to_bytes(width, "little"))

    def get(address, width=4):
        return int.from_bytes(uc.mem_read(address, width), "little")

    for uid, values in enumerate(extents):
        uc.mem_write(0x522C38 + uid * 8, struct.pack("<4H", *values))
    put(0x543230, 1024, 2)
    put(0x543234, 1024, 2)
    put(0x69B584, MAP)
    put(MAP + 0x97A1C, CONTEXT)
    edges = [[[400, 350, 450, direction, 0]] for direction in range(4)]
    for side, group in enumerate(edges):
        address = ARRAYS + side * 0x10000
        put(CONTEXT + side * 4, address)
        put(CONTEXT + 0x10 + side * 2, len(group), 2)
        uc.mem_write(address, b"".join(struct.pack("<3h2B", *row) for row in group))

    def call(entry, arguments=(), ecx=POOL, edx=0):
        sp = STACK + 0xF000
        uc.mem_write(sp, struct.pack("<" + "I" * (len(arguments) + 1), STOP,
                                    *(value & 0xFFFFFFFF for value in arguments)))
        uc.reg_write(UC_X86_REG_ESP, sp)
        uc.reg_write(UC_X86_REG_ECX, ecx & 0xFFFFFFFF)
        uc.reg_write(UC_X86_REG_EDX, edx & 0xFFFFFFFF)
        count, trace = 0, Counter()
        def check(cpu, address, size, unused):
            nonlocal count
            if address == STOP:
                cpu.emu_stop()
                return
            if reviewed.get(address) != size:
                raise RuntimeError("Unreviewed original instruction " + hex(address))
            count += 1
            if address in entries:
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

    def unit(index, uid=0, status=0x100000, order=0, owner=0, point=(400, 400), **extra):
        left, up, right, down = extents[uid]
        return {"index": index, "type": uid, "status": status, "order": order, "owner": owner,
                "point": list(point), "bounds": [point[0] - left, point[1] - up, point[0] + right, point[1] + down], **extra}

    def setup(units, relation=2, tie_reverse=False):
        uc.mem_write(0x534914, bytes([2] * 144))
        axes = [[], []]
        for item in units:
            index = item["index"]
            address, sprite = POOL + (index - 1) * 0x150, SPRITES + index * 0x40
            uc.mem_write(address, bytes(0x150))
            uc.mem_write(sprite, bytes(0x40))
            for offset, value, width in ((0xC, sprite, 4), (0x64, item["type"], 2), (0xDC, item["status"], 4),
                                          (0x4C, item["owner"], 1), (0x4D, item["order"], 1),
                                          (0x105, item.get("finder_flags", 1), 1)):
                put(address + offset, value, width)
            uc.mem_write(sprite + 0x14, struct.pack("<2h", *item["point"]))
            put(sprite + 0xA, item.get("sprite_owner", item["owner"]), 1)
            put(sprite + 0xE, 0x20 if item.get("hidden") else 0, 1)
            for axis in range(2):
                axes[axis].extend((item["bounds"][axis + high * 2], index, high) for high in (0, 1))
        blocker_owner = units[1].get("sprite_owner", units[1]["owner"]) if units[1]["owner"] == 11 else units[1]["owner"]
        put(0x534914 + units[0]["owner"] * 12 + blocker_owner, relation, 1)
        for axis, table in enumerate((0x5C99B8, 0x5D03F8)):
            if tie_reverse:
                axes[axis].reverse()
            axes[axis].sort(key=lambda row: row[0])
            axes[axis] = [(-32768, 0, 0)] + axes[axis] + [(32767, 0, 1)]
            for position, (coordinate, index, high) in enumerate(axes[axis]):
                uc.mem_write(table + position * 8, struct.pack("<Ii", index, coordinate))
                if index:
                    put(POOL + (index - 1) * 0x150 + (0x144 if axis else 0x13C) + high * 4, position)
        put(0x5C99B0, len(axes[0]))
        put(0x5D6E48, 0)
        put(0x5D6E5C, 0)
        return axes

    # One grouped case per stock type: both absent-collision status profiles,
    # all signed/equality map edges. This wrapper reads bit100000, not bit4.
    for uid, (left, up, right, down) in enumerate(extents):
        points = ((left - 1, 512), (left, 512), (1023 - right, 512), (1024 - right, 512),
                  (512, up - 1), (512, up), (512, 1023 - down), (512, 1024 - down), (-1, -1))
        runs, results = [], []
        for status in (0, 4):
            setup([unit(1, uid, status=status), unit(2, point=(800, 800))])
            for x, y in points:
                put(0x543280, x, 2)
                put(0x543284, y, 2)
                expected = int(x < left or y < up or x + right >= 1024 or y + down >= 1024)
                run = call(0x42D990)
                assert run["returned"] == expected, (uid, status, x, y, expected, run)
                runs.append(run)
                results.append({"status": status, "destination": [x, y], "collision": expected})
        cases.append({"name": "stock_map_bounds_{}".format(uid), "passed": True, "inputs": {"unit_id": uid, "stock_extents": list(extents[uid])},
                      "runs": runs, "observed": {"queries": results}, "oracle": "signed point and stock inclusive unit box inside1024x1024"})
    print(json.dumps({"phase": "stock bounds", "cases": len(cases)}), flush=True)

    for blocker_type, mover_type, bs, ms, bo, mo, relation in itertools.product((0, 35), (0, 35),
            (0, 2, 0x400000, 0x800000, 0x400002, 0x800002, 0xC00000, 0xC00002),
            (0, 2, 0x800000), (0, 82), (0, 84), (0, 1, 2)):
        mover = unit(1, mover_type, status=ms, order=mo)
        blocker = unit(2, blocker_type, status=bs, order=bo, owner=1)
        setup([mover, blocker], relation)
        expected = int(pair_allowed(blocker, mover, relation))
        run = call(0x42EBF0, ecx=POOL + 0x150, edx=POOL)
        assert run["returned"] == expected, (blocker, mover, relation, expected, run)
        cases.append({"name": "pair_{}_{}_{}_{}_{}_{}_{}".format(blocker_type, mover_type, bs, ms, bo, mo, relation),
                      "passed": True, "inputs": {"blocker": blocker, "mover": mover, "mover_to_blocker_relation": relation},
                      "runs": [run], "observed": {"pair_allowed": expected}, "oracle": "native-derived pair policy independently expressed"})
    # Complete hostility conversion for neutral-owned blockers uses sprite owner.
    for sprite_owner, relation in itertools.product((0, 1, 7), (0, 1, 2)):
        mover, blocker = unit(1), unit(2, status=0x400000, owner=11, sprite_owner=sprite_owner)
        setup([mover, blocker], relation)
        run = call(0x42EBF0, ecx=POOL + 0x150, edx=POOL)
        assert run["returned"] == int(relation != 0)
        cases.append({"name": "neutral_blocker_owner_{}_{}".format(sprite_owner, relation), "passed": True,
                      "inputs": {"mover": mover, "blocker": blocker, "relation": relation}, "runs": [run],
                      "observed": {"pair_allowed": run["returned"]}})
    print(json.dumps({"phase": "pair policy", "cases": len(cases)}), flush=True)

    # Actual terrain wrapper composes current source, delta and existing/rebuilt
    # cache. Synthetic walls here; authentic multi-record maps have a separate batch.
    for uid, point, movement in itertools.product((0, 7, 30, 37, 41, 65), ((400, 500), (300, 400), (500, 400)),
            ((0, 0), (0, -100), (100, 0), (-100, 0), (0, 100), (256, 256))):
        setup([unit(1, uid, point=point), unit(2, point=(800, 800))])
        destination = (point[0] + movement[0], point[1] + movement[1])
        put(0x543280, destination[0], 2)
        put(0x543284, destination[1], 2)
        left, up, right, down = extents[uid]
        inner = [up, -right, -down, left]
        bounds = terrain_sweep.collision_bounds(point, inner, edges)
        within = bounds[0] <= destination[0] <= bounds[2] and bounds[1] <= destination[1] <= bounds[3]
        expected = int(not within and not terrain_sweep.sweep_clear(point, destination, inner, edges))
        run = call(0x42D990)
        assert run["returned"] == expected
        assert list(struct.unpack("<4h", uc.mem_read(POOL + 0x108, 8))) == bounds
        cases.append({"name": "terrain_wrapper_{}_{}_{}_{}_{}".format(uid, *point, *movement), "passed": True,
                      "inputs": {"unit_id": uid, "source": list(point), "destination": list(destination)}, "runs": [run],
                      "observed": {"collision": expected, "safe_bounds": bounds}})

    profiles = ("normal", "no_finder", "no_collision", "no_ground_collision", "larva", "enemy_pass",
                "enemy_block", "gathering_pass", "gathering_order", "source_no_collision", "hidden_source", "reverse_ties")
    for uid, direction, delta, profile in itertools.product((0, 7, 30, 37, 41, 65), range(8), (1, 20, 100), profiles):
        source = unit(1, uid)
        movement = ((delta, 0), (0, delta), (-delta, 0), (0, -delta),
                    (delta, delta), (-delta, delta), (-delta, -delta), (delta, -delta))[direction]
        shift = ((30, 0), (0, 30), (-30, 0), (0, -30),
                 (30, 30), (-30, 30), (-30, -30), (30, -30))[direction]
        blocker = unit(2, point=(400 + shift[0], 400 + shift[1]), owner=1)
        other = unit(3, point=tuple(blocker["point"]), owner=2)
        relation = 2
        if profile == "no_finder": blocker["finder_flags"] = 0
        if profile == "no_collision": blocker["status"] |= 0x200000
        if profile == "no_ground_collision": blocker["status"] &= ~0x100000
        if profile == "larva": blocker = unit(2, 35, point=tuple(blocker["point"]), owner=1)
        if profile in ("enemy_pass", "enemy_block"):
            blocker["status"] |= 0x400000
            relation = 0 if profile == "enemy_pass" else 1
        if profile in ("gathering_pass", "gathering_order"):
            source["status"] |= 0x800000
            blocker["status"] |= 0x800000
            blocker["order"] = 82 if profile == "gathering_order" else 0
        if profile == "source_no_collision": source["status"] &= ~0x100000
        if profile == "hidden_source": source["hidden"] = True
        units = [source, blocker, other]
        axes = setup(units, relation, profile == "reverse_ties")
        expected_list = swept_indices(units, axes, movement)
        direct = call(0x4217F0, arguments=movement)
        cursor, observed = direct["returned"], []
        while get(cursor):
            observed.append((get(cursor) - POOL) // 0x150 + 1)
            cursor += 4
            assert len(observed) <= 32
        assert observed == expected_list, (uid, direction, delta, profile, observed, expected_list)
        release = call(0x4222B0)
        assert get(0x5D6E48) == get(0x5D6E5C) == 0
        expected_blocker = 0
        for index in expected_list:
            item = units[index - 1]
            pair_relation = relation if index == 2 else 2
            if (item.get("finder_flags", 1) & 1 and not item["status"] & 0x200000 and
                    pair_allowed(item, source, pair_relation) and item["status"] & 0x100000):
                expected_blocker = POOL + (index - 1) * 0x150
                break
        put(0x543280, 400 + movement[0], 2)
        put(0x543284, 400 + movement[1], 2)
        wrapped = call(0x42D900)
        assert wrapped["returned"] == expected_blocker, (uid, direction, delta, profile, expected_list, expected_blocker, wrapped)
        gate = call(0x42DA30)
        expected_gate = expected_blocker if source["status"] & 0x100000 else 0
        assert gate["returned"] == expected_gate
        assert get(0x5D6E48) == get(0x5D6E5C) == 0
        cases.append({"name": "spatial_{}_{}_{}_{}".format(uid, direction, delta, profile), "passed": True,
            "inputs": {"units": units, "delta": list(movement), "blocker_relation": relation, "supplied_spatial_ties_reversed": profile == "reverse_ties"},
            "runs": [direct, release, wrapped, gate], "observed": {"candidate_indices": observed,
                "blocking_unit_index": 0 if not expected_blocker else (expected_blocker - POOL) // 0x150 + 1,
                "gate_blocking_unit_index": 0 if not expected_gate else (expected_gate - POOL) // 0x150 + 1,
                "query_stack_depth": 0, "query_storage_cursor": 0},
            "oracle": "inclusive swept edge crossings, explicit sorted tie order, pair policy and first eligible candidate"})
    result = {"target_sha256": TARGET_HASH, "version": "1.17.0.1", "probe_script_sha256": assets.file_sha(Path(__file__)),
        "dependency_hashes": {Path(module.__file__).name: assets.file_sha(Path(module.__file__)) for module in (assets, terrain_sweep, counts)},
        "units_dat_sha256": assets.sha(dat_bytes), "pyms_commit": counts.PYMS, "case_count": len(cases),
        "total_native_checks": len(cases), "all_expected_results_matched": True, "failed_cases": 0,
        "complete_function_returns": sum(len(row["runs"]) for row in cases), "fragment_completions": 0,
        "no_helper_stubs": True, "no_import_execution": True, "full_game_or_campaign_executed": False,
        "maximum_instructions_per_execution": 100000,
        "runtime": {"python": platform.python_version(), "architecture": platform.machine(), "unicorn": unicorn.__version__},
        "cases": cases, "limits": "Sorted endpoint tables/unit records are supplied fixtures; insertion timing, natural status production, dynamic movement, route search and full frame order remain separate"}
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({"checks": len(cases), "complete_returns": result["complete_function_returns"]}))


if __name__ == "__main__":
    main()
