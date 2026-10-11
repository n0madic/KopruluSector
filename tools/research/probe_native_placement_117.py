#!/usr/bin/env python3
"""Inspect or execute known-hash 1.17 placement predicates in private memory.

The bounded guest runs original instructions and original callees only. No
Windows import, entry point, helper replacement or host service is provided.
Synthetic tile flags, spatial indexes and power masks are explicit inputs;
this does not establish their generation or complete construction orders.
"""

import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import platform
import struct

import capstone
import pefile

TARGET_HASH = "94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0"
FIXTURE, TILES, POWER = 0x800000, 0x810000, 0x820000
STACK, STOP = 0x900000, 0xA00000
MASK = 0xFFFFFFFF
BLOCKS = {
    "builder_overlaps_tile": (0x4122D0, 0x412353),
    "unit_occupancy_grid": (0x412360, 0x412558),
    "nydus_tile_grid": (0x412720, 0x412870),
    "ordinary_tile_grid": (0x412870, 0x412AB5),
    "creep_tile_grid": (0x412AC0, 0x412C0C),
    "shared_placement_validator": (0x412F10, 0x4130E6),
    "power_placement_code": (0x4130F0, 0x413147),
    "tile_occupancy_predicate": (0x413390, 0x41349C),
    "sorted_spatial_bound": (0x419500, 0x419555),
    "spatial_query_release": (0x4222B0, 0x4222C8),
    "spatial_rectangle_query": (0x422330, 0x422454),
    "gas_building_predicate": (0x429320, 0x429353),
    "power_predicate": (0x430000, 0x4300D5),
    "power_frame_pointer": (0x40F670, 0x40F679),
    "grp_opaque_sample": (0x40FD50, 0x40FDBA),
    "power_mask_initializer": (0x42FE60, 0x42FFB4),
    "native_memset": (0x4D0C60, 0x4D0CF0),
    "footprint_player_high_bit": (0x4134F0, 0x41357F),
    "footprint_player_low_bit": (0x413580, 0x41360D),
    "gas_placement": (0x413610, 0x413743),
    "live_geyser_at_point": (0x4132B0, 0x41336F),
    "resource_container_predicate": (0x413370, 0x413382),
    "fog_sprite_type_at_point": (0x418000, 0x418093),
    "fog_resource_neighborhood": (0x4180A0, 0x418198),
    "invalid_fog_unit_type": (0x4BC730, 0x4BC754),
    "spatial_point_query": (0x4221F0, 0x4222A6),
    "first_filtered_border_query": (0x442B60, 0x442BBA),
    "builder_reachability_gate": (0x48E980, 0x48E9A5),
    "point_connectivity_group": (0x481D00, 0x481D50),
    "region_at_pixel": (0x481B10, 0x481B98),
}
ALLOW = list(BLOCKS.values())


def main():
    probe_script_hash = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", required=True, type=Path)
    parser.add_argument("--out", type=Path)
    parser.add_argument("--static-out", type=Path)
    parser.add_argument("--power-grp", type=Path)
    parser.add_argument("--execute-original-code", action="store_true")
    args = parser.parse_args()
    data = args.exe.read_bytes()
    if hashlib.sha256(data).hexdigest() != TARGET_HASH:
        parser.error("Unknown EXE hash: reviewed addresses do not apply")
    pe = pefile.PE(data=data)
    base = pe.OPTIONAL_HEADER.ImageBase
    assert base == 0x400000 and pe.FILE_HEADER.Machine == 0x14C
    if args.static_out:
        md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
        witnesses = {}
        for name, (lo, hi) in BLOCKS.items():
            offset = pe.get_offset_from_rva(lo - base)
            rows = list(md.disasm(data[offset:offset + hi - lo], lo))
            if not rows or rows[-1].address + rows[-1].size != hi:
                parser.error(f"Invalid instruction boundary: {name}")
            witnesses[name] = [{"va": hex(i.address), "bytes": i.bytes.hex(),
                                "mnemonic": i.mnemonic, "operands": i.op_str} for i in rows]
        args.static_out.parent.mkdir(parents=True, exist_ok=True)
        args.static_out.write_text(json.dumps({"target_sha256": TARGET_HASH,
            "version": "1.17.0.1", "evidence": "static only; analytical labels",
            "witnesses": witnesses}, indent=2) + "\n")
    if not args.execute_original_code:
        print(json.dumps({"target_sha256": TARGET_HASH, "reviewed_blocks": list(BLOCKS),
                          "plan": "Isolated original placement predicates; no stubs/imports"}))
        return
    if args.out is None:
        parser.error("--out required for execution evidence")
    import unicorn
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_ESP, UC_X86_REG_EBP

    image = pe.get_memory_mapped_image()
    image_size = (pe.OPTIONAL_HEADER.SizeOfImage + 4095) & ~4095
    cases = []

    def machine():
        uc = Uc(UC_ARCH_X86, UC_MODE_32)
        uc.mem_map(base, image_size)
        uc.mem_write(base, image)
        uc.mem_map(FIXTURE, 0x80000)
        uc.mem_map(STACK, 0x10000)
        uc.mem_map(STOP, 0x1000)
        # The reviewed memset path uses REP STOSB, not an imported helper.
        put(uc, 0x6D319C, 2)
        return uc

    def put(uc, address, value, size=4):
        uc.mem_write(address, (value & ((1 << (size * 8)) - 1)).to_bytes(size, "little"))

    def get(uc, address, size=4):
        return int.from_bytes(uc.mem_read(address, size), "little")

    def call(uc, start, ecx=0, edx=0, arguments=(), fragment_ebp=None, eax=0):
        count, reached = 0, False

        def instruction(vm, address, size, _):
            nonlocal count, reached
            if address == STOP:
                reached = True
                vm.emu_stop()
                return
            if not any(lo <= address and address + size <= hi for lo, hi in ALLOW):
                raise RuntimeError(f"Unreviewed instruction {address:#x}")
            count += 1

        hook = uc.hook_add(UC_HOOK_CODE, instruction)
        sp = STACK + 0xF000 if fragment_ebp is None else fragment_ebp - 0x18
        if fragment_ebp is None:
            uc.mem_write(sp, struct.pack("<" + "I" * (1 + len(arguments)), STOP,
                                         *(v & MASK for v in arguments)))
        uc.reg_write(UC_X86_REG_ESP, sp)
        uc.reg_write(UC_X86_REG_ECX, ecx & MASK)
        uc.reg_write(UC_X86_REG_EDX, edx & MASK)
        uc.reg_write(UC_X86_REG_EAX, eax & MASK)
        if fragment_ebp is not None:
            uc.reg_write(UC_X86_REG_EBP, fragment_ebp)
        uc.emu_start(start, STOP + 1, count=10000)
        uc.hook_del(hook)
        if not reached:
            raise RuntimeError("Original call did not return within 10000 instructions")
        return uc.reg_read(UC_X86_REG_EAX), count

    def record(kind, inputs, observed, expected, instructions):
        if observed != expected:
            raise AssertionError((kind, inputs, observed, expected))
        cases.append({"kind": kind, "inputs": inputs, "observed": observed,
                      "expected": expected, "instructions": instructions,
                      "evidence": "complete original function and actual called functions"})

    def tile_fixture(uc, player, controller, flags, entry=2):
        put(uc, 0x666454, TILES)
        put(uc, 0x5264B4, 8, 2)
        put(uc, 0x5264B6, 8, 2)
        put(uc, 0x6726D0 + player * 36, controller, 1)
        for row in range(2):
            for col in range(2):
                put(uc, TILES + 4 * ((row + 1) * 8 + col + 1), flags)
        output = 0x675188 + entry * 48
        uc.mem_write(output - 1, bytes([0xA7]) * 50)
        return output

    for player in [0, 1, 7, 11]:
        for controller in [1, 2]:
            for vision in [0, 1]:
                for ignore_visibility in [0, 1]:
                    bits = [0, 0x00400000, 0x40000000, 0x40400000,
                            1 << player, 1 << (player + 8),
                            (1 << player) | (1 << (player + 8)),
                            0x00400000 | (1 << (player + 8)),
                            0x40000000 | (1 << (player + 8))]
                    for flags in bits:
                        uc = machine()
                        output = tile_fixture(uc, player, controller, flags)
                        result, n = call(uc, 0x412AC0, 1, 1,
                                         [player, (2 << 16) | 2, 2, vision, ignore_visibility])
                        cell = maximum = 10 if controller == 2 and flags & (1 << (player + 8)) else 0
                        bypass = not ignore_visibility and not vision and controller == 2 and flags & (1 << player)
                        if not bypass and not flags & (0x40000000 if vision else 0x00400000):
                            cell = 5
                            maximum = max(maximum, 5)
                        observed = list(uc.mem_read(output, 48))
                        expected = [0xA7] * 48
                        for pos in [0, 1, 6, 7]:
                            expected[pos] = cell
                        assert observed == expected and get(uc, output - 1, 1) == 0xA7 and get(uc, output + 48, 1) == 0xA7
                        record("creep_grid", [player, controller, vision, ignore_visibility, flags],
                               [result, cell], [maximum, cell], n)
                    if ignore_visibility:
                        continue
                    for flags in bits:
                        uc = machine()
                        output = tile_fixture(uc, player, controller, flags)
                        result, n = call(uc, 0x412720, 1, 1, [player, (2 << 16) | 2, 2, vision])
                        if controller == 2 and flags & (1 << (player + 8)):
                            cell = 10
                        elif controller == 2 and vision and flags & (1 << player):
                            cell = 9
                        elif flags & (0x40000000 if vision else 0x00400000):
                            cell = 0
                        else:
                            cell = 5
                        expected = [0xA7] * 48
                        for pos in [0, 1, 6, 7]:
                            expected[pos] = cell
                        assert list(uc.mem_read(output, 48)) == expected
                        record("nydus_grid", [player, controller, vision, flags], [result, cell], [cell, cell], n)

    # The power predicate uses a synthetic mask and explicit half-extents.
    # Every addressable cell is toggled independently; mask generation is separate.
    power_unit, power_sprite = FIXTURE + 0x1000, FIXTURE + 0x2000
    for row in range(6):
        for col in range(8):
            for enabled in [0, 1]:
                uc = machine()
                put(uc, 0x521660 + 160 * 4, 0x80000)
                put(uc, 0x665B50, POWER)
                put(uc, 0x665B54, power_unit)
                put(uc, 0x665B58, 128)
                put(uc, 0x665B5C, 96)
                put(uc, power_unit + 0xC, power_sprite)
                put(uc, power_unit + 0x4C, 3, 1)
                put(uc, power_unit + 0xFC, 0)
                put(uc, power_sprite + 0x14, 512, 2)
                put(uc, power_sprite + 0x16, 512, 2)
                put(uc, POWER + row * 8 + col, enabled, 1)
                point = [512 - 128 + col * 32 + 1, 512 - 96 + row * 32 + 1]
                result, n = call(uc, 0x430000, 160, point[0], [point[1], 3])
                record("power_mask_cell", [row, col, enabled, point], result, enabled, n)
    for dx, dy, owner, required, present in [
        (-128, 0, 3, 1, 1), (128, 0, 3, 1, 1), (0, -96, 3, 1, 1), (0, 96, 3, 1, 1),
        (-127, -95, 3, 1, 1), (127, 95, 3, 1, 1), (0, 0, 4, 1, 1),
        (0, 0, 3, 1, 0), (0, 0, 3, 0, 0),
    ]:
        uc = machine()
        put(uc, 0x521660 + 160 * 4, 0x80000 if required else 0)
        put(uc, 0x665B50, POWER)
        uc.mem_write(POWER, bytes([1]) * 48)
        put(uc, 0x665B54, power_unit if present else 0)
        put(uc, 0x665B58, 128)
        put(uc, 0x665B5C, 96)
        put(uc, power_unit + 0xC, power_sprite)
        put(uc, power_unit + 0x4C, owner, 1)
        put(uc, power_unit + 0xFC, 0)
        put(uc, power_sprite + 0x14, 512, 2)
        put(uc, power_sprite + 0x16, 512, 2)
        result, n = call(uc, 0x430000, 160, 512 + dx, [512 + dy, 3])
        expected = int(not required or (present and owner == 3 and abs(dx) < 128 and abs(dy) < 96))
        record("power_bounds", [dx, dy, owner, required, present], result, expected, n)

    coordinates = [-32, 0, 0, 31, 32, 32, 96]
    for values in [[], coordinates]:
        for target in [-33, -32, -1, 0, 1, 31, 32, 33, 96, 97]:
            for flag in [0, 1]:
                uc = machine()
                put(uc, 0x5C99B0, len(values))
                for i, value in enumerate(values):
                    put(uc, 0x5C99B8 + i * 8, i)
                    put(uc, 0x5C99BC + i * 8, value)
                result, n = call(uc, 0x419500, 0x5C99B8, target, [flag])
                expected = sum(v < target if flag else v <= target for v in values)
                record("sorted_spatial_bound", [values, target, flag], result, expected, n)

    def border_fixture(uc, addr, box, unit_id=7, status=0, hidden=0, owner=3, visibility=MASK):
        sprite = FIXTURE + 0x2000 + ((addr // 0x150) % 32) * 0x28
        put(uc, addr + 0xC, sprite)
        put(uc, sprite + 0xE, 0x20 if hidden else 0, 1)
        put(uc, addr + 0x64, unit_id, 2)
        put(uc, addr + 0xDC, status)
        put(uc, addr + 0xE4, visibility)
        put(uc, addr + 0x4C, owner, 1)
        for pos, value, table in [(0x13C, box[0], 0x5C99B8), (0x140, box[2], 0x5C99B8),
                                  (0x144, box[1], 0x5D03F8), (0x148, box[3], 0x5D03F8)]:
            index = 10 + ((addr - FIXTURE) // 0x150) * 2 + (pos in (0x140, 0x148))
            put(uc, addr + pos, index)
            put(uc, table + index * 8 + 4, value)

    builder, candidate, unitlist = FIXTURE + 0x1000, FIXTURE + 0x1300, FIXTURE + 0x7000
    boxes = [[32, 32, 63, 63], [0, 32, 32, 63], [64, 32, 95, 63],
             [32, 0, 63, 32], [32, 64, 63, 95], [31, 31, 33, 33]]
    for box in boxes:
        for status in [0, 2, 4, 6]:
            for hidden in [0, 1]:
                uc = machine()
                border_fixture(uc, builder, box, status=status, hidden=hidden)
                result, n = call(uc, 0x4122D0, builder, 1, [1])
                overlap = box[0] < 64 and box[2] > 32 and box[1] < 64 and box[3] > 32
                expected = int(not (status & 2 and not hidden and overlap))
                record("builder_tile_overlap", [box, status, hidden], result, expected, n)
    for absent, unit_id in [(1, 7), (0, 134)]:
        uc = machine()
        border_fixture(uc, builder, boxes[0], unit_id=unit_id, status=2)
        result, n = call(uc, 0x4122D0, 0 if absent else builder, 1, [1])
        record("builder_tile_exceptions", [absent, unit_id], result, 1, n)

    for status, unit_id, hidden, visible, include_buildings, include_unseen, self_unit, box in [
        (0, 7, 0, 1, 0, 0, 0, boxes[0]), (2, 7, 0, 1, 0, 0, 0, boxes[0]),
        (4, 7, 0, 1, 0, 0, 0, boxes[0]), (0, 105, 0, 1, 0, 0, 0, boxes[0]),
        (0, 202, 0, 1, 0, 0, 0, boxes[0]), (0, 7, 1, 1, 0, 0, 0, boxes[0]),
        (0, 7, 0, 1, 0, 0, 1, boxes[0]), (0x20000, 7, 0, 1, 0, 0, 0, boxes[0]),
        (0x20000, 7, 0, 1, 1, 0, 0, boxes[0]),
        (0x100, 7, 0, 0, 0, 0, 0, boxes[0]), (0x100, 7, 0, 0, 0, 1, 0, boxes[0]),
        (0x200, 7, 0, 0, 0, 1, 0, boxes[0]), (0x300, 7, 0, 1, 0, 1, 0, boxes[0]),
        (0, 7, 0, 1, 0, 0, 0, boxes[1]), (0, 7, 0, 1, 0, 0, 0, boxes[2]),
    ]:
        uc = machine()
        border_fixture(uc, builder, boxes[0], owner=3)
        border_fixture(uc, candidate, box, unit_id, status, hidden, visibility=(1 << 3) if visible else 0)
        put(uc, unitlist, builder if self_unit else candidate)
        put(uc, unitlist + 4, 0)
        result, n = call(uc, 0x413390, builder, unitlist, [1, 1, include_buildings, include_unseen])
        unseen = bool(status & 0x300 and not visible)
        skip = (not include_buildings and status & 0x20000) or self_unit or status & 6 or unit_id in (105, 202) or hidden or (unseen and not include_unseen)
        overlap = box[0] < 64 and box[2] > 32 and box[1] < 64 and box[3] > 32
        expected = 0 if skip or not overlap else 1 if unseen else 4
        record("unit_tile_occupancy", [status, unit_id, hidden, visible, include_buildings, include_unseen, self_unit, box], result, expected, n)

    def spatial_fixture(uc, entries):
        # Native rectangle queries resolve unit IDs into the original unit pool.
        axes = [[], []]
        for index, box in entries:
            address = 0x5DA250 + index * 0x150
            for axis in [0, 1]:
                axes[axis].extend([(box[axis], index, False), (box[axis + 2], index, True)])
            put(uc, address + 0xC, FIXTURE + 0x3000 + index * 0x28)
            put(uc, address + 0x64, 7, 2)
            put(uc, address + 0xDC, 0)
        for axis, table in enumerate([0x5C99B8, 0x5D03F8]):
            axes[axis].sort(key=lambda value: value[0])
            for position, (coordinate, index, high) in enumerate(axes[axis]):
                put(uc, table + position * 8, index)
                put(uc, table + position * 8 + 4, coordinate)
                put(uc, 0x5DA250 + index * 0x150 + [0x13C, 0x144][axis] + (4 if high else 0), position)
        put(uc, 0x5C99B0, len(entries) * 2)
        put(uc, 0x5D6E48, 0)
        put(uc, 0x5D6E5C, 0)
        put(uc, 0x5DA38C, 0)
        return axes

    rectangle = [32, 32, 64, 64]
    for entry_boxes in [[], [boxes[0]], [boxes[1]], [boxes[2]], [boxes[3]], [boxes[4]],
                        [boxes[5]], [[32, 32, 64, 64]],
                        [[20, 20, 70, 70]], [boxes[0], boxes[5]], [boxes[5], boxes[0]],
                        [boxes[2], boxes[0], boxes[5]], [boxes[0], [33, 100, 63, 120]],
                        [boxes[0], [100, 33, 120, 63]]]:
        uc = machine()
        entries = [(i + 1, box) for i, box in enumerate(entry_boxes)]
        axes = spatial_fixture(uc, entries)
        for i, value in enumerate(rectangle):
            put(uc, FIXTURE + i * 2, value, 2)
        result, n = call(uc, 0x422330, FIXTURE)
        observed = []
        for i in range(len(entries) + 1):
            value = get(uc, result + i * 4)
            if value == 0:
                break
            observed.append((value - 0x5DA250) // 0x150)
        y_first = sum(c < rectangle[1] for c, _, _ in axes[1])
        y_last = sum(c < rectangle[3] for c, _, _ in axes[1])
        expected = []
        for coordinate, index, _ in axes[0]:
            if not rectangle[0] <= coordinate < rectangle[2] or index in expected:
                continue
            unit_address = 0x5DA250 + index * 0x150
            if get(uc, unit_address + 0x148) >= y_first and get(uc, unit_address + 0x144) < y_last:
                expected.append(index)
        record("spatial_rectangle", [entry_boxes, rectangle], observed, expected, n)
        assert get(uc, 0x5D6E48) == 1 and get(uc, 0x5D6E5C) == len(observed) + 1
        _, release_n = call(uc, 0x4222B0, result)
        record("spatial_release", [entry_boxes], [get(uc, 0x5D6E48), get(uc, 0x5D6E5C)], [0, 0], release_n)

    for generation, seen_tag in [(0, 0), (MASK, 0), (MASK, 1)]:
        uc = machine()
        spatial_fixture(uc, [(1, boxes[0])])
        put(uc, 0x5DA38C, generation)
        put(uc, 0x5D6E60 + 4, seen_tag)
        for i, value in enumerate(rectangle):
            put(uc, FIXTURE + i * 2, value, 2)
        result, n = call(uc, 0x422330, FIXTURE)
        expected = 0 if generation == MASK and seen_tag == 1 else 0x5DA3A0
        record("spatial_generation", [generation, seen_tag],
               [get(uc, result), get(uc, 0x5DA38C)], [expected, 1], n)
    uc = machine()
    spatial_fixture(uc, [(1, boxes[0])])
    for i, value in enumerate(rectangle):
        put(uc, FIXTURE + i * 2, value, 2)
    first, n1 = call(uc, 0x422330, FIXTURE)
    second, n2 = call(uc, 0x422330, FIXTURE)
    assert second == first + 8 and get(uc, second) == 0x5DA3A0
    _, n3 = call(uc, 0x4222B0, second)
    after_one = [get(uc, 0x5D6E48), get(uc, 0x5D6E5C)]
    _, n4 = call(uc, 0x4222B0, first)
    record("spatial_nested_release", [],
           [after_one, [get(uc, 0x5D6E48), get(uc, 0x5D6E5C)]], [[1, 2], [0, 0]], n1 + n2 + n3 + n4)

    for reverse in [0, 1]:
        uc = machine()
        border_fixture(uc, builder, boxes[0], owner=3)
        border_fixture(uc, candidate, boxes[0], status=0x100, visibility=0)
        border_fixture(uc, candidate + 0x150, boxes[0])
        ordered = [candidate + 0x150, candidate] if reverse else [candidate, candidate + 0x150]
        for i, value in enumerate(ordered + [0]):
            put(uc, unitlist + i * 4, value)
        result, n = call(uc, 0x413390, builder, unitlist, [1, 1, 0, 1])
        record("occupancy_first_eligible", [reverse], result, 4 if reverse else 1, n)

    for prior, occupied_bit, builder_kind, candidate_present, vision_flag in [
        (0, 0, None, 0, 0), (7, 0, None, 0, 0), (0, 1, None, 0, 0),
        (0, 1, 7, 0, 0), (0, 1, 134, 0, 0), (0, 0, None, 1, 0),
        (0, 0, None, 1, 1), (0, 0, None, 1, 2),
    ]:
        uc = machine()
        flags = 0x08000000 if occupied_bit else (1 << 3) if vision_flag else 0
        output = tile_fixture(uc, 3, 2, flags)
        uc.mem_write(output, bytes([0]) * 48)
        for position in [0, 1, 6, 7]:
            put(uc, output + position, prior, 1)
        put(uc, 0x522728 + 109 * 4, 64, 2)
        put(uc, 0x52272A + 109 * 4, 64, 2)
        for i in range(4):
            put(uc, 0x522C38 + 109 * 8 + i * 2, 31, 2)
        spatial_fixture(uc, [(1, boxes[0])] if candidate_present else [])
        builder_pointer = 0
        if builder_kind is not None:
            builder_pointer = builder
            border_fixture(uc, builder, [32, 32, 95, 95], unit_id=builder_kind, status=2)
        result, n = call(uc, 0x412360, builder_pointer, 1,
                         [1, 3, 109, (2 << 16) | 2, 2, 0, 0, int(vision_flag == 2)])
        if prior:
            expected_cells, expected_result = [prior] * 4, 0
        elif occupied_bit and builder_kind != 7:
            expected_cells, expected_result = [4] * 4, 4
        elif candidate_present and vision_flag != 1:
            expected_cells, expected_result = [4, 0, 0, 0], 4
        else:
            expected_cells, expected_result = [0] * 4, 0
        record("occupancy_grid", [prior, occupied_bit, builder_kind, candidate_present, vision_flag],
               [result, [get(uc, output + p, 1) for p in [0, 1, 6, 7]]],
               [expected_result, expected_cells], n)

    # Complete shared validator calls. The index is genuinely empty; its real
    # query and release still execute. Builder-null/grounded fixtures avoid the
    # separate reachability helper rather than replacing it.
    for profile, unit_id, properties, group, builder_id in [
        ("ordinary", 109, 2, 2, None), ("creep", 143, 2 | 0x20000, 1, None),
        ("nydus", 134, 2 | 0x20000, 1, 134), ("powered", 160, 2 | 0x80000, 4, None),
        ("unpowered", 160, 2 | 0x80000, 4, None),
    ]:
        for controller in [1, 2]:
            for vision in [0, 1]:
                for flags in [0, 0x00400000, 0x40000000, 0x20800000, 1 << 3, 1 << 11]:
                    uc = machine()
                    output = tile_fixture(uc, 3, controller, flags)
                    put(uc, 0x521660 + unit_id * 4, properties)
                    put(uc, 0x523C40 + unit_id, group, 1)
                    put(uc, 0x522728 + unit_id * 4, 64, 2)
                    put(uc, 0x52272A + unit_id * 4, 64, 2)
                    for i in range(4):
                        put(uc, 0x522C38 + unit_id * 8 + i * 2, 31, 2)
                    put(uc, 0x6A2E80, 0)
                    spatial_fixture(uc, [])
                    builder_pointer = 0
                    if builder_id is not None:
                        builder_pointer = builder
                        border_fixture(uc, builder, [32, 32, 95, 95], unit_id=builder_id, status=2)
                    put(uc, 0x665B50, POWER)
                    put(uc, 0x665B54, power_unit if profile == "powered" else 0)
                    put(uc, 0x665B58, 128)
                    put(uc, 0x665B5C, 96)
                    put(uc, power_unit + 0xC, power_sprite)
                    put(uc, power_unit + 0x4C, 3, 1)
                    put(uc, power_unit + 0xFC, 0)
                    put(uc, power_sprite + 0x14, 64, 2)
                    put(uc, power_sprite + 0x16, 64, 2)
                    uc.mem_write(POWER, bytes([1]) * 48)
                    result, n = call(uc, 0x412F10, builder_pointer, 3, [1, 1, unit_id, 2, vision, 0, 0])
                    human = controller == 2
                    if profile == "unpowered":
                        cell = expected = 6
                    elif profile == "nydus":
                        cell = expected = (10 if human and flags & (1 << 11) else
                                           9 if human and vision and flags & (1 << 3) else
                                           0 if flags & (0x40000000 if vision else 0x00400000) else 5)
                    elif profile == "creep":
                        cell = expected = 10 if human and flags & (1 << 11) else 0
                        bypass = not vision and human and flags & (1 << 3)
                        if not bypass and not flags & (0x40000000 if vision else 0x00400000):
                            cell = 5
                            expected = max(expected, 5)
                    else:
                        mask = 0x20800000 | (0x40000000 if vision else 0x00400000)
                        cell = expected = (10 if human and flags & (1 << 11) else
                                           0 if not vision and human and flags & (1 << 3) else
                                           4 if flags & mask else 0)
                    footprint = [get(uc, output + p, 1) for p in [0, 1, 6, 7]]
                    assert footprint == [cell] * 4
                    if profile == "unpowered":
                        assert list(uc.mem_read(output, 48)) == [6] * 48
                    else:
                        assert get(uc, 0x5D6E48) == 0 and get(uc, 0x5D6E5C) == 0
                    record("shared_validator", [profile, controller, vision, flags],
                           [result, footprint], [expected, [cell] * 4], n)

    for unit_id in range(228):
        uc = machine()
        result, n = call(uc, 0x4BC730, unit_id)
        record("invalid_fog_unit_type", [unit_id], result,
               int(unit_id in (14, 85, 105, 202, 205, 206, 207, 208)), n)

    fog_node, fog_sprite = FIXTURE + 0x1000, FIXTURE + 0x2000

    def fog_fixture(uc, sprite_id, x, y, unit_id=188):
        put(uc, 0x5C9978, fog_node)
        put(uc, fog_node + 4, 0)
        put(uc, fog_node + 8, unit_id, 2)
        put(uc, fog_node + 0xC, fog_sprite)
        put(uc, fog_sprite + 8, sprite_id, 2)
        put(uc, fog_sprite + 0x14, x, 2)
        put(uc, fog_sprite + 0x16, y, 2)

    def large_tile_fixture(uc, flags, controller):
        put(uc, 0x666454, TILES)
        put(uc, 0x5264B4, 128, 2)
        put(uc, 0x5264B6, 128, 2)
        put(uc, 0x6726D0 + 3 * 36, controller, 1)
        for row in range(2):
            for col in range(4):
                put(uc, TILES + 4 * ((100 + row) * 128 + 100 + col), flags)
        put(uc, 0x522A18, 128 | (64 << 16))
        put(uc, 0x5D6E40, 128)
        put(uc, 0x5D6E44, 64)

    # Human low-bit paths consult fog sprites in vision mode, whereas other
    # paths use the live geyser query. This tests actual lookup, not a stub.
    for controller in [1, 2]:
        for flags in [0, 1 << 3, 1 << 11, (1 << 3) | (1 << 11)]:
            for vision in [0, 1]:
                for present, dx, sprite_id, fog_unit_id in [
                    (0, 0, 275, 188), (1, 0, 275, 188), (1, 1, 275, 188),
                    (1, 32, 275, 188), (1, -1, 275, 188), (1, 0, 279, 188),
                    (1, 0, 275, 14),
                ]:
                    uc = machine()
                    large_tile_fixture(uc, flags, controller)
                    spatial_fixture(uc, [(1, [3200 + dx, 3200, 3327 + dx, 3263])] if present else [])
                    live = 0x5DA3A0
                    put(uc, live + 0x64, 188, 2)
                    sprite = get(uc, live + 0xC)
                    if present:
                        put(uc, sprite + 0x14, 3264 + dx, 2)
                        put(uc, sprite + 0x16, 3232, 2)
                        fog_fixture(uc, sprite_id, 3264 + dx, 3232, fog_unit_id)
                    else:
                        put(uc, 0x5C9978, 0)
                    result, n = call(uc, 0x413610, 100, 100, [3, vision])
                    if controller == 2 and flags & (1 << 11):
                        expected = 10
                    elif controller == 2 and flags & (1 << 3):
                        expected = 0 if not vision or (present and dx == 0 and sprite_id == 275 and fog_unit_id not in (14, 85, 105, 202, 205, 206, 207, 208)) else 8
                    else:
                        expected = 0 if present and (3264 + dx) // 32 == 3264 // 32 else 8
                    record("gas_placement", [controller, flags, vision, present, dx, sprite_id, fog_unit_id], result, expected, n)

    for reverse in [0, 1]:
        uc = machine()
        large_tile_fixture(uc, 1 << 3, 2)
        spatial_fixture(uc, [])
        fog_fixture(uc, 275, 3264 + (0 if reverse else 1), 3232)
        second_node, second_sprite = fog_node + 0x100, fog_sprite + 0x100
        put(uc, fog_node + 4, second_node)
        put(uc, second_node + 4, 0)
        put(uc, second_node + 8, 188, 2)
        put(uc, second_node + 0xC, second_sprite)
        put(uc, second_sprite + 8, 275, 2)
        put(uc, second_sprite + 0x14, 3264 + (1 if reverse else 0), 2)
        put(uc, second_sprite + 0x16, 3232, 2)
        result, n = call(uc, 0x413610, 100, 100, [3, 1])
        record("gas_first_fog_match", [reverse], result, 0 if reverse else 8, n)

    for requested_id in [110, 149, 157]:
        for present, low_bit in [(0, 0), (1, 0), (0, 1)]:
            uc = machine()
            large_tile_fixture(uc, (1 << 3) if low_bit else 0, 2)
            spatial_fixture(uc, [(1, [3200, 3200, 3327, 3263])] if present else [])
            if present:
                put(uc, 0x5DA3A0 + 0x64, 188, 2)
                sprite = get(uc, 0x5DA3A0 + 0xC)
                put(uc, sprite + 0x14, 3264, 2)
                put(uc, sprite + 0x16, 3232, 2)
            put(uc, 0x5C9978, 0)
            put(uc, 0x521660 + requested_id * 4, 2 | (0x20000 if requested_id == 149 else 0))
            put(uc, 0x522728 + requested_id * 4, 128, 2)
            put(uc, 0x52272A + requested_id * 4, 64, 2)
            result, n = call(uc, 0x412F10, 0, 3, [100, 100, requested_id, 2, 0, 0, 0])
            expected = 0 if present or low_bit else 8
            output = 0x675188 + 2 * 48
            record("shared_gas_validator", [requested_id, present, low_bit],
                   [result, list(uc.mem_read(output, 48))], [expected, [expected] * 48], n)

    # Native fog-resource classification is by sprite ID. Mineral sprites map
    # to IDs176..178; gas-resource sprites use the geyser188 dimensions.
    fog_resource_ids = {178: 188, 204: 188, 263: 188, 275: 188, 279: 176, 280: 177, 281: 178}
    for sprite_id in range(177, 283):
        uc = machine()
        fog_fixture(uc, sprite_id, 3216, 3216)
        for uid in [176, 177, 178, 188]:
            put(uc, 0x522728 + uid * 4, 64, 2)
            put(uc, 0x52272A + uid * 4, 32, 2)
        result, n = call(uc, 0x4180A0, 100, 100, [3])
        record("fog_resource_class", [sprite_id], result, fog_node if sprite_id in fog_resource_ids else 0, n)
    for dx in [-113, -112, -111, 0, 143, 144]:
        uc = machine()
        fog_fixture(uc, 275, 3216 + dx, 3216)
        put(uc, 0x522A18, 64 | (32 << 16))
        left_tile = int((3216 + dx - 32) / 32)
        expected = fog_node if 0 <= 100 - left_tile + 3 < 8 else 0
        result, n = call(uc, 0x4180A0, 100, 100, [3])
        record("fog_resource_margin", [dx], result, expected, n)

    for vision, controller, present, resource, offset, fog_id in [
        (0, 1, 0, 0, 0, None), (0, 2, 0, 0, 0, None),
        (0, 1, 1, 1, 0, None), (0, 1, 1, 0, 0, None),
        (0, 1, 1, 1, -2, None), (0, 1, 1, 1, -1, None), (0, 1, 1, 1, 223, None), (0, 1, 1, 1, 224, None),
        (1, 2, 0, 0, 0, 275), (0, 2, 0, 0, 0, 275),
        (1, 2, 0, 0, 0, 279), (1, 2, 0, 0, 0, 178), (1, 2, 0, 0, 0, 100),
    ]:
        uc = machine()
        large_tile_fixture(uc, 0, controller)
        unit_id = 106
        put(uc, 0x521660 + unit_id * 4, 2 | 0x1000)
        put(uc, 0x523C40 + unit_id, 2, 1)
        location = 3104 + offset
        spatial_fixture(uc, [(1, [location, 3200, location + 1, 3201])] if present else [])
        if present:
            put(uc, 0x5DA3A0 + 0x64, 176, 2)
        put(uc, 0x521660 + 176 * 4, 0x2000 if resource else 0)
        put(uc, 0x5C9978, 0)
        if fog_id is not None:
            fog_fixture(uc, fog_id, 3216, 3216)
            for uid in [176, 177, 178, 188]:
                put(uc, 0x522728 + uid * 4, 64, 2)
                put(uc, 0x52272A + uid * 4, 32, 2)
        output = 0x675188 + 2 * 48
        put(uc, output, 0xA7, 1)
        result, n = call(uc, 0x412870, unit_id, 100, [100, 3, (1 << 16) | 1, 2, vision])
        hit = (present and resource and -1 <= offset < 224) or (vision and fog_id in fog_resource_ids)
        expected = 2 if hit else 0
        record("depot_resource_neighborhood", [vision, controller, present, resource, offset, fog_id],
               [result, get(uc, output, 1)], [expected, expected], n)

    map_pointer = FIXTURE + 0x30000
    for collision_flags in [0, 1, 2, 3]:
        for same_group in [0, 1]:
            uc = machine()
            border_fixture(uc, builder, boxes[0])
            sprite = get(uc, builder + 0xC)
            put(uc, sprite + 0x14, 512, 2)
            put(uc, sprite + 0x16, 512, 2)
            put(uc, builder + 0x105, collision_flags, 1)
            put(uc, 0x69B584, map_pointer)
            put(uc, map_pointer + 0xC + (16 * 256 + 16) * 2, 1, 2)
            put(uc, map_pointer + 0xC + (16 * 256 + 17) * 2, 2, 2)
            put(uc, map_pointer + 0x449FE + 1 * 64, 7, 2)
            put(uc, map_pointer + 0x449FE + 2 * 64, 7 if same_group else 8, 2)
            result, n = call(uc, 0x48E980, builder, 544, [512])
            record("builder_reachability", [collision_flags, same_group], result,
                   int(not collision_flags & 1 or same_group), n)

    for collision_flags, same_group in [(0, 0), (1, 1), (1, 0)]:
        uc = machine()
        output = tile_fixture(uc, 3, 1, 0)
        unit_id = 109
        put(uc, 0x521660 + unit_id * 4, 2)
        put(uc, 0x523C40 + unit_id, 2, 1)
        put(uc, 0x522728 + unit_id * 4, 64, 2)
        put(uc, 0x52272A + unit_id * 4, 64, 2)
        for i in range(4):
            put(uc, 0x522C38 + unit_id * 8 + i * 2, 31, 2)
        spatial_fixture(uc, [])
        border_fixture(uc, builder, [500, 500, 524, 524], status=1)
        sprite = get(uc, builder + 0xC)
        put(uc, sprite + 0x14, 512, 2)
        put(uc, sprite + 0x16, 512, 2)
        put(uc, builder + 0x105, collision_flags, 1)
        put(uc, 0x6A2E80, 0)
        put(uc, 0x69B584, map_pointer)
        put(uc, map_pointer + 0xC + (16 * 256 + 16) * 2, 1, 2)
        put(uc, map_pointer + 0xC + (1 * 256 + 1) * 2, 2, 2)
        put(uc, map_pointer + 0x449FE + 64, 7, 2)
        put(uc, map_pointer + 0x449FE + 128, 7 if same_group else 8, 2)
        result, n = call(uc, 0x412F10, builder, 3, [1, 1, unit_id, 2, 0, 0, 0])
        expected = 7 if collision_flags & 1 and not same_group else 0
        footprint = [get(uc, output + p, 1) for p in [0, 1, 6, 7]]
        record("shared_builder_reachability", [collision_flags, same_group],
               [result, footprint], [expected, [expected] * 4], n)
        if expected:
            assert list(uc.mem_read(output, 48)) == [7] * 48

    power_profile = None
    if args.power_grp:
        grp = args.power_grp.read_bytes()
        grp_hash = hashlib.sha256(grp).hexdigest()
        if grp_hash != "cd98ce60f5afb8167896cc099f0e9650edf2dc03bcc8c14fda90a667bff3ae08":
            raise ValueError("Unreviewed power GRP")
        _, _, width, height, data_offset = struct.unpack_from("<4BI", grp, 6)
        grp_address = FIXTURE + 0x30000
        frame = grp_address + 6

        def load_frame(uc):
            uc.mem_write(grp_address, grp)
            put(uc, frame + 4, grp_address + data_offset)

        def sample(x, y):
            if not 0 <= x < width or not 0 <= y < height:
                return 0
            position = data_offset + struct.unpack_from("<H", grp, data_offset + y * 2)[0]
            remaining = x
            while True:
                command = grp[position]
                position += 1
                if command & 0x80:
                    length = command & 0x7F
                elif command & 0x40:
                    length = command & 0x3F
                    position += 1
                else:
                    length = command
                    position += length
                if not length:
                    raise ValueError("Zero-length GRP run")
                remaining -= length
                if remaining <= 0:
                    return int(not command & 0x80)

        for x, y in [(0, 0), (width - 1, 0), (width, 0), (0, height), (-1, 0)] + [
            (16 + col * 32, 16 + row * 32) for row in range(5) for col in range(8)
        ]:
            uc = machine()
            load_frame(uc)
            result, n = call(uc, 0x40FD50, frame, x, [y])
            record("original_grp_sample", [x, y], result, sample(x, y), n)
        half_width, half_height = (width + 15) // 32, (height + 15) // 32
        generated = [0] * (half_width * half_height * 4)
        quarter = []
        for row in range(half_height):
            quarter_row = []
            for col in range(half_width):
                value = sample(16 + col * 32, 16 + row * 32)
                quarter_row.append(value)
                for r in [row, half_height * 2 - row - 1]:
                    for c in [half_width + col, half_width - col - 1]:
                        generated[r * half_width * 2 + c] = value
            quarter.append(quarter_row)
        uc = machine()
        load_frame(uc)
        put(uc, 0x665B58, half_width)
        put(uc, 0x665B5C, half_height)
        put(uc, 0x544018, grp_address)
        frame_pointer, pointer_n = call(uc, 0x40F670)
        record("power_frame_pointer", [], frame_pointer, frame, pointer_n)
        context = STACK + 0xF000
        put(uc, context, 0)
        put(uc, context + 4, STOP)
        put(uc, context - 0x14, frame)
        result, n = call(uc, 0x42FEBC, fragment_ebp=context, eax=POWER)
        observed = list(uc.mem_read(POWER, len(generated)))
        record("power_mask_post_allocation_fragment", [width, height, half_width, half_height],
               [observed, get(uc, 0x665B58), get(uc, 0x665B5C)],
               [generated, half_width * 32, half_height * 32], n)
        cases[-1]["evidence"] = "Original post-allocation fragment; imported allocation and full initializer not executed"
        power_profile = {"grp_sha256": grp_hash, "frame_dimensions": [width, height],
                         "half_extents_pixels": [half_width * 32, half_height * 32],
                         "mask_dimensions": [half_width * 2, half_height * 2],
                         "right_upper_quarter_top_to_center": quarter,
                         "mask": [generated[r * half_width * 2:(r + 1) * half_width * 2]
                                  for r in range(half_height * 2)]}

    args.out.parent.mkdir(parents=True, exist_ok=True)
    result = {"target_sha256": TARGET_HASH, "version": "1.17.0.1",
              "probe_script_sha256": probe_script_hash,
              "runtime_versions": {"python": platform.python_version(), "architecture": platform.machine(),
                                   "unicorn": unicorn.__version__, "pefile": pefile.__version__, "capstone": capstone.__version__},
              "case_count": len(cases), "all_expected_results_matched": True,
              "case_kinds": dict(Counter(c["kind"] for c in cases)), "cases": cases,
              "original_power_asset_profile": power_profile,
              "allowlisted_va_ranges": [[hex(a), hex(b)] for a, b in ALLOW],
              "instruction_limit": 10000, "helper_stubs": [], "original_imports_resolved": False,
              "complete_original_game_executed": False,
              "limits": ["Synthetic tile flag/index/power inputs; their generation is separate.",
                         "No construction order progression, allocation, Windows API, host file/network service."]}
    args.out.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({"case_count": len(cases), "case_kinds": result["case_kinds"], "out": str(args.out)}))


if __name__ == "__main__":
    main()
