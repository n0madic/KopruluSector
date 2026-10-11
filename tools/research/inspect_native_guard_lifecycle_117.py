#!/usr/bin/env python3
"""Recover guard pool/list and periodic rebuild behavior in the exact 1.17 EXE.

Static by default. Explicit execution uses private emulated memory, original
reviewed callees and an instruction allowlist, without helper stubs or APIs.
"""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import struct

import capstone
import pefile
import inspect_native_default_production_117 as default
import inspect_native_morph_selection_117 as selectors

TARGET_HASH = default.TARGET_HASH
PLAYER_BASE, STRIDE = default.PLAYER_BASE, default.STRIDE
WITNESSES = {
    "guard_pool_initialize": (0x4B1A60, 0x4B1AFD),
    "guard_global_lists_initialize": (0x4B2640, 0x4B26EB),
    "guard_precreate": (0x4B22A0, 0x4B231F),
    "guard_remove_parentless": (0x4B21F0, 0x4B226B),
    "guard_periodic_rebuild": (0x4B2320, 0x4B2537),
    "guard_request_insert": (0x482A40, 0x482AD6),
    "guard_home_ai_region": (0x4B0930, 0x4B0951),
    "guard_detach_and_death_increment": (0x4B26F0, 0x4B271D),
    "guard_attach_parent_home": (0x4B25B0, 0x4B25CA),
    "nearest_available_military": (0x4AF750, 0x4AF80A),
    "training_word_write": (0x482EB0, 0x482EBF),
    "training_word_take": (0x482BE0, 0x482C08),
}
STATIC_ONLY = {
    "guard_periodic_outer_player_service": (0x483400, 0x4834AD),
    "guard_remove_parentless_first_town_caller": (0x487030, 0x487082),
    "aiscript_training_writer_caller": (0x493B73, 0x493BE6),
}


def dependency_hashes():
    mods = (default, selectors, default.dispatch, default.production,
            default.dispatch.training, default.dispatch.research,
            default.dispatch.training.unit_reqs,
            default.dispatch.research.requirements)
    return {Path(m.__file__).name: hashlib.sha256(Path(m.__file__).read_bytes()).hexdigest() for m in mods}


def execute_probes(data, pe):
    if platform.machine().lower() not in ("x86_64", "amd64"):
        raise RuntimeError("Use the reviewed x86_64 Rosetta runtime")
    import unicorn
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_ESP

    unit, ai, sprite, regions, pool, free_node = 0x800000, 0x800400, 0x800800, 0x810000, 0x830000, 0x840000
    map_base, stack, stop = 0xB00000, 0x900000, 0xA00000
    image = pe.get_memory_mapped_image()
    size = (pe.OPTIONAL_HEADER.SizeOfImage + 4095) & ~4095
    allowed = list(WITNESSES.values()) + list(default.WITNESSES.values()) + list(selectors.WITNESSES.values())
    for mod in (default.production, default.dispatch, default.dispatch.training,
                default.dispatch.training.unit_reqs, default.dispatch.research,
                default.dispatch.research.requirements):
        allowed += list(mod.WITNESSES.values())
    cases = []

    def w32(u, addr, value):
        u.mem_write(addr, struct.pack("<I", value & 0xFFFFFFFF))

    def w16(u, addr, value):
        u.mem_write(addr, struct.pack("<H", value & 65535))

    def w8(u, addr, value):
        u.mem_write(addr, bytes([value & 255]))

    def r32(u, addr):
        return struct.unpack("<I", u.mem_read(addr, 4))[0]

    def r16(u, addr):
        return struct.unpack("<H", u.mem_read(addr, 2))[0]

    def r8(u, addr):
        return u.mem_read(addr, 1)[0]

    def new():
        u = Uc(UC_ARCH_X86, UC_MODE_32)
        u.mem_map(0x400000, size)
        u.mem_write(0x400000, image)
        u.mem_map(unit, 0x60000)
        u.mem_map(map_base, 0x60000)
        u.mem_map(stack, 0x2000)
        u.mem_map(stop, 0x1000)
        u.mem_write(PLAYER_BASE, bytes(STRIDE * 8))
        w32(u, 0x6BE610 + 7 * 8, pool)
        w32(u, 0x6BE614 + 7 * 8, 0)
        w32(u, pool + 0x7D00, free_node)
        w32(u, 0x69B584, map_base)
        w32(u, 0x6B1A74 + 28, regions)
        w32(u, 0x665B08 + 28, 0)
        w16(u, map_base + 0xC + 2 * (3 * 256 + 2), 3)
        w16(u, map_base + 0xC + 2 * (3 * 256 + 3), 4)
        w32(u, unit + 0x134, ai)
        w8(u, ai + 8, 3)
        w32(u, unit + 0xC, sprite)
        w8(u, unit + 0x4D, 1)
        w8(u, unit + 0x4C, 7)
        w32(u, unit + 0xDC, 1)
        w32(u, 0x5349D8, 100)
        for ident in range(228):
            w16(u, 0x523520 + 2 * ident, 10)
            w16(u, 0x5236E8 + 2 * ident, 5)
            w16(u, 0x5238B0 + 2 * ident, 150)
            w8(u, 0x523E10 + ident, 2)
            w32(u, 0x521660 + 4 * ident, 0)
        return u

    def call(u, start, ecx=7, edx=0, args=()):
        reached, instructions, trace = False, 0, []

        def hook(m, address, length, _):
            nonlocal reached, instructions
            if address == stop:
                reached = True
                m.emu_stop()
                return
            if not any(lo <= address and address + length <= hi for lo, hi in allowed):
                raise RuntimeError(f"Unreviewed original instruction {address:#x}")
            instructions += 1
            if address in tuple(a for a, _ in WITNESSES.values()) + (0x482040, 0x482130, 0x4AEA70):
                trace.append(hex(address))

        h = u.hook_add(UC_HOOK_CODE, hook)
        u.mem_write(stack + 0x1800, struct.pack("<" + "I" * (len(args) + 1), stop, *args))
        u.reg_write(UC_X86_REG_ESP, stack + 0x1800)
        u.reg_write(UC_X86_REG_ECX, ecx)
        u.reg_write(UC_X86_REG_EDX, edx)
        u.emu_start(start, stop + 1, count=150000)
        u.hook_del(h)
        if not reached:
            raise RuntimeError("No normal return within150000 original instructions")
        return u.reg_read(UC_X86_REG_EAX), instructions, trace

    def observe(label, inputs, ex, got, expected):
        if got != expected:
            raise AssertionError((label, inputs, got, expected))
        cases.append({"label": label, "fixture_inputs": inputs, "observed": got,
                      "expected_from_static_contract": expected, "matched": True,
                      "original_instruction_count": ex[1], "native_call_trace": ex[2],
                      "execution_scope": "complete reviewed functions and actual callees for fixture path",
                      "helper_stubs": []})

    def guard(u, offset=0, ident=0, deaths=0, timestamp=0, parent=0):
        g = pool + offset
        w8(u, g + 8, 1)
        w8(u, g + 9, deaths)
        w32(u, g + 0xC, parent)
        w16(u, g + 0x10, ident)
        for field, value in ((0x12, 64), (0x14, 96), (0x16, 64), (0x18, 96)):
            w16(u, g + field, value)
        w32(u, g + 0x1C, timestamp)
        return g

    def heap(u):
        p = PLAYER_BASE + 7 * STRIDE
        return sorted([{"priority": r8(u, p + 0x18 + j * 8), "kind": r8(u, p + 0x19 + j * 8),
                        "id": r16(u, p + 0x1A + j * 8), "context": r32(u, p + 0x1C + j * 8)}
                       for j in range(r8(u, p + 0x210))], key=lambda e: (e["id"], e["priority"]))

    def demand(u):
        return [r32(u, PLAYER_BASE + 7 * STRIDE + 4 * j) for j in range(3)]

    u = new()
    u.mem_write(pool, bytes([0xAA]) * 0x7D04)
    ex = call(u, 0x4B1A60, pool)
    expected = bytearray([0xAA]) * 0x7D04
    for j in range(1000):
        at = j * 32
        struct.pack_into("<II", expected, at, pool + at + 32 if j < 999 else 0, pool + at - 32 if j else 0)
        expected[at + 8] = 0
        for field in (0xC, 0x10, 0x14, 0x1C):
            struct.pack_into("<I", expected, at + field, 0)
        struct.pack_into("<H", expected, at + 0x18, 0)
    struct.pack_into("<I", expected, 0x7D00, pool)
    actual = bytes(u.mem_read(pool, 0x7D04))
    assert actual == expected
    observe("full1000-slot pool initializer preserves unassigned padding/death bytes", {"initial_byte": 170, "pool": hex(pool)}, ex,
            {"full_bytes_sha256": hashlib.sha256(actual).hexdigest(), "free_head": r32(u, pool + 0x7D00), "slots": 1000},
            {"full_bytes_sha256": hashlib.sha256(expected).hexdigest(), "free_head": pool, "slots": 1000})
    u = new()
    ex = call(u, 0x4B2640)
    observe("global guard initialization shares one pool across8 player lists", {}, ex,
            [[r32(u, 0x6BE610 + j * 8), r32(u, 0x6BE614 + j * 8)] for j in range(8)], [[0x6B6908, 0]] * 8)
    for count in (0, 1, 2):
        for old_head in (0, pool + 0x100):
            u = new()
            w32(u, pool + 0x7D00, pool if count else 0)
            w32(u, pool, pool + 32 if count == 2 else 0)
            w32(u, pool + 36, pool)
            w32(u, 0x6BE614 + 56, old_head)
            w32(u, pool + 0x1C, 77)
            w8(u, pool + 9, 88)
            ex = call(u, 0x4B22A0, 7, 0x10005, [0x10001, 0xFFFFFFFC])
            got = {"head": r32(u, 0x6BE614 + 56), "free_head": r32(u, pool + 0x7D00), "timestamp": r32(u, pool + 0x1C), "deaths": r8(u, pool + 9)}
            want = {"head": pool if count else old_head, "free_head": pool + 32 if count == 2 else 0, "timestamp": 77, "deaths": 0 if count else 88}
            if count:
                got.update({"next": r32(u, pool), "prev": r32(u, pool + 4), "type": r8(u, pool + 8), "parent": r32(u, pool + 0xC), "id": r16(u, pool + 0x10), "positions": [r16(u, pool + f) for f in (0x12, 0x14, 0x16, 0x18)], "old_head_prev": r32(u, old_head + 4) if old_head else 0})
                want.update({"next": old_head, "prev": 0, "type": 1, "parent": 0, "id": 5, "positions": [1, 65532, 1, 65532], "old_head_prev": pool if old_head else 0})
            observe("precreate pops free head and prepends guard without resetting timestamp", {"free_slots": count, "old_head": old_head, "id": 0x10005, "x": 0x10001, "y": 0xFFFFFFFC, "old_timestamp": 77}, ex, got, want)
    for parents in ([], [0], [unit], [0, unit, 0], [unit, 0, unit]):
        u = new()
        w32(u, 0x6BE614 + 56, pool if parents else 0)
        for j, parent in enumerate(parents):
            g = guard(u, 32 * j, parent=parent, timestamp=j + 9)
            w32(u, g, pool + 32 * (j + 1) if j + 1 < len(parents) else 0)
            w32(u, g + 4, pool + 32 * (j - 1) if j else 0)
        ex = call(u, 0x4B21F0)
        retained = [pool + j * 32 for j, v in enumerate(parents) if v]
        removed = [pool + j * 32 for j, v in enumerate(parents) if not v]
        observe("reset removes only parentless list nodes preserving attached guards", {"parents": parents}, ex,
                {"head": r32(u, 0x6BE614 + 56), "free_head": r32(u, pool + 0x7D00), "retained_links": [[r32(u, g), r32(u, g + 4)] for g in retained], "timestamps": [r32(u, pool + j * 32 + 0x1C) for j in range(len(parents))]},
                {"head": retained[0] if retained else 0, "free_head": removed[-1] if removed else free_node, "retained_links": [[retained[j + 1] if j + 1 < len(retained) else 0, retained[j - 1] if j else 0] for j in range(len(retained))], "timestamps": [j + 9 for j in range(len(parents))]})
    for ty in (0, 1, 4):
        for deaths in (0, 99, 100, 255):
            u = new()
            g = guard(u, deaths=deaths, parent=unit)
            w8(u, g + 8, ty)
            w32(u, unit + 0x134, g)
            ex = call(u, 0x4B26F0, unit)
            observe("detach guard increments deaths only below100 without unlinking or clearing unitAI", {"ai_type": ty, "deaths": deaths}, ex,
                    {"return": ex[0], "parent": r32(u, g + 0xC), "deaths": r8(u, g + 9), "unitAI": r32(u, unit + 0x134)},
                    {"return": int(ty == 1), "parent": 0 if ty == 1 else unit, "deaths": deaths + 1 if ty == 1 and deaths < 100 else deaths, "unitAI": g})
    for ident in (0, 30, 44, 62, 63, 68, 103):
        for deaths in (0, 1, 2):
            added = 150 if ident in (44, 62, 63, 68, 103) else 0
            deadline = 1 + (300 if deaths else 5 + (150 + added) // 15)
            for elapsed in (deadline, deadline + 1):
                u = new()
                g = guard(u, ident=ident, deaths=deaths, timestamp=1)
                w32(u, 0x6BE614 + 56, g)
                w32(u, 0x5349D8, elapsed)
                ex = call(u, 0x4B2320)
                emitted = elapsed > deadline
                expected_entries = []
                parents = {44: [43], 62: [43], 63: [61, 61], 68: [67, 67], 103: [38]}.get(ident, [])
                if emitted:
                    expected_entries = [{"priority": 50, "kind": 2, "id": 5 if ident == 30 else ident, "context": g}] + [{"priority": 49, "kind": 1, "id": v, "context": regions + 3 * 0x34} for v in parents]
                    expected_entries.sort(key=lambda e: (e["id"], e["priority"]))
                amount = 1 + len(parents) if emitted else 0
                observe("periodic retry deadline branches and special build-time additions", {"id": ident, "deaths": deaths, "previous_update": 1, "all_build_times": 150, "deadline": deadline, "elapsed": elapsed}, ex,
                        {"heap": heap(u), "timestamp": r32(u, g + 0x1C), "demand": demand(u)},
                        {"heap": expected_entries, "timestamp": elapsed if emitted else 1, "demand": [10 * amount, 5 * amount, 2 * amount]})
    for throttle, elapsed, blocked in ((0, 100, False), (100, 129, False), (100, 130, False), (200, 100, False), (0, 100, True)):
        u = new()
        p = PLAYER_BASE + 7 * STRIDE
        g = guard(u)
        w32(u, 0x6BE614 + 56, g)
        w32(u, p + 0x224, throttle)
        w32(u, 0x5349D8, elapsed)
        if blocked:
            w8(u, p + 0x3F0, 255)
        ex = call(u, 0x4B2320)
        accepted = not throttle or (elapsed - throttle) & 0xFFFFFFFF >= 30
        observe("guard scheduler updates timestamp even throttle or limit rejects insertion", {"throttle_field224": throttle, "elapsed": elapsed, "unit_limit255": blocked}, ex,
                {"heap": heap(u), "demand": demand(u), "timestamp": r32(u, g + 0x1C)},
                {"heap": [{"priority": 50, "kind": 2, "id": 0, "context": g}] if accepted and not blocked else [], "demand": [10, 5, 2] if accepted else [0, 0, 0], "timestamp": elapsed})
    for state, target1, target2, flags in [(v, 0, 0, 0) for v in range(11)] + [(0, 1, 0, 0), (0, 0, 1, 0), (0, 0, 0, 0x20)]:
        u = new()
        g = guard(u)
        w32(u, 0x6BE614 + 56, g)
        region = regions + 3 * 0x34
        w8(u, region + 5, state)
        w32(u, region + 0x1C, target1)
        w32(u, region + 0x20, target2)
        w8(u, region + 8, flags)
        ex = call(u, 0x4B2320)
        accepted = state != 3 and not target1 and not target2 and not flags
        observe("periodic region rejection leaves guard timestamp and demand unchanged", {"state": state, "target1": target1, "target2": target2, "flags": flags}, ex,
                {"heap": heap(u), "timestamp": r32(u, g + 0x1C), "demand": demand(u)},
                {"heap": [{"priority": 50, "kind": 2, "id": 0, "context": g}] if accepted else [], "timestamp": 100 if accepted else 0, "demand": [10, 5, 2] if accepted else [0, 0, 0]})
    for ident, previous, deaths, build, extra, elapsed in ((30, 1, 0, 150, 0, 17), (0, 1, 0, 151, 0, 17), (44, 1, 0, 65535, 65535, 8744), (44, 1, 0, 65535, 65535, 8745), (0, 0xFFFFFFFF, 1, 150, 0, 299), (0, 0xFFFFFFFF, 1, 150, 0, 300)):
        u = new()
        g = guard(u, ident=ident, deaths=deaths, timestamp=previous)
        w32(u, 0x6BE614 + 56, g)
        w16(u, 0x5238B0 + 2 * (5 if ident == 30 else ident), build)
        if ident == 30:
            w16(u, 0x5238B0 + 2 * 30, 999)
        if ident == 44:
            w16(u, 0x5238B0 + 2 * 43, extra)
        w32(u, 0x5349D8, elapsed)
        deadline = (previous + (300 if deaths else 5 + (build + extra) // 15)) & 0xFFFFFFFF
        ex = call(u, 0x4B2320)
        accepted = elapsed > deadline
        observe("retry duration uses u16 time sum floor15 and unsigned wrapped deadline", {"id": ident, "previous": previous, "deaths": deaths, "build_time": build, "extra_time": extra, "elapsed": elapsed, "deadline": deadline}, ex,
                {"timestamp": r32(u, g + 0x1C), "heap_count": len(heap(u))}, {"timestamp": elapsed if accepted else previous, "heap_count": (2 if ident == 44 else 1) if accepted else 0})
    for campaign in (False, True):
        for deaths in (2, 3, 100):
            for attached, hallucination in ((False, False), (True, False), (True, True)):
                u = new()
                p = PLAYER_BASE + 7 * STRIDE
                g = guard(u, deaths=deaths, parent=unit if attached else 0)
                w32(u, 0x6BE614 + 56, g)
                w16(u, p + 0x218, 0x20 if campaign else 0)
                w32(u, unit + 0x134, g)
                w32(u, unit + 0xDC, 0x40000000 if hallucination else 1)
                w32(u, p + 0x18, 50 | (2 << 8))
                w32(u, p + 0x1C, g)
                w8(u, p + 0x210, 1)
                ex = call(u, 0x4B2320)
                recycled = campaign and deaths >= 3 or attached and hallucination
                observe("campaign death and hallucination recycling leaves parentAI and existing request context", {"campaign": campaign, "deaths": deaths, "attached": attached, "hallucination": hallucination}, ex,
                        {"head": r32(u, 0x6BE614 + 56), "free_head": r32(u, pool + 0x7D00), "unitAI": r32(u, unit + 0x134), "old_request_context": r32(u, p + 0x1C), "parent": r32(u, g + 0xC)},
                        {"head": 0 if recycled else g, "free_head": g if recycled else free_node, "unitAI": g, "old_request_context": g, "parent": unit if attached else 0})
    for elapsed in (0, 100):
        u = new()
        g = guard(u)
        w32(u, 0x6BE614 + 56, g)
        w32(u, 0x5349D8, elapsed)
        call(u, 0x4B2320)
        ex = call(u, 0x4B2320)
        n = 2 if elapsed == 0 else 1
        observe("repeated elapsed0 visits retain zero marker and do not deduplicate requests", {"elapsed": elapsed, "visits": 2}, ex,
                {"requests": len(heap(u)), "timestamp": r32(u, g + 0x1C), "demand": demand(u)}, {"requests": n, "timestamp": elapsed, "demand": [10 * n, 5 * n, 2 * n]})
    for ident in (37, 47):
        u = new()
        g = guard(u, ident=ident)
        w32(u, 0x6BE614 + 56, g)
        ex = call(u, 0x4B2320)
        observe("paired-birth guard falls back to request when actual player-unit list empty", {"id": ident, "player_units": []}, ex,
                {"heap": heap(u), "timestamp": r32(u, g + 0x1C)}, {"heap": [{"priority": 50, "kind": 2, "id": ident, "context": g}], "timestamp": 100})
    u = new()
    g = guard(u, ident=44)
    w16(u, g + 0x12, 96)
    w32(u, 0x6BE614 + 56, g)
    ex = call(u, 0x4B2320)
    observe("guard rebuild eligibility uses other_home but morph parents use home region", {"id": 44, "home": [96, 96], "other_home": [64, 96], "home_region": 4, "other_region": 3}, ex, heap(u),
            [{"priority": 49, "kind": 1, "id": 43, "context": regions + 4 * 0x34}, {"priority": 50, "kind": 2, "id": 44, "context": g}])
    for value in (0, 5, 30, 228, 65535, 65536, 0xFFFFFFFF):
        u = new()
        ex = call(u, 0x482EB0, 7, value)
        observe("explicit training writer LOW16 unit+1", {"requested": value}, ex, r16(u, PLAYER_BASE + 7 * STRIDE + 0x220), (value + 1) & 65535)
    u = new()
    g = guard(u, parent=0, timestamp=77)
    w16(u, g + 0x16, 0xFFFC)
    ex = call(u, 0x4B25B0, g, unit)
    observe("guard attach sets parent and copies other_home while retaining timestamp/deaths", {"other_home": [65532, 96], "timestamp": 77}, ex,
            {"home": [r16(u, g + 0x12), r16(u, g + 0x14)], "parent": r32(u, g + 0xC), "unitAI": r32(u, unit + 0x134), "timestamp": r32(u, g + 0x1C)},
            {"home": [65532, 96], "parent": unit, "unitAI": g, "timestamp": 77})
    return {"format_version": 1, "target_sha256": TARGET_HASH,
            "probe_script_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
            "dependency_script_sha256": dependency_hashes(),
            "runtime_versions": {"python": platform.python_version(), "unicorn": unicorn.__version__, "capstone": capstone.__version__, "pefile": pefile.__version__, "architecture": platform.machine(), "system": platform.system()},
            "original_code_executed": True, "complete_game_executed": False, "helper_stubs": [],
            "allowlisted_preferred_va_ranges": [[hex(a), hex(b)] for a, b in allowed],
            "all_matched": True, "case_count": len(cases), "cases": cases}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--execute-isolated-original-code", action="store_true")
    parser.add_argument("--probe-out", type=Path)
    args = parser.parse_args()
    if args.execute_isolated_original_code and args.probe_out is None:
        parser.error("--probe-out is required for explicit execution")
    data = args.exe.read_bytes()
    if hashlib.sha256(data).hexdigest() != TARGET_HASH:
        parser.error("Wrong EXE hash")
    pe = pefile.PE(data=data)
    c = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    report = {"format_version": 1, "target_sha256": TARGET_HASH,
              "inspector_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
              "dependency_script_sha256": dependency_hashes(), "original_code_executed": False,
              "complete_game_executed": False, "witnesses": {}, "static_only_witnesses": {},
              "unresolved_selected_helpers": ["nearest available military selected candidate strength4a9a20/distance405cf0", "found-unit48dbc0 AI detach and42a2c0 order issue", "callers' broader state and table maintenance"]}
    for group, ranges in (("witnesses", WITNESSES), ("static_only_witnesses", STATIC_ONLY)):
        for name, (start, end) in ranges.items():
            raw = pe.get_data(start - 0x400000, end - start)
            report[group][name] = {"start_preferred_va": hex(start), "end_exclusive_preferred_va": hex(end), "bytes_sha256": hashlib.sha256(raw).hexdigest(),
                                   "instructions": [{"address": hex(i.address), "bytes": i.bytes.hex(), "mnemonic": i.mnemonic, "operands": i.op_str} for i in c.disasm(raw, start)]}
    targets = struct.unpack("<5I", pe.get_data(0x4B2538 - 0x400000, 20))
    index = pe.get_data(0x4B254C - 0x400000, 60)
    report["extra_build_time_switch"] = {"minimum_unit": 44, "maximum_unit": 103,
        "targets_preferred_va": "0x4b2538", "index_preferred_va": "0x4b254c",
        "unit_target_map": {str(44 + j): hex(targets[v]) for j, v in enumerate(index)}}
    text_section = next(s for s in pe.sections if s.Name.startswith(b".text"))
    c.skipdata = True
    direct, absolute = [], []
    for i in c.disasm(text_section.get_data(), 0x400000 + text_section.VirtualAddress):
        if i.mnemonic in ("call", "jmp") and i.op_str in ("0x4b2320", "0x4b22a0", "0x4b21f0", "0x482eb0"):
            direct.append({"address": hex(i.address), "mnemonic": i.mnemonic, "target": i.op_str})
        if "0x67d288" in i.op_str:
            absolute.append({"address": hex(i.address), "mnemonic": i.mnemonic, "operands": i.op_str})
    report["linear_text_direct_reference_candidates"] = direct
    report["training_word_absolute_reference_candidates"] = absolute
    report["reference_scan_limitations"] = "Linear .text disassembly with skipdata supplies direct/immediate candidates; aliases, indirect calls and block initialization/serialization are not exhaustively recovered."
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(report, indent=2) + "\n")
    if args.execute_isolated_original_code:
        result = execute_probes(data, pe)
        args.probe_out.parent.mkdir(parents=True, exist_ok=True)
        args.probe_out.write_text(json.dumps(result, indent=2) + "\n")
        print(f"{result['case_count']} native guard lifecycle cases passed; no stubs")
    else:
        print("Wrote static guard lifecycle witnesses; original code not executed")


if __name__ == "__main__":
    main()
