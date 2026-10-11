#!/usr/bin/env python3
"""Probe reviewed original 1.17 region search in private, bounded x86 memory.

Default mode only hashes/disassembles the supplied executable. Explicit execution
uses synthetic region graphs, real reviewed callees, and no replacement helper,
Windows import, guest host filesystem or network operation.
"""

import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import platform
import random
import struct
import heapq

import capstone
import pefile

TARGET_HASH = "94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0"
CONTEXT, OUT, MAP, NEIGHBORS = 0x810000, 0x811000, 0x840000, 0x8F0000
STACK, STOP = 0x900000, 0xA00000
BLOCKS = {
    "region_long_path": (0x491300, 0x4919B0),
    "heap_change_priority": (0x491A70, 0x491B11),
    "heap_sift_down": (0x491B20, 0x491B9E),
    "approximate_distance": (0x405CF0, 0x405D3D),
    "region_group": (0x481C90, 0x481CA4),
    "region_center": (0x481A60, 0x481A86),
    "route_cut_rank": (0x481A90, 0x481AA4),
    "native_stack_probe": (0x4D2200, 0x4D222B),
    "single_route_cut": (0x40E4E0, 0x40E56F),
    "placement_path_distance": (0x496930, 0x496ABF),
    "region_at_pixel": (0x481B10, 0x481B98),
    "region_at_unit": (0x481BA0, 0x481BB0),
    "region_literal_1ffd": (0x481BB0, 0x481BD1),
    "nearest_hostile_in_player_list": (0x48DFF0, 0x48E080),
    "unit_inactive": (0x41FE40, 0x41FE55),
    "player_unit_list": (0x4256F0, 0x4256F8),
    "hostility_predicate": (0x429110, 0x429130),
    "repeated_route_cuts": (0x40E190, 0x40E2EB),
    "ai_anchor_producer": (0x496E10, 0x496F1A),
    "ai_radius_filter": (0x4976D0, 0x497906),
}


def distance(a, b):
    """The previously recovered integer distance, on nonoverflowing fixtures."""
    major, minor = sorted([abs(a[0] - b[0]), abs(a[1] - b[1])], reverse=True)
    if major // 4 >= minor:
        return major
    scale = (major // 4) // 4
    small = (3 * minor) // 8
    return major - scale - scale // 4 + small + small // 32


def main():
    own_hash = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", required=True, type=Path)
    parser.add_argument("--static-out", type=Path)
    parser.add_argument("--out", type=Path)
    parser.add_argument("--execute-original-code", action="store_true")
    args = parser.parse_args()
    data = args.exe.read_bytes()
    if hashlib.sha256(data).hexdigest() != TARGET_HASH:
        parser.error("Unknown executable hash; reviewed addresses do not apply")
    pe = pefile.PE(data=data)
    base = pe.OPTIONAL_HEADER.ImageBase
    assert base == 0x400000 and pe.FILE_HEADER.Machine == 0x14C
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    witnesses = {}
    for name, (lo, hi) in BLOCKS.items():
        rows = list(md.disasm(pe.get_data(lo - base, hi - lo), lo))
        if not rows or rows[-1].address + rows[-1].size != hi:
            parser.error("Invalid instruction boundary: " + name)
        witnesses[name] = [{"va": hex(i.address), "bytes": i.bytes.hex(),
                            "mnemonic": i.mnemonic, "operands": i.op_str} for i in rows]
    initial_node = list(struct.unpack("<4I", pe.get_data(0x4EFA00 - base, 16)))
    text_section = next(section for section in pe.sections if section.Name.rstrip(b"\0") == b".text")
    md.skipdata = True
    anchor_count_references = [{"va": hex(i.address), "bytes": i.bytes.hex(),
                                "mnemonic": i.mnemonic, "operands": i.op_str}
                               for i in md.disasm(text_section.get_data(), base + text_section.VirtualAddress)
                               if "0x6a2e84" in i.op_str]
    allocator_import = [{"dll": item.dll.decode("ascii"), "ordinal": imported.ordinal,
                         "name": imported.name.decode("ascii") if imported.name else None,
                         "iat_va": hex(imported.address)}
                        for item in pe.DIRECTORY_ENTRY_IMPORT for imported in item.imports
                        if imported.address == 0x4EB55C]
    if args.static_out:
        args.static_out.parent.mkdir(parents=True, exist_ok=True)
        args.static_out.write_text(json.dumps({"target_sha256": TARGET_HASH,
            "version": "1.17.0.1", "evidence": "static; analytical labels",
            "witnesses": witnesses, "first_node_parent_heuristic_cost_priority": initial_node,
            "anchor_count_linear_text_references": anchor_count_references,
            "linear_scan_limit": "Embedded data/misalignment can miss references; not a complete disassembler control-flow proof",
            "route_backup_allocator_import": allocator_import}, indent=2) + "\n")
    if not args.execute_original_code:
        print(json.dumps({"blocks": len(BLOCKS), "target_sha256": TARGET_HASH,
                          "plan": "Synthetic graph paths and cuts; actual original callees, no stubs"}))
        return
    if args.out is None:
        parser.error("--out required for execution evidence")
    import unicorn
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_ESP, UC_X86_REG_EIP
    image = pe.get_memory_mapped_image()
    size = (pe.OPTIONAL_HEADER.SizeOfImage + 4095) & ~4095
    reviewed = {(int(row["va"], 16), len(bytes.fromhex(row["bytes"])))
                for rows in witnesses.values() for row in rows}
    entries = {va[0]: name for name, va in BLOCKS.items()}
    interesting = {0x491732: "decrease_open_node", 0x491739: "reopen_closed_node",
                   0x491803: "search_capacity_return", 0x491830: "exhausted_open_set",
                   0x4918A3: "forward_fallback_after_reverse_search"}
    cases = []

    def put(uc, address, value, width=4):
        uc.mem_write(address, (value & ((1 << (width * 8)) - 1)).to_bytes(width, "little"))

    def get(uc, address, width=4):
        return int.from_bytes(uc.mem_read(address, width), "little")

    def region_address(index):
        return MAP + 0x449FC + index * 64

    def machine(graph, points=None, groups=None, ranks=None, ground_counts=None):
        uc = Uc(UC_ARCH_X86, UC_MODE_32)
        uc.mem_map(base, size)
        uc.mem_write(base, image)
        uc.mem_map(0x800000, 0x100000)
        uc.mem_map(STACK, 0x10000)
        uc.mem_map(STOP, 0x1000)
        put(uc, 0x69B584, MAP)
        cursor = NEIGHBORS
        points = points or {index: (index * 3, 30) for index in graph}
        for index, neighbors in graph.items():
            assert 0 <= index < 512 and len(neighbors) <= 255
            address = region_address(index)
            put(uc, address + 2, (groups or {}).get(index, 1), 2)
            put(uc, address + 6, (ground_counts or {}).get(index, len(neighbors)), 1)
            put(uc, address + 7, len(neighbors), 1)
            put(uc, address + 0xC, cursor)
            put(uc, address + 0x10, points[index][0] * 256)
            put(uc, address + 0x14, points[index][1] * 256)
            put(uc, address + 0x20, (ranks or {}).get(index, 0), 1)
            if neighbors:
                uc.mem_write(cursor, struct.pack("<" + "H" * len(neighbors), *neighbors))
            cursor += 2 * len(neighbors)
        assert cursor < STACK
        uc.mem_write(CONTEXT, b"\xCC" * 0x174)
        put(uc, OUT, 0xBEEF, 2)
        return uc, points

    def initialize(uc, source, target, points, source_point=None, target_point=None):
        for address, point in [(CONTEXT + 4, source_point or points[source]),
                               (CONTEXT + 8, target_point or points[target])]:
            put(uc, address, point[0], 2)
            put(uc, address + 2, point[1], 2)
        put(uc, CONTEXT + 0x160, source, 2)
        put(uc, CONTEXT + 0x162, target, 2)

    def map_point(uc, point, region):
        assert min(point) >= 0 and max(point) < 8192
        put(uc, MAP + 0xC + 2 * ((point[1] // 32) * 256 + point[0] // 32), region, 2)

    def call(uc, address, ecx=CONTEXT, edx=0, args=()):
        stack = STACK + 0xF000
        uc.mem_write(stack, struct.pack("<" + "I" * (1 + len(args)), STOP, *args))
        uc.reg_write(UC_X86_REG_ESP, stack)
        uc.reg_write(UC_X86_REG_ECX, ecx)
        uc.reg_write(UC_X86_REG_EDX, edx)
        count = 0
        trace = Counter()
        branches = Counter()
        def check(machine, va, width, unused):
            nonlocal count
            count += 1
            if va == STOP:
                machine.emu_stop()
                return
            if (va, width) not in reviewed:
                raise RuntimeError("Unreviewed instruction " + hex(va))
            if va in entries:
                trace[entries[va]] += 1
            if va in interesting:
                branches[interesting[va]] += 1
        hook = uc.hook_add(UC_HOOK_CODE, check)
        try:
            uc.emu_start(address, STOP + 1, count=2000000)
            if uc.reg_read(UC_X86_REG_EIP) != STOP or uc.reg_read(UC_X86_REG_ESP) != stack + 4 + len(args) * 4:
                raise RuntimeError("Did not complete reviewed function with its expected ABI")
        finally:
            uc.hook_del(hook)
        return {"returned": uc.reg_read(UC_X86_REG_EAX), "instructions": count,
                "calls": dict(trace), "branches": dict(branches)}

    def observe(uc, graph):
        count = get(uc, CONTEXT + 0x86, 2)
        return {"partial_byte": get(uc, CONTEXT + 0x1C, 1),
                "used_nodes": get(uc, CONTEXT + 0x164, 2),
                "peak_heap": get(uc, CONTEXT + 0x168, 2),
                "full_path_count_low_byte": get(uc, CONTEXT + 0x154, 1),
                "stored_path_count": count,
                "path": [get(uc, CONTEXT + 0x20 + i * 2, 2) for i in range(min(count, 50))],
                "cursor_word": get(uc, CONTEXT + 0x84, 2),
                "remaining_region_backrefs": {str(i): get(uc, region_address(i) + 8)
                                              for i in graph if get(uc, region_address(i) + 8)}}

    def record(kind, inputs, run, observed, expected):
        for key, value in expected.items():
            if observed.get(key) != value:
                raise AssertionError(json.dumps({"kind": kind, "inputs": inputs, "run": run,
                                     "observed": observed, "expected": expected}))
        cases.append({"kind": kind, "inputs": inputs, "original_execution": run,
                      "observed": observed, "expected": expected, "matched": True})

    # Straight chains independently fix adjacency, length and truncation boundaries.
    for length in [2, 3, 4, 5, 10, 49, 50, 51, 52, 100, 254, 255, 256, 349, 350, 351, 400]:
        graph = {i: ([i - 1] if i > 1 else []) + ([i + 1] if i < length else [])
                 for i in range(1, length + 1)}
        uc, points = machine(graph)
        initialize(uc, 1, length, points)
        run = call(uc, 0x491300)
        reached = min(length, 350)
        observed = observe(uc, graph)
        observed["returned"] = run["returned"]
        record("chain_length_and_capacity", [length], run, observed,
               {"returned": 1, "partial_byte": 0, "used_nodes": reached, "peak_heap": 1,
                "full_path_count_low_byte": reached & 255, "stored_path_count": min(reached, 50),
                "path": list(range(1, min(reached, 50) + 1)), "cursor_word": 65535,
                "remaining_region_backrefs": {}})

    # Identical low16 regions return before any output reset.
    for source, target in [(1, 1), (0x10001, 1), (1, 0x10001)]:
        graph = {1: []}
        uc, points = machine(graph)
        initialize(uc, source, target, {source: (3, 30), target: (3, 30)})
        before = bytes(uc.mem_read(CONTEXT, 0x174))
        run = call(uc, 0x491300)
        record("equal_region_leaves_context", [source, target], run,
               {"returned": run["returned"], "context_unchanged": bytes(uc.mem_read(CONTEXT, 0x174)) == before},
               {"returned": 0, "context_unchanged": True})

    # Disconnected same-group paths return the first strictly closest visited node.
    for target_x in [0, 9, 12, 15, 18, 21, 60]:
        graph = {1: [2, 3], 2: [], 3: [], 4: []}
        points = {1: (3, 30), 2: (9, 30), 3: (15, 30), 4: (target_x, 30)}
        uc, points = machine(graph, points)
        initialize(uc, 1, 4, points)
        # Candidate discovery order is1,2,3 even when heap extraction differs.
        best = min([1, 2, 3], key=lambda i: distance(points[i], points[4]))
        run = call(uc, 0x491300)
        observed = observe(uc, graph)
        observed["returned"] = run["returned"]
        path = [1] if best == 1 else [1, best]
        record("disconnected_nearest_visited", [target_x], run, observed,
               {"returned": 1, "partial_byte": 1, "used_nodes": 3, "peak_heap": 2,
                "path": path, "stored_path_count": len(path), "remaining_region_backrefs": {}})

    # Two equal routes distinguish heap-insertion ties from region-ID sorting.
    for neighbor_order in [[2, 3], [3, 2]]:
        graph = {1: neighbor_order, 2: [4], 3: [4], 4: []}
        points = {1: (30, 30), 2: (60, 15), 3: (60, 45), 4: (90, 30)}
        uc, points = machine(graph, points)
        initialize(uc, 1, 4, points)
        run = call(uc, 0x491300)
        observed = observe(uc, graph)
        record("equal_priority_neighbor_order", neighbor_order, run, observed,
               {"path": [1, neighbor_order[-1], 4], "partial_byte": 0,
                "remaining_region_backrefs": {}})

    # An incomplete neighbor prefix is used when the open heap reaches125.
    for neighbor_count in [124, 125, 126, 200, 255]:
        graph = {1: list(range(2, neighbor_count + 2)), 400: []}
        graph.update({i: [] for i in range(2, neighbor_count + 2)})
        points = {i: (60, 30) for i in graph}
        points[1], points[400] = (30, 30), (300, 30)
        uc, points = machine(graph, points)
        initialize(uc, 1, 400, points)
        run = call(uc, 0x491300)
        observed = observe(uc, graph)
        limited = neighbor_count >= 125
        record("open_heap_capacity", [neighbor_count], run, observed,
               {"path": [1, 126 if limited else 2], "partial_byte": int(not limited),
                "used_nodes": 126 if limited else neighbor_count + 1,
                "peak_heap": min(neighbor_count, 125), "remaining_region_backrefs": {}})

    # Complete route-cut selection composes the actual long-path function.
    for length in [2, 3, 4, 5, 6, 8, 51]:
        for maximum in [0, 1, 2, 3, 6, 100]:
            for minimum in [0, 1, 2, 3, 49]:
                graph = {i: ([i + 1] if i < length else []) for i in range(1, length + 1)}
                ranks = {i: 7 for i in graph}
                uc, points = machine(graph, ranks=ranks)
                initialize(uc, 1, length, points)
                run = call(uc, 0x40E4E0, edx=maximum, args=[OUT, minimum])
                last = min(min(length, 50) - 2, maximum)
                expected_status = 4 if min(length, 50) < minimum or last < minimum else 0
                expected_cut = 0xBEEF if expected_status else last + 1
                record("route_cut_range_and_last_rank_tie", [length, maximum, minimum], run,
                       {"returned": run["returned"], "cut_region": get(uc, OUT, 2)},
                       {"returned": expected_status, "cut_region": expected_cut})
    for ranks_list, expected_status, expected_cut in [
            ([0, 0, 0, 0, 0, 0], 3, 0xBEEF), ([0, 1, 8, 2, 0, 0], 0, 3),
            ([0, 1, 8, 8, 0, 0], 0, 4), ([0, 255, 0, 0, 0, 0], 0, 2)]:
        graph = {i: ([i + 1] if i < 6 else []) for i in range(1, 7)}
        uc, points = machine(graph, ranks={i: value for i, value in enumerate(ranks_list, 1)})
        initialize(uc, 1, 6, points)
        run = call(uc, 0x40E4E0, edx=6, args=[OUT, 1])
        record("route_cut_rank_values", ranks_list, run,
               {"returned": run["returned"], "cut_region": get(uc, OUT, 2)},
               {"returned": expected_status, "cut_region": expected_cut})
    for equal in [False, True]:
        graph = {1: [], 2: []}
        uc, points = machine(graph)
        initialize(uc, 1, 1 if equal else 2, points)
        run = call(uc, 0x40E4E0, edx=6, args=[OUT, 2])
        record("route_cut_no_complete_route", [equal], run,
               {"returned": run["returned"], "cut_region": get(uc, OUT, 2)},
               {"returned": 2, "cut_region": 0xBEEF})

    # Differing groups run the target-to-source search before its forward fallback.
    for mode, expected_path, expected_partial, used in [
            ("reverse_connected", [1, 2, 3], 0, 6),
            ("forward_only", [1, 2, 3, 4, 5, 6], 1, 6),
            ("disconnected_groups", [1, 2, 3], 1, 3)]:
        graph = {i: ([i + 1] if i < 6 else []) for i in range(1, 7)}
        if mode != "forward_only":
            for i in range(2, 7):
                graph[i].append(i - 1)
        if mode == "disconnected_groups":
            graph[3].remove(4)
            graph[4].remove(3)
        groups = {i: 1 if i <= 3 else 2 for i in graph}
        uc, points = machine(graph, groups=groups)
        initialize(uc, 1, 6, points)
        run = call(uc, 0x491300)
        observed = observe(uc, graph)
        record("different_groups_search_direction", [mode], run, observed,
               {"path": expected_path, "partial_byte": expected_partial,
                "used_nodes": used, "peak_heap": 1, "remaining_region_backrefs": {}})

    for middle_group, expected_path, expected_partial, expected_nodes in [
            (2, [1], 0, 3), (1, [1, 2], 1, 2)]:
        graph = {1: [2], 2: [1], 3: [2]}
        uc, points = machine(graph, groups={1: 1, 2: middle_group, 3: 2}, ground_counts={2: 0})
        initialize(uc, 1, 3, points)
        run = call(uc, 0x491300)
        record("ground_vs_all_neighbor_count", [middle_group], run, observe(uc, graph),
               {"path": expected_path, "partial_byte": expected_partial,
                "used_nodes": expected_nodes, "remaining_region_backrefs": {}})
    graph = {1: [2], 2: [], 3: []}
    points = {1: (300, 30), 2: (128, 30), 3: (64, 30)}
    uc, points = machine(graph, points)
    initialize(uc, 1, 3, points, source_point=(1, 30))
    run = call(uc, 0x491300)
    record("exhaustion_recomputes_start_from_region_center", {"points": points, "source_point": [1, 30]},
           run, observe(uc, graph), {"path": [1, 2], "partial_byte": 1, "remaining_region_backrefs": {}})

    # Graphs with positive weights are compared with an independent
    # Dijkstra oracle rather than an implementation of the original A* heap.
    for seed in range(100):
        rng = random.Random(seed)
        graph = {i: ([i + 1] if i < 8 else []) for i in range(1, 9)}
        for i in range(1, 7):
            graph[i].extend(j for j in range(i + 2, 9) if rng.randrange(4) == 0)
        points = {i: (i * 30, 30 + rng.randrange(60)) for i in graph}
        fixed_points = {i: (point[0] * 256, point[1] * 256) for i, point in points.items()}
        costs, pending = {1: 0}, [(0, 1)]
        while pending:
            cost, region = heapq.heappop(pending)
            if cost != costs[region]:
                continue
            for neighbor in graph[region]:
                new = cost + distance(fixed_points[region], fixed_points[neighbor])
                if new < costs.get(neighbor, 1 << 60):
                    costs[neighbor] = new
                    heapq.heappush(pending, (new, neighbor))
        uc, points = machine(graph, points)
        initialize(uc, 1, 8, points)
        run = call(uc, 0x491300)
        observed = observe(uc, graph)
        path = observed["path"]
        valid = path[0] == 1 and path[-1] == 8 and all(b in graph[a] for a, b in zip(path, path[1:]))
        cost = sum(distance(fixed_points[a], fixed_points[b]) for a, b in zip(path, path[1:]))
        observed.update({"route_edges_valid": valid, "route_cost": cost, "dijkstra_cost": costs[8]})
        record("independent_shortest_path_graphs", {"seed": seed, "graph": graph, "points": points},
               run, observed, {"route_edges_valid": True, "route_cost": costs[8],
                               "partial_byte": 0, "remaining_region_backrefs": {}})

    # The distance heuristic is not always consistent. These distinguishing
    # graphs verify both a reopened closed node and early nonoptimal goal exit.
    for scale in [1, 8, 16, 64]:
        base_points = {1: (4, 19), 2: (4, 20), 3: (53, 32), 4: (70, 90), 5: (63, 40)}
        points = {i: (x * scale, y * scale) for i, (x, y) in base_points.items()}
        graph = {1: [3, 2], 2: [3], 3: [4], 4: [5], 5: []}
        uc, points = machine(graph, points)
        initialize(uc, 1, 5, points)
        run = call(uc, 0x491300)
        observed = observe(uc, graph)
        observed["closed_reopen_count"] = run["branches"].get("reopen_closed_node", 0)
        observed["decrease_open_count"] = run["branches"].get("decrease_open_node", 0)
        record("closed_node_reopening", {"scale": scale, "points": points}, run, observed,
               {"path": [1, 2, 3, 4, 5], "partial_byte": 0, "closed_reopen_count": 1,
                "decrease_open_count": 1, "remaining_region_backrefs": {}})
        graph = {1: [3, 2], 2: [3], 3: [5], 5: []}
        uc, points = machine(graph, points)
        initialize(uc, 1, 5, points)
        run = call(uc, 0x491300)
        observed = observe(uc, graph)
        fixed = {i: (x * 256, y * 256) for i, (x, y) in points.items()}
        direct = distance(fixed[1], fixed[3]) + distance(fixed[3], fixed[5])
        alternative = distance(fixed[1], fixed[2]) + distance(fixed[2], fixed[3]) + distance(fixed[3], fixed[5])
        observed.update({"returned_route_cost": direct, "alternative_route_cost": alternative,
                         "returned_route_is_longer": direct > alternative})
        record("heuristic_nonoptimal_early_goal", {"scale": scale, "points": points}, run, observed,
               {"path": [1, 3, 5], "partial_byte": 0, "returned_route_is_longer": True,
                "remaining_region_backrefs": {}})

    # Priority changes and downward repair use their complete original helpers.
    heap, node_base, sentinel = 0x812000, 0x813000, 0x814000
    original = [5, 10, 10, 30, 40, 50]
    for selected, new_priority, expected_ids in [
            (1, 60, [2, 4, 3, 1, 5, 6]), (6, 1, [6, 2, 1, 4, 5, 3]),
            (6, 5, [6, 2, 1, 4, 5, 3]), (6, 50, [1, 2, 3, 4, 5, 6]),
            (7, 7, [1, 2, 3, 4, 5, 6]), (1, 10, [1, 2, 3, 4, 5, 6])]:
        uc, unused = machine({1: []})
        put(uc, heap + 0x1F8, 6)
        put(uc, heap + 0x1FC, sentinel)
        put(uc, sentinel + 0x14, 0xFFFFFFFF)
        for index, priority in enumerate(original, 1):
            node = node_base + index * 24
            put(uc, heap + index * 4, node)
            put(uc, node + 0x14, priority)
        put(uc, node_base + 7 * 24 + 0x14, 99)
        run = call(uc, 0x491A70, ecx=heap, args=[node_base + selected * 24, new_priority])
        observed = {"heap_node_ids": [(get(uc, heap + i * 4) - node_base) // 24 for i in range(1, 7)],
                    "selected_priority": get(uc, node_base + selected * 24 + 0x14),
                    "heap_count": get(uc, heap + 0x1F8)}
        record("heap_priority_change", [selected, new_priority], run, observed,
               {"heap_node_ids": expected_ids, "selected_priority": new_priority, "heap_count": 6})

    for mode in ["same_region", "direct_edge", "detour", "disconnected", "different_group"]:
        for offset in [0, 1, 15, 31]:
            graph = {1: [2], 2: [3], 3: [4], 4: []}
            points = {1: (64, 64), 2: (64, 192), 3: (256, 192), 4: (256, 64)}
            if mode == "direct_edge":
                graph[1] = [4]
            if mode == "disconnected":
                graph[2] = []
            uc, points = machine(graph, points, groups={4: 2} if mode == "different_group" else None)
            first, last = (64 + offset, 64), (256 + offset, 64)
            map_point(uc, first, 1)
            map_point(uc, last, 1 if mode == "same_region" else 4)
            run = call(uc, 0x496930, ecx=first[0], edx=first[1], args=last)
            if mode == "different_group":
                expected = 9999
            elif mode in {"same_region", "direct_edge", "disconnected"}:
                expected = distance(first, last) // 32
            else:
                expected = (distance(first, points[2]) + distance(points[2], points[3])
                            + distance(points[3], last)) // 32
            record("placement_distance_connected_and_unreachable", [mode, offset], run,
                   {"returned": run["returned"]}, {"returned": expected})

    for length in [3, 49, 50, 51, 52, 60]:
        graph = {i: ([i + 1] if i < length else []) for i in range(1, length + 1)}
        points = {i: (i * 64, 32 if i % 2 else 128) for i in graph}
        uc, points = machine(graph, points)
        first, last = points[1], points[length]
        map_point(uc, first, 1)
        map_point(uc, last, length)
        path_points = [first] + [points[i] for i in range(2, min(length, 50))] + [last]
        expected = sum(distance(a, b) for a, b in zip(path_points, path_points[1:])) // 32
        run = call(uc, 0x496930, ecx=first[0], edx=first[1], args=last)
        record("placement_distance_truncated_source_route", [length], run,
               {"returned": run["returned"]}, {"returned": expected})

    for mode in ["same_region", "different_group", "disconnected", "detour"]:
        for offset in [0, 1, 15, 31]:
            graph = {1: [2], 2: [3], 3: [4], 4: []}
            points = {1: (1536, 1536), 2: (1632, 1632), 3: (1824, 1632), 4: (1728, 1536)}
            if mode == "disconnected":
                graph[1] = []
            first, last = (1536 + offset, 1536), (1728 + offset, 1536)
            if mode == "different_group":
                expected_distance = 9999
            elif mode in {"same_region", "disconnected"}:
                expected_distance = 6
            else:
                expected_distance = (distance(first, points[2]) + distance(points[2], points[3])
                                     + distance(points[3], last)) // 32
            for limit in [expected_distance - 1, expected_distance, expected_distance + 1, 0xFFFFFFFF]:
                uc, unused = machine(graph, points, groups={4: 2} if mode == "different_group" else None)
                map_point(uc, first, 1)
                map_point(uc, last, 1 if mode == "same_region" else 4)
                grid_address, index = 0x818000, 32 * 64 + 38
                put(uc, grid_address + index, 7, 1)
                seed = first[0] | (first[1] << 16)
                run = call(uc, 0x4976D0, ecx=grid_address, edx=limit, args=[seed])
                expected_value = 7 if expected_distance <= limit else 0
                expected_grid = bytearray(4096)
                expected_grid[index] = expected_value
                record("radius_filter_actual_connected_search", [mode, offset, limit, expected_distance], run,
                       {"candidate_value": get(uc, grid_address + index, 1),
                        "whole_grid_matches": bytes(uc.mem_read(grid_address, 4096)) == bytes(expected_grid)},
                       {"candidate_value": expected_value, "whole_grid_matches": True})

    def enemy_fixture(uc, points, owners=(7, 7), current=3, other=7):
        units, sprites = [0x820000, 0x820200], [0x822000, 0x822100]
        for player in range(12):
            put(uc, 0x534914 + current * 12 + player, int(player == current), 1)
        put(uc, 0x665B08 + other * 4, units[0])
        for index, (unit, sprite, owner) in enumerate(zip(units, sprites, owners)):
            put(uc, unit + 0xC, sprite)
            put(uc, unit + 0x4C, owner, 1)
            put(uc, unit + 0x4D, 1, 1)
            put(uc, unit + 0x64, 0, 2)
            put(uc, unit + 0x6C, units[1] if index == 0 else 0)
            put(uc, sprite + 0xA, owner, 1)
            put(uc, sprite + 0x14, points[index][0], 2)
            put(uc, sprite + 0x16, points[index][1], 2)
            map_point(uc, points[index], 7 + index)
        return units, sprites

    enemy_modes = ["ordinary", "null_sprite", "death_order", "subunit_property", "allied_owner",
                   "neutral_sprite_hostile", "neutral_sprite_allied", "blocked_region",
                   "other_region_literal", "incomplete", "hidden", "disabled", "hallucination"]
    for mode in enemy_modes:
        graph = {7: [], 8: []}
        uc, unused = machine(graph)
        points = [(320, 64), (512, 64)]
        units, sprites = enemy_fixture(uc, points)
        wanted = units[0]
        if mode == "null_sprite":
            put(uc, units[0] + 0xC, 0)
            wanted = units[1]
        elif mode == "death_order":
            put(uc, units[0] + 0x4D, 0, 1)
            wanted = units[1]
        elif mode == "subunit_property":
            put(uc, units[0] + 0x64, 1, 2)
            put(uc, 0x521660 + 4, 0x10)
            wanted = units[1]
        elif mode == "allied_owner":
            put(uc, units[0] + 0x4C, 3, 1)
            wanted = units[1]
        elif mode.startswith("neutral_sprite"):
            put(uc, units[0] + 0x4C, 11, 1)
            put(uc, sprites[0] + 0xA, 3 if mode.endswith("allied") else 7, 1)
            if mode.endswith("allied"):
                wanted = units[1]
        elif mode.endswith("region"):
            put(uc, region_address(7), 0x1FFD, 2)
            wanted = units[1]
        elif mode == "other_region_literal":
            put(uc, region_address(7), 0x1FFC, 2)
        elif mode in {"hidden", "disabled", "hallucination"}:
            put(uc, units[0] + 0xDC, {"hidden": 0x100, "disabled": 0x400, "hallucination": 0x40000000}[mode])
        run = call(uc, 0x48DFF0, ecx=3, edx=7, args=[64, 64])
        record("nearest_enemy_actual_gates", [mode], run,
               {"returned": run["returned"]}, {"returned": wanted})
    for points, wanted_index in [([(320, 64), (128, 64)], 1),
                                 ([(320, 64), (64, 320)], 0),
                                 ([(320, 64), (320, 64)], 0)]:
        uc, unused = machine({7: [], 8: []})
        units, unused = enemy_fixture(uc, points)
        run = call(uc, 0x48DFF0, ecx=3, edx=7, args=[64, 64])
        record("nearest_enemy_distance_and_list_tie", points, run,
               {"returned": run["returned"]}, {"returned": units[wanted_index]})
    uc, unused = machine({1: []})
    run = call(uc, 0x48DFF0, ecx=3, edx=7, args=[64, 64])
    record("nearest_enemy_empty_list", [], run, {"returned": run["returned"]}, {"returned": 0})

    for mode in ["equal", "too_short_attempt", "different_group", "disconnected", "zero_rank", "short_path"]:
        for minimum in [0, 1, 2, 3]:
            graph = {1: [2], 2: [3], 3: [4], 4: []}
            if mode == "disconnected":
                graph[2] = []
            if mode == "short_path":
                graph[1] = [4]
            groups = {4: 2} if mode == "different_group" else None
            uc, points = machine(graph, groups=groups)
            put(uc, OUT + 4, 0xDEADBEEF)
            attempt = 1 if mode == "too_short_attempt" else 6
            target = 1 if mode == "equal" else 4
            run = call(uc, 0x40E190, ecx=1, edx=target, args=[attempt, OUT, OUT + 4, minimum])
            if mode in {"equal", "too_short_attempt", "short_path"} or mode == "zero_rank" and minimum == 3:
                status = 4
            elif mode in {"different_group", "disconnected"}:
                status = 2
            else:
                status = 3
            record("repeated_cut_complete_no_allocation", [mode, minimum], run,
                   {"returned": run["returned"], "status": get(uc, OUT + 4), "cut": get(uc, OUT, 2)},
                   {"returned": 0, "status": status, "cut": 0xBEEF})

    for mode in ["allied", "empty_lists", "zero_rank_routes", "different_groups"]:
        graph = {1: [2], 2: [3], 3: [7], 7: [8], 8: []}
        points = {1: (64, 64), 2: (128, 64), 3: (192, 64), 7: (320, 64), 8: (512, 64)}
        uc, points = machine(graph, points, groups={7: 2, 8: 2} if mode == "different_groups" else None)
        put(uc, 0x6A2E84, 17)
        uc.mem_write(0x6A5D70, b"\xBE\xEF" * 24)
        if mode != "empty_lists":
            for other in range(8):
                unit, sprite = 0x824000 + other * 0x200, 0x826000 + other * 0x100
                put(uc, 0x665B08 + other * 4, unit)
                put(uc, unit + 0xC, sprite)
                put(uc, unit + 0x4C, other, 1)
                put(uc, unit + 0x4D, 1, 1)
                put(uc, sprite + 0xA, other, 1)
                put(uc, sprite + 0x14, 320 + 32 * other, 2)
                put(uc, sprite + 0x16, 64, 2)
                map_point(uc, (320 + 32 * other, 64), 7)
            put(uc, 0x534914 + 3 * 12 + 3, 1, 1)
        if mode == "allied":
            uc.mem_write(0x534914 + 3 * 12, b"\x01" * 12)
        run = call(uc, 0x496E10, ecx=3, edx=1, args=[3])
        record("anchor_producer_complete_zero_cuts", [mode], run,
               {"count": get(uc, 0x6A2E84), "stored_ids_unchanged": bytes(uc.mem_read(0x6A5D70, 48)) == b"\xBE\xEF" * 24},
               {"count": 0, "stored_ids_unchanged": True})

    result = {"target_sha256": TARGET_HASH, "version": "1.17.0.1",
        "probe_script_sha256": own_hash,
        "runtime": {"python": platform.python_version(), "host_machine": platform.machine(),
                    "unicorn": unicorn.__version__, "capstone": capstone.__version__},
        "scope": "complete original region path/cut calls; supplied synthetic region graphs",
        "allowlist": [[hex(lo), hex(hi)] for lo, hi in BLOCKS.values()],
        "instruction_limit_per_call": 2000000,
        "case_count": len(cases), "passed_cases": len(cases), "failed_cases": 0,
        "all_expected_results_matched": True, "cases": cases,
        "kind_counts": dict(Counter(case["kind"] for case in cases)),
        "complete_original_game_executed": False,
        "limits": "Terrain region generation, adjacency production, allocator-dependent repeated cuts and movement orders remain separate."}
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({"cases": len(cases), "all_matched": True, "out": str(args.out)}))


if __name__ == "__main__":
    main()
