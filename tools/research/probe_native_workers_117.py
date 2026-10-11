#!/usr/bin/env python3
"""Plan or run isolated, allowlisted 1.17 worker/town instructions.

All callees executed by these probes are original EXE instructions. No helper
is replaced. Selection-prefix probes stop before issuing an order and are
labeled separately. No Windows process, imported API, DLL or PE entry runs.
"""

import argparse
import hashlib
import json
from pathlib import Path
import platform
import struct

import pefile

TARGET_HASH = "94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0"
FIXTURE = 0x800000
STACK = 0x900000
STOP = 0xA00000
POOL = 0x810000
MAP = 0x820000
REGIONS = 0x80B000
MASK = 0xFFFFFFFF
ALLOW = [
    (0x41FE40, 0x41FE55), (0x424460, 0x4244BE),
    (0x405CF0, 0x405D3D), (0x428D60, 0x428D83),
    (0x43C040, 0x43C084),
    (0x481B10, 0x481B98), (0x481BA0, 0x481BB0),
    (0x483530, 0x48356C), (0x483B70, 0x483BC1),
    (0x484CD0, 0x484D5A), (0x484F05, 0x484F08),
    (0x4852B0, 0x48538E), (0x485400, 0x485479), (0x485689, 0x485692),
    (0x4856A0, 0x4856B0), (0x485753, 0x485755),
    (0x485840, 0x485881), (0x486000, 0x486069),
    (0x486070, 0x486095), (0x4860A0, 0x4860F3), (0x486100, 0x486124),
    (0x486480, 0x4865A8), (0x486AF0, 0x486B9C),
    (0x4AEAA0, 0x4AEAEF), (0x4B38B0, 0x4B38B7),
    (0x482EC0, 0x482ED2), (0x4962C0, 0x4962E4),
    (0x485DE0, 0x485E74), (0x485E80, 0x485EEB), (0x485F20, 0x485F92),
    (0x487580, 0x48772B), (0x496930, 0x4969AF),
    (0x412F10, 0x4130E6), (0x4D0C60, 0x4D0C88), (0x4D0CE5, 0x4D0CF0),
    (0x4976D0, 0x497906),
    (0x412870, 0x412AB5),
]
PLAN = {
    "target_sha256": TARGET_HASH,
    "engine": "Unicorn x86-32; private memory and instruction allowlist",
    "functions": ["8x8 split-region lookup", "building region availability",
                  "worker/AI pool insertion and migration", "racial worker/gas mapping",
                  "worker race acceptance", "construction early rejection",
                  "repair/resume target-selection prefix", "placement throttle rejection",
                  "worker resource-order decision prefix", "same/disconnected-region placement distance",
                  "placement bounds and radius", "placement tile masks without ResourceDepot branch"],
    "stubs": "None; unreviewed paths fail",
    "limits": "No full game, complete construction placement, order issue or repair progression",
    "instruction_bounds": "10000 per ordinary call; 200000 for the 4096-cell radius loop",
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", required=True, type=Path)
    parser.add_argument("--out", type=Path)
    parser.add_argument("--execute-original-code", action="store_true")
    args = parser.parse_args()
    data = args.exe.read_bytes()
    if hashlib.sha256(data).hexdigest() != TARGET_HASH:
        parser.error("Unknown EXE hash: reviewed addresses do not apply")
    if not args.execute_original_code:
        print(json.dumps(PLAN, indent=2))
        return
    if args.out is None:
        parser.error("--out is required for execution evidence")
    import unicorn
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EBX, UC_X86_REG_ECX
    from unicorn.x86_const import UC_X86_REG_EDX, UC_X86_REG_ESP

    pe = pefile.PE(data=data)
    image = pe.get_memory_mapped_image()
    base = pe.OPTIONAL_HEADER.ImageBase
    assert base == 0x400000 and pe.FILE_HEADER.Machine == 0x14C
    image_size = (pe.OPTIONAL_HEADER.SizeOfImage + 4095) & ~4095
    cases = []

    def machine():
        uc = Uc(UC_ARCH_X86, UC_MODE_32)
        uc.mem_map(base, image_size)
        uc.mem_write(base, image)
        uc.mem_map(FIXTURE, 0x80000)
        uc.mem_map(STACK, 0x10000)
        uc.mem_map(STOP, 0x1000)
        return uc

    def put(uc, address, value, size=4):
        uc.mem_write(address, (value & ((1 << (8 * size)) - 1)).to_bytes(size, "little"))

    def get(uc, address, size=4):
        return int.from_bytes(uc.mem_read(address, size), "little")

    def call(uc, start, ecx=0, edx=0, arguments=(), stops=(STOP,), instruction_limit=10000):
        visited = []
        reached = None

        def instruction(vm, address, size, _):
            nonlocal reached
            if address in stops:
                reached = address
                vm.emu_stop()
                return
            if not any(lo <= address and address + size <= hi for lo, hi in ALLOW):
                raise RuntimeError(f"Instruction outside reviewed code: {address:#x}")
            visited.append(address)

        hook = uc.hook_add(UC_HOOK_CODE, instruction)
        sp = STACK + 0xF000
        uc.mem_write(sp, struct.pack("<" + "I" * (len(arguments) + 1), STOP,
                                     *(x & MASK for x in arguments)))
        uc.reg_write(UC_X86_REG_ESP, sp)
        uc.reg_write(UC_X86_REG_ECX, ecx & MASK)
        uc.reg_write(UC_X86_REG_EDX, edx & MASK)
        uc.emu_start(start, STOP + 1, count=instruction_limit)
        uc.hook_del(hook)
        if reached is None:
            raise RuntimeError(f"Probe did not reach its reviewed stop within {instruction_limit} instructions")
        return uc.reg_read(UC_X86_REG_EAX), len(visited), reached

    def unit(uc, addr, unit_id=7, order=1, status=1, owner=0, x=0, y=0):
        sprite = 0x80A000 + ((addr - FIXTURE) // 0x180) * 0x28
        put(uc, addr + 0xC, sprite)
        put(uc, sprite + 0x14, x, 2)
        put(uc, sprite + 0x16, y, 2)
        put(uc, addr + 0x4C, owner, 1)
        put(uc, addr + 0x4D, order, 1)
        put(uc, addr + 0x64, unit_id, 2)
        put(uc, addr + 0xDC, status)
        return sprite

    # Direct cells use 32x32 tiles; split cells select by a 4x4 bitmap of 8x8 cells.
    for x, y in [(0, 0), (31, 31), (32, 0), (63, 33), (8191, 8191), (-1, -1)]:
        uc = machine()
        put(uc, 0x69B584, MAP)
        tx, ty = int(x / 32), int(y / 32)
        put(uc, MAP + 0xC + 2 * (ty * 256 + tx), 177, 2)
        result, count, _ = call(uc, 0x481B10, x, y)
        assert result & 0xFFFF == 177
        cases.append({"kind": "direct_region", "point": [x, y],
                      "observed_region": result & 0xFFFF, "instructions": count})
    for split in [0x2000, 0x2001]:
        for bit in range(16):
            for selected in [0, 1]:
                uc = machine()
                put(uc, 0x69B584, MAP)
                put(uc, MAP + 0xC, split, 2)
                put(uc, MAP + 0x1400C + split * 6, (1 << bit) if selected else 0, 2)
                put(uc, MAP + 0x1400E + split * 6, 101, 2)
                put(uc, MAP + 0x14010 + split * 6, 102, 2)
                x, y = (bit % 4) * 8 + 7, (bit // 4) * 8 + 7
                result, count, _ = call(uc, 0x481B10, x, y)
                assert result & 0xFFFF == 101 + selected
                cases.append({"kind": "split_region", "split_cell": split, "bit": bit,
                              "selected": selected, "point": [x, y],
                              "observed_region": result & 0xFFFF, "instructions": count})

    def approximate_distance(x1, y1, x2, y2):
        a, b = sorted([abs(x1 - x2), abs(y1 - y2)], reverse=True)
        if a // 4 >= b:
            return a
        c = 3 * b // 8
        return a - a // 16 - a // 64 + c + c // 32

    for points, same_region, same_group in [
        ([0, 0, 0, 0], True, True), ([0, 0, 32, 0], True, True),
        ([0, 0, 1024, 768], True, True), ([127, 255, 480, 31], True, True),
        ([0, 0, 32, 0], False, False), ([0, 0, 1024, 768], False, False),
    ]:
        uc = machine()
        put(uc, 0x69B584, MAP)
        x1, y1, x2, y2 = points
        put(uc, MAP + 0xC + 2 * ((y1 // 32) * 256 + x1 // 32), 7, 2)
        put(uc, MAP + 0xC + 2 * ((y2 // 32) * 256 + x2 // 32), 7 if same_region else 8, 2)
        put(uc, MAP + 0x449FE + 7 * 64, 10, 2)
        put(uc, MAP + 0x449FE + 8 * 64, 10 if same_group else 11, 2)
        result, count, _ = call(uc, 0x496930, x1, y1, [x2, y2])
        assert result == (approximate_distance(*points) // 32 if same_region else 9999)
        cases.append({"kind": "placement_path_distance", "points": points, "same_region": same_region,
                      "same_connectivity_group": same_group, "observed": result, "instructions": count,
                      "limits": "Connected distinct-region path search not entered"})

    for absent, inactive, state, first, second, flags in [
        (1, 0, 0, 0, 0, 0), (0, 1, 0, 0, 0, 0), (0, 0, 0, 0, 0, 0),
        (0, 0, 3, 0, 0, 0), (0, 0, 2, 0, 0, 0), (0, 0, 0, 1, 0, 0),
        (0, 0, 0, 0, 1, 0), (0, 0, 0, 0, 0, 0x20), (0, 0, 0, 0, 0, 0xDF),
    ]:
        uc = machine()
        addr = 0x801000
        unit(uc, addr, 106, order=0 if inactive else 1, owner=2)
        put(uc, 0x69B584, MAP)
        put(uc, MAP + 0xC, 7, 2)
        put(uc, 0x6B1A74 + 2 * 4, REGIONS)
        region = REGIONS + 7 * 0x34
        put(uc, region + 5, state, 1)
        put(uc, region + 0x1C, first)
        put(uc, region + 0x20, second)
        put(uc, region + 8, flags, 1)
        result, count, _ = call(uc, 0x4AEAA0, 0 if absent else addr)
        expected = int(not (absent or inactive or state == 3 or first or second or flags & 0x20))
        assert result == expected
        cases.append({"kind": "building_region_available", "input": [absent, inactive, state, first, second, flags],
                      "observed": result, "instructions": count})

    for race in [0, 1, 2, 3, 255]:
        uc = machine()
        put(uc, 0x6726D1 + 3 * 36, race, 1)
        worker, n, _ = call(uc, 0x486070, 3)
        gas, m, _ = call(uc, 0x486100, 3)
        assert [worker, gas] == ([41, 149] if race == 0 else [7, 110] if race == 1 else [64, 157])
        cases.append({"kind": "race_mapping", "race": race, "observed": [worker, gas], "instructions": n + m})
    for requested_flags in [0, 1, 2, 3, 4, 7]:
        for worker_flags in [0, 1, 2, 4, 7]:
            uc = machine()
            addr = 0x801000
            unit(uc, addr)
            put(uc, 0x523C40 + 7, worker_flags, 1)
            put(uc, 0x523C40 + 106, requested_flags, 1)
            put(uc, 0x6BE654, 0x55555555)
            result, count, _ = call(uc, 0x4860A0, addr, 106)
            wanted = 2 if requested_flags & 2 else 1 if requested_flags & 1 else 4
            accepted = int(bool(worker_flags & wanted))
            assert result == accepted
            assert get(uc, 0x6BE654) == (0x55555555 if accepted else 25)
            cases.append({"kind": "race_acceptance", "flags": [requested_flags, worker_flags],
                          "observed": [result, get(uc, 0x6BE654)], "instructions": count})

    for old_count, free in [(0, True), (1, True), (255, True), (0, False)]:
        uc = machine()
        town, worker, ai = FIXTURE, 0x801000, 0x805000
        unit(uc, worker)
        put(uc, 0x521660 + 7 * 4, 8)
        put(uc, town + 8, POOL)
        put(uc, town + 0x1A, old_count, 1)
        put(uc, POOL + 0x5DC0, ai if free else 0)
        put(uc, 0x5349D8, 1234)
        put(uc, worker + 0xC8, 0x55555555)
        before = bytes(uc.mem_read(town, 0x1CC))
        _, count, _ = call(uc, 0x484CD0, town, worker)
        if free:
            observed = [get(uc, town + 0xC), get(uc, town + 0x1A, 1), get(uc, worker + 0x134),
                        get(uc, ai + 8, 2), get(uc, ai + 0xA, 2), get(uc, ai + 0xC),
                        get(uc, ai + 0x10), get(uc, ai + 0x14), get(uc, worker + 0xC8)]
            assert observed == [ai, (old_count + 1) & 255, ai, 0x102, 0, 1234, worker, town, 0]
        else:
            assert bytes(uc.mem_read(town, 0x1CC)) == before and get(uc, worker + 0x134) == 0
            observed = [get(uc, town + 0xC), get(uc, town + 0x1A, 1)]
        cases.append({"kind": "worker_ai_add", "old_count": old_count, "free_slot": free,
                      "observed": observed, "instructions": count})

    for unit_id, status, properties, morph_id in [
        (106, 3, 0x1001, 0), (106, 1, 0x1001, 0), (106, 2, 0x1001, 0),
        (106, 3, 1, 0), (132, 2, 0x1001, 0), (133, 2, 0x1001, 0),
        (131, 2, 0x1001, 132), (131, 2, 0x1001, 133),
        (131, 2, 0x1001, 137), (131, 2, 0x1001, 144), (131, 2, 0x1001, 146),
        (131, 2, 0x1001, 131), (131, 2, 0x1001, 0), (131, 0, 0x1001, 132),
    ]:
        uc = machine()
        addr = 0x806000
        unit(uc, addr, unit_id, status=status)
        put(uc, 0x521660 + unit_id * 4, properties)
        put(uc, addr + 0x98, morph_id, 2)
        result, count, _ = call(uc, 0x424460, addr)
        eligible = bool(status & 1 or unit_id in [132, 133] or
                        unit_id == 131 and morph_id in [132, 133, 137, 144, 146])
        assert result == int(eligible and bool(status & 2) and bool(properties & 0x1000))
        cases.append({"kind": "town_main_building_available", "unit_id": unit_id,
                      "status": status, "properties": properties, "morph_id": morph_id,
                      "observed": result, "instructions": count})

    for resource_property in [False, True]:
        uc = machine()
        addr = 0x806000
        unit(uc, addr, 176)
        put(uc, 0x521660 + 176 * 4, 0x2000 if resource_property else 0)
        for player in range(8):
            town = FIXTURE + player * 0x300
            put(uc, 0x69B51C + player * 8, town)
            for offset in [0x2C, 0x30, 0x34, 0x38]:
                put(uc, town + offset, addr if offset == [0x2C, 0x30, 0x34, 0x38][player % 4] else 0x77777777)
            put(uc, town + 0x1C, player % 2, 1)
        _, count, _ = call(uc, 0x486000, addr)
        observed = []
        for player in range(8):
            town = FIXTURE + player * 0x300
            refs = [get(uc, town + offset) for offset in [0x2C, 0x30, 0x34, 0x38]]
            dirty = get(uc, town + 0x1D, 1)
            expected = [0 if resource_property and i == player % 4 else
                        addr if i == player % 4 else 0x77777777 for i in range(4)]
            assert refs == expected and dirty == (player % 2 if resource_property else 0)
            observed.append({"player": player, "references": refs, "refresh": dirty})
        cases.append({"kind": "clear_resource_references", "resource_property": resource_property,
                      "observed": observed, "instructions": count})

    def migration_fixture(uc, orders, recipient_count=1, recipient_limit=10, donor_limit=1):
        recipient, donor = FIXTURE, FIXTURE + 0x300
        put(uc, recipient, donor)
        put(uc, donor + 4, recipient)
        put(uc, 0x69B51C, recipient)
        for town, count, limit in [(recipient, recipient_count, recipient_limit), (donor, len(orders), donor_limit)]:
            put(uc, town + 8, POOL)
            put(uc, town + 0x1A, count, 1)
            put(uc, town + 0x1B, limit, 1)
        building, building_ai = 0x806000, 0x808000
        unit(uc, building, 106, status=3)
        put(uc, 0x521660 + 106 * 4, 0x1001)
        put(uc, donor + 0x24, building)
        put(uc, donor + 0x14, building_ai)
        put(uc, building_ai + 0x10, building)
        put(uc, 0x521660 + 7 * 4, 8)
        workers = []
        for i, order in enumerate(orders):
            worker, ai = 0x801000 + i * 0x180, 0x805000 + i * 0x20
            workers.append(worker)
            unit(uc, worker, order=order)
            put(uc, worker + 0x134, ai)
            put(uc, ai, 0x805000 + (i + 1) * 0x20 if i + 1 < len(orders) else 0)
            put(uc, ai + 4, 0x805000 + (i - 1) * 0x20 if i else 0)
            put(uc, ai + 0x10, worker)
            put(uc, ai + 0x14, donor)
        put(uc, donor + 0xC, 0x805000 if orders else 0)
        return recipient, donor, workers

    excluded = [0x53, 0x57, 0x58, 0x1E, 0x21, 0x1F, 0x46, 0x1A, 0x19]
    variants = [("normal", [1, 1, 1], 1, 10, 1, 1),
                ("recipient_clamp", [1, 1, 1], 1, 2, 1, 5),
                ("recipient_full", [1, 1, 1], 2, 2, 1, 1),
                ("empty_recipient_bypasses_limit", [1, 1, 1], 0, 0, 3, 1),
                ("donor_no_excess", [1, 1, 1], 1, 10, 3, 1),
                ("donor_single", [1], 1, 10, 0, 1),
                ("zero_requested", [1, 1, 1], 1, 10, 1, 0),
                ("drains_selected_donor", [1, 1, 1], 0, 0, 3, 5)]
    variants += [(f"excluded_{order:02x}", [order, 1, 1], 1, 10, 1, 1) for order in excluded]
    variants += [("all_excluded", [0x53, 0x57, 0x58], 1, 10, 1, 2),
                 ("recipient_in_battle", [1, 1, 1], 1, 10, 1, 1)]
    for name, orders, r_count, r_limit, d_limit, wanted in variants:
        uc = machine()
        recipient, donor, workers = migration_fixture(uc, orders, r_count, r_limit, d_limit)
        if name == "recipient_in_battle":
            put(uc, recipient + 0x1E, 1, 1)
        _, count, _ = call(uc, 0x486480, recipient, wanted)
        eligible = [w for w, order in zip(workers, orders) if order not in excluded]
        allowed = (name != "recipient_in_battle" and wanted != 0 and
                   (r_count == 0 or r_count < r_limit) and len(workers) > 1 and
                   (r_count == 0 or len(workers) > d_limit))
        n = min(wanted if r_count == 0 else min(wanted, r_limit - r_count), len(eligible)) if allowed else 0
        moved = [w for w in workers if get(uc, get(uc, w + 0x134) + 0x14) == recipient]
        assert moved == eligible[:n]
        assert [get(uc, recipient + 0x1A, 1), get(uc, donor + 0x1A, 1)] == [r_count + n, len(workers) - n]
        head = get(uc, recipient + 0xC)
        reverse = []
        while head:
            reverse.append(get(uc, head + 0x10))
            head = get(uc, head)
        assert reverse == moved[::-1]
        cases.append({"kind": "worker_migration", "name": name,
                      "orders": orders, "counts_limits_request": [r_count, r_limit, d_limit, wanted],
                      "moved_indices": [workers.index(w) for w in moved],
                      "recipient_list_indices": [workers.index(w) for w in reverse],
                      "final_counts": [r_count + n, len(workers) - n], "instructions": count})

    uc = machine()
    recipient, donor, workers = migration_fixture(uc, [1, 1, 1], recipient_count=0)
    last = FIXTURE + 0x600
    put(uc, donor, last)
    put(uc, last + 4, donor)
    put(uc, last + 8, POOL)
    put(uc, last + 0x1A, 2, 1)
    put(uc, last + 0x1B, 1, 1)
    building, building_ai = 0x806600, 0x808600
    unit(uc, building, 106, status=3)
    put(uc, last + 0x24, building)
    put(uc, last + 0x14, building_ai)
    put(uc, building_ai + 0x10, building)
    last_workers = [0x803000, 0x803180]
    for i, worker in enumerate(last_workers):
        ai = 0x805200 + i * 0x20
        unit(uc, worker)
        put(uc, worker + 0x134, ai)
        put(uc, ai, 0x805220 if i == 0 else 0)
        put(uc, ai + 4, 0x805200 if i else 0)
        put(uc, ai + 0x10, worker)
        put(uc, ai + 0x14, last)
    put(uc, last + 0xC, 0x805200)
    _, count, _ = call(uc, 0x486480, recipient, 2)
    assert all(get(uc, get(uc, w + 0x134) + 0x14) == recipient for w in last_workers)
    assert all(get(uc, get(uc, w + 0x134) + 0x14) == donor for w in workers)
    assert get(uc, last + 0x1A, 1) == 0
    cases.append({"kind": "worker_migration", "name": "town_tail_first",
                  "observed": "both workers moved from the final town before the earlier donor was considered",
                  "instructions": count})

    selections = [
        ("largest_absolute_deficit", [(1000, 900, 1, 0), (200, 50, 1, 0)], 1),
        ("equal_deficit_first", [(200, 100, 1, 0), (1000, 900, 1, 0)], 0),
        ("incomplete_overrides_damage", [(200, 50, 1, 0), (1000, 900, 0, 0)], 1),
        ("first_incomplete", [(200, 50, 0, 0), (1000, 0, 0, 0)], 0),
        ("occupied_target_skipped", [(200, 0, 1, 1), (1000, 950, 1, 0)], 1),
        ("hp_ceiling_tie", [(200, 100 + 1 / 256, 1, 0), (200, 101, 1, 0)], 0),
        ("max_hp_zero_not_deficient", [(0, 1, 1, 0), (200, 150, 1, 0)], 1),
    ]
    for name, values, expected_index in selections:
        uc = machine()
        town, worker = FIXTURE, 0x801000
        unit(uc, worker)
        put(uc, 0x69B584, MAP)
        put(uc, 0x6B1A74, REGIONS)
        put(uc, town + 0x14, 0x808000)
        targets = []
        for i, (maximum, current, completed, occupied) in enumerate(values):
            target = 0x806000 + i * 0x180
            targets.append(target)
            ai = 0x808000 + i * 0x30
            unit(uc, target, 106 + i, status=completed)
            put(uc, target + 8, int(current * 256))
            put(uc, target + 0x80, worker if occupied else 0)
            put(uc, 0x520708 + (106 + i) * 4, maximum * 256)
            put(uc, ai, 0x808000 + (i + 1) * 0x30 if i + 1 < len(values) else 0)
            put(uc, ai + 0x10, target)
        _, count, reached = call(uc, 0x4852B0, worker, town, stops=(0x485369, 0x48534E))
        selected = uc.reg_read(UC_X86_REG_EBX)
        assert reached == 0x485369 and selected == targets[expected_index]
        cases.append({"kind": "repair_selection_prefix", "name": name, "targets": values,
                      "selected_index": expected_index, "stop_va": hex(reached), "instructions": count,
                      "limits": "Stops before native order issue and target builder-pointer write"})

    for name, top_kind, top_town, reassignment, unit_id in [
        ("empty_queue", 0, FIXTURE, 0, 106),
        ("other_request_kind", 4, FIXTURE, 0, 106),
        ("wrong_town", 3, FIXTURE + 0x300, 0, 106),
        ("reassign_three", 3, FIXTURE, 3, 106),
        ("reassign_255", 3, FIXTURE, 255, 106),
        ("race_mismatch", 3, FIXTURE, 0, 106),
    ]:
        uc = machine()
        town, worker, ai = FIXTURE, 0x801000, 0x805000
        unit(uc, worker)
        put(uc, worker + 0x134, ai)
        put(uc, ai + 0x14, town)
        put(uc, ai + 0xA, reassignment, 1)
        put(uc, 0x67D068 + 0x210, 0 if name == "empty_queue" else 1, 1)
        put(uc, 0x67D068 + 0x19, top_kind, 1)
        put(uc, 0x67D068 + 0x1A, unit_id, 2)
        put(uc, 0x67D068 + 0x1C, top_town)
        result, count, _ = call(uc, 0x485400, town, worker)
        assert result == 0
        cases.append({"kind": "construction_early_rejection", "name": name,
                      "observed": result, "instructions": count})

    uc = machine()
    town, worker, ai = FIXTURE, 0x801000, 0x805000
    unit(uc, worker)
    put(uc, worker + 0x134, ai)
    put(uc, ai + 0x14, town)
    put(uc, 0x67D068 + 0x210, 1, 1)
    put(uc, 0x67D068 + 0x19, 3, 1)
    put(uc, 0x67D068 + 0x1A, 109, 2)
    put(uc, 0x67D068 + 0x1C, town + 0x300)
    put(uc, 0x523C40 + 7, 2, 1)
    put(uc, 0x523C40 + 109, 2, 1)
    _, count, reached = call(uc, 0x485400, town, worker, stops=(0x485473,))
    assert reached == 0x485473
    cases.append({"kind": "construction_service_prefix", "name": "supply_depot_bypasses_town_mismatch",
                  "stop_va": hex(reached), "instructions": count,
                  "limits": "Stops before requirements, resources, placement and order issue"})

    uc = machine()
    worker = 0x801000
    unit(uc, worker)
    put(uc, 0x67D279, 1, 1)
    result, count, _ = call(uc, 0x4962C0, worker, 106, [0, FIXTURE, 1, 64])
    assert result == 0
    cases.append({"kind": "placement_throttle_rejection", "observed": result, "instructions": count})

    for wait in [1, 2, 10, 255]:
        uc = machine()
        worker, ai = 0x801000, 0x805000
        unit(uc, worker, 41)
        put(uc, worker + 0x134, ai)
        put(uc, ai + 0x14, FIXTURE)
        put(uc, ai + 0xB, wait, 1)
        put(uc, ai + 0xC, 123)
        put(uc, 0x5349D8, 1000)
        _, count, _ = call(uc, 0x487580, worker)
        assert [get(uc, ai + 0xB, 1), get(uc, ai + 0xC)] == [wait - 1, 123]
        cases.append({"kind": "worker_ai_wait", "old_wait": wait,
                      "observed": [wait - 1, 123], "instructions": count})

    for code in [2, 3, 4, 5]:
        for amounts in [[200, 300, 400], [200, 0, 400], [0, 0, 0], [None, 300, None], [None, None, None]]:
            uc = machine()
            town, worker, ai = FIXTURE, 0x801000, 0x805000
            unit(uc, worker, 41)
            put(uc, 0x523C40 + 41, 1, 1)
            put(uc, worker + 0x134, ai)
            put(uc, ai + 9, code, 1)
            put(uc, ai + 0x14, town)
            put(uc, 0x5349D8, 1234)
            main = 0x806000
            unit(uc, main, 131, status=3)
            put(uc, 0x521660 + 131 * 4, 0x1001)
            put(uc, town + 0x24, main)
            mineral = 0x806180
            unit(uc, mineral, 176)
            put(uc, town + 0x2C, mineral)
            gases = []
            for i, amount in enumerate(amounts):
                gas = 0x806300 + i * 0x180
                gases.append(gas)
                if amount is not None:
                    unit(uc, gas, 149)
                    put(uc, gas + 0xD0, amount, 2)
                    put(uc, town + 0x30 + i * 4, gas)
            _, count, reached = call(uc, 0x487580, worker, stops=(0x42A380,))
            nth = code - 1 if code in [2, 3] else 3
            nonempty = [g for g, amount in zip(gases, amounts) if amount is not None and amount > 0]
            depleted = [g for g, amount in zip(gases, amounts) if amount == 0]
            chosen = nonempty[nth - 1] if len(nonempty) >= nth else (nonempty + depleted + [mineral])[0]
            sp = uc.reg_read(UC_X86_REG_ESP)
            assert reached == 0x42A380 and [get(uc, sp + 4), get(uc, sp + 8)] == [0x4F, chosen]
            assert get(uc, ai + 0xC) == 1234
            cases.append({"kind": "worker_resource_decision_prefix", "code": code,
                          "gas_amounts": amounts, "chosen_slot": gases.index(chosen) if chosen in gases else "mineral",
                          "order": 0x4F, "stop_va": hex(reached), "instructions": count,
                          "limits": "Stops at native order helper entry; order execution/harvesting not observed"})

    for x, y, width, height, accepted in [
        (-1, 0, 64, 96, False), (0, -1, 64, 96, False),
        (62, 61, 64, 96, True), (63, 61, 64, 96, False), (62, 62, 64, 96, False),
        (64, 0, 0, 32, True), (65, 0, 0, 32, False),
        (0, 64, 32, 0, True), (0, 65, 32, 0, False),
    ]:
        uc = machine()
        put(uc, 0x5264B4, 64, 2)
        put(uc, 0x5264B6, 64, 2)
        put(uc, 0x522728 + 106 * 4, width, 2)
        put(uc, 0x52272A + 106 * 4, height, 2)
        put(uc, 0x6D319C, 2)
        uc.mem_write(0x675188 - 1, b"\xAA" * 50)
        result, count, reached = call(uc, 0x412F10, 0, 0, [x, y, 106, 0, 0, 1, 0],
                                      stops=(0x412FB3,) if accepted else (STOP,))
        if accepted:
            assert reached == 0x412FB3 and bytes(uc.mem_read(0x675188, 48)) == b"\xAA" * 48
        else:
            assert result == 3 and bytes(uc.mem_read(0x675188, 48)) == b"\x03" * 48
        assert get(uc, 0x675188 - 1, 1) == 0xAA and get(uc, 0x675188 + 48, 1) == 0xAA
        cases.append({"kind": "placement_bounds", "tile": [x, y], "box_pixels": [width, height],
                      "bounds_accepted": accepted, "stop_va": hex(reached), "instructions": count,
                      "limits": "Accepted cases stop before reachability, terrain, psi, creep and occupancy"})

    cells = [(32, 32), (33, 32), (34, 32), (0, 32), (31, 31), (0, 0), (63, 63)]
    for radius in [0, 1, 2, 31, 32, 0xFFFFFFFF]:
        uc = machine()
        put(uc, 0x69B584, MAP)
        uc.mem_write(MAP + 0xC, struct.pack("<H", 7) * 65536)
        grid = 0x80C000
        expected = bytearray(4096)
        for i, (column, row) in enumerate(cells):
            put(uc, grid + row * 64 + column, i + 1, 1)
            distance = approximate_distance(2048, 2048, 1024 + column * 32, 1024 + row * 32) // 32
            expected[row * 64 + column] = i + 1 if distance <= radius else 0
        _, count, _ = call(uc, 0x4976D0, grid, radius, [2048 | (2048 << 16)], instruction_limit=200000)
        observed = bytes(uc.mem_read(grid, 4096))
        assert observed == expected
        cases.append({"kind": "placement_radius_filter", "radius_tiles": radius,
                      "retained_cell_indices": [i for i, (column, row) in enumerate(cells) if observed[row * 64 + column]],
                      "instructions": count, "limits": "Same-region candidate paths; connected-region search not entered"})

    # Full tile-mask leaf with ResourceDepot property clear: resource-neighborhood
    # search is a separate branch, rather than a substituted helper result.
    player = 3
    for unit_id, group_flags in [(109, 2), (142, 1), (194, 2), (199, 0), (200, 0)]:
        for player_type in [1, 2]:
            for check_vision in [0, 1]:
                for flags in [0, 0x800000, 0x20000000, 0x400000, 0x40000000,
                              0x80800000, 1 << player, 1 << (player + 8)]:
                    uc = machine()
                    put(uc, 0x5264B4, 64, 2)
                    put(uc, 0x666454, MAP)
                    put(uc, 0x6726D0 + player * 36, player_type, 1)
                    put(uc, 0x523C40 + unit_id, group_flags, 1)
                    put(uc, 0x521660 + unit_id * 4, 0)
                    tiles = [flags, 0, 0x20000000, flags | (1 << player)]
                    for i, tile_flags in enumerate(tiles):
                        put(uc, MAP + 4 * ((40 + i // 2) * 64 + 40 + i % 2), tile_flags)
                    uc.mem_write(0x675188 - 1, b"\xAA" * 50)
                    result, count, _ = call(uc, 0x412870, unit_id, 40,
                                            [40, player, 2 | (2 << 16), 0, check_vision])
                    mask = 0x20800000
                    if not group_flags & 1:
                        mask |= 0x40000000 if check_vision else 0x400000
                    expected_codes = []
                    for tile_flags in tiles:
                        if player_type == 2 and tile_flags & (1 << (player + 8)):
                            code = 10
                        elif (not check_vision and player_type == 2 and tile_flags & (1 << player)):
                            code = 0
                        elif tile_flags & mask and not (194 <= unit_id <= 199 and tile_flags & 0x80000000):
                            code = 4
                        else:
                            code = 0
                        expected_codes.append(code)
                    observed = [get(uc, 0x675188 + i, 1) for i in [0, 1, 6, 7]]
                    assert observed == expected_codes and result == max(expected_codes)
                    unchanged = [i for i in range(48) if i not in [0, 1, 6, 7]]
                    assert all(get(uc, 0x675188 + i, 1) == 0xAA for i in unchanged)
                    assert get(uc, 0x675188 - 1, 1) == 0xAA and get(uc, 0x675188 + 48, 1) == 0xAA
                    cases.append({"kind": "placement_tile_flags", "unit_id": unit_id, "group_flags": group_flags,
                                  "player_type": player_type, "check_vision": check_vision, "tiles": tiles,
                                  "observed_cell_codes": observed, "return": result, "instructions": count,
                                  "limits": "ResourceDepot branch excluded; does not include creep/psi/occupancy or flag generation"})

    result = {
        "target_sha256": TARGET_HASH, "version": "1.17.0.1",
        "evidence": "isolated original x86 instructions on explicit synthetic private state",
        "original_game_launched": False, "guest_imports_executed": False, "helper_stubs": False,
        "probe_script_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        "runtime": {"unicorn": unicorn.__version__, "pefile": pefile.__version__,
                    "python": platform.python_version(), "machine": platform.machine()},
        "plan": PLAN, "case_count": len(cases), "passed": len(cases), "cases": cases,
    }
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({"case_count": len(cases), "passed": len(cases), "output": str(args.out)}))


if __name__ == "__main__":
    main()
