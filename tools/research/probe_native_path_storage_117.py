#!/usr/bin/env python3
"""Execute original path packing, finite-pool reuse and waypoint consumers.

Explicit search-output/path/unit records run through complete original helpers;
no path search, heap allocation, Windows imports or whole movement executes.
"""

import argparse
from collections import Counter
import itertools
import json
from pathlib import Path
import platform
import struct

import capstone
import pefile
import inspect_terrain_assets as assets
import probe_native_region_paths_117 as regions
import probe_native_trigger_unit_counts_117 as counts

TARGET_HASH = regions.TARGET_HASH
CONTEXT, UNIT, SPRITE, RESTORED, POOL = 0x800000, 0x810000, 0x810200, 0x811000, 0x820000
STACK, STOP, MAP = 0x900000, 0xA00000, 0xB00000
BLOCKS = {"path_length_allocator": (0x42C840, 0x42C8A0),
          "path_pack": (0x48E730, 0x48E7D1), "path_unpack": (0x48E7E0, 0x48E879),
          "path_unit_release": (0x48E880, 0x48E8CE), "path_pool_pop": (0x48E8D0, 0x48E906),
          "retry_timer": (0x48E950, 0x48E96F), "retry_timer_set": (0x48E970, 0x48E97E),
          "waypoint_reuse": (0x48EE40, 0x48EF0A),
          "heading_difference": (0x40B990, 0x40B9AB),
          "approximate_distance": regions.BLOCKS["approximate_distance"],
          "region_at_pixel": regions.BLOCKS["region_at_pixel"],
          "memmove_rep_entry": counts.BLOCKS["memmove_rep_entry"],
          "memmove_return": counts.BLOCKS["memmove_return"]}


def adjusted_lengths(long_length, short_length):
    payload_bytes = long_length * 2 + short_length * 4
    if payload_bytes > 96:
        return (1, 23) if short_length >= 24 else ((24 - short_length) * 2, short_length)
    return long_length, short_length


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
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
    if args.out is None:
        parser.error("Execution requires --out")
    import unicorn
    from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_ESP, UC_X86_REG_EIP
    uc = unicorn.Uc(unicorn.UC_ARCH_X86, unicorn.UC_MODE_32)
    for address, size in ((base, (pe.OPTIONAL_HEADER.SizeOfImage + 4095) & ~4095), (CONTEXT, 0x100000),
                          (STACK, 0x10000), (STOP, 0x1000), (MAP, 0x100000)):
        uc.mem_map(address, size)
    uc.mem_write(base, pe.get_memory_mapped_image())
    entries = {lo: name for name, (lo, hi) in BLOCKS.items()}
    cases = []

    def put(address, value, width=4):
        uc.mem_write(address, (value & ((1 << (width * 8)) - 1)).to_bytes(width, "little"))

    def get(address, width=4):
        return int.from_bytes(uc.mem_read(address, width), "little")

    put(0x6D319C, 2)  # Select the genuine reviewed REP copy branch.
    put(0x69B584, MAP)
    put(0x5264B4, 256, 2)
    uc.mem_write(MAP + 0xC, struct.pack("<H", 1) * 65536)
    put(UNIT + 0xC, SPRITE)
    uc.mem_write(SPRITE + 0x14, struct.pack("<2H", 100, 200))

    def call(entry, arguments=(), ecx=CONTEXT, edx=0):
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

    def pool_setup(available):
        uc.mem_write(POOL, bytes([0xA5]) * 512)
        put(0x69B574, POOL)
        put(0x69B570, POOL if available else 0)
        for index in range(available):
            put(POOL + index * 128, index + 2 if index + 1 < available else 0)
            put(POOL + index * 128 + 4, index + 1)
        put(UNIT + 0x100, 0)

    for long_length, short_length, available in itertools.product(range(51), range(51), (0, 1)):
        pool_setup(available)
        put(CONTEXT + 0x86, long_length, 2)
        put(CONTEXT + 0x152, short_length, 2)
        expected_long, expected_short = adjusted_lengths(long_length, short_length)
        run = call(0x42C840, ecx=CONTEXT + 0x86, edx=CONTEXT + 0x152)
        assert (get(CONTEXT + 0x86, 2), get(CONTEXT + 0x152, 2)) == (expected_long, expected_short)
        assert run["returned"] == (POOL if available else 0)
        assert get(0x69B570) == 0
        cases.append({"name": "allocation_{}_{}_{}".format(long_length, short_length, available), "passed": True,
            "inputs": {"long_length": long_length, "short_length": short_length, "available_records": available},
            "runs": [run], "observed": {"long_length": expected_long, "short_length": expected_short,
                "allocated": bool(available), "free_head": 0}, "oracle": "96-byte payload capacity and finite original free-list pop"})
    print(json.dumps({"phase": "all0..50 lengths", "cases": len(cases)}), flush=True)

    for size, available in itertools.product((-1, 0, 32, 128, 129, 0x7FFFFFFF), (0, 1, 2)):
        pool_setup(available)
        expected = POOL if size <= 128 and available else 0
        before = get(0x69B570)
        run = call(0x48E8D0, ecx=size)
        assert run["returned"] == expected
        expected_head = POOL + 128 if expected and available == 2 else 0 if expected else before
        assert get(0x69B570) == expected_head
        cases.append({"name": "raw_pool_size_{}_{}".format(size, available), "passed": True,
            "inputs": {"signed_size": size, "available_records": available}, "runs": [run],
            "observed": {"allocated": bool(expected), "free_head": expected_head},
            "limits": "Negative/zero raw size fixtures distinguish signed native gate; normal length allocator requests at least32 bytes"})
    for long_length, short_length in ((50, 50), (0, 24), (50, 0), (1, 50)):
        pool_setup(0)
        uc.mem_write(CONTEXT, bytes(0x16C))
        put(CONTEXT + 0x86, long_length, 2)
        put(CONTEXT + 0x152, short_length, 2)
        expected_long, expected_short = adjusted_lengths(long_length, short_length)
        before_record = bytes(uc.mem_read(POOL, 128))
        run = call(0x48E730)
        assert run["returned"] == 0
        assert (get(CONTEXT + 0x86, 2), get(CONTEXT + 0x152, 2)) == (expected_long, expected_short)
        assert bytes(uc.mem_read(POOL, 128)) == before_record
        cases.append({"name": "packing_empty_pool_{}_{}".format(long_length, short_length), "passed": True,
            "inputs": {"long_length": long_length, "short_length": short_length, "available_records": 0}, "runs": [run],
            "observed": {"allocated": False, "long_length": expected_long, "short_length": expected_short,
                "record_unchanged": True}})

    for long_length, short_length, current_long, full_length_byte in itertools.product((0, 1, 23, 24, 49, 50),
            (0, 1, 23, 24, 49, 50), (-1, 0, 1, 23, 49), (0, 1)):
        pool_setup(2)
        uc.mem_write(CONTEXT, bytes(0x16C))
        put(CONTEXT + 0x86, long_length, 2)
        put(CONTEXT + 0x152, short_length, 2)
        put(CONTEXT + 0x84, current_long, 2)
        put(CONTEXT + 0x154, full_length_byte, 1)
        put(0x52651C, 123456)
        long_data = struct.pack("<50H", *range(1000, 1050))
        short_data = b"".join(struct.pack("<2H", 300 + i, 500 + i) for i in range(50))
        uc.mem_write(CONTEXT + 0x20, long_data)
        uc.mem_write(CONTEXT + 0x88, short_data)
        # Arrays must be written before adjacent length/control fields.
        put(CONTEXT + 0x86, long_length, 2)
        put(CONTEXT + 0x152, short_length, 2)
        put(CONTEXT + 0x84, current_long, 2)
        put(CONTEXT + 0x154, full_length_byte, 1)
        expected_long, expected_short = adjusted_lengths(long_length, short_length)
        run = call(0x48E730)
        assert run["returned"] == POOL
        assert get(0x69B570) == POOL + 128
        assert get(POOL + 0xC) == 123456
        assert get(POOL + 0x18, 1) == get(POOL + 0x19, 1) == get(POOL + 0x1F, 1) == 0
        assert get(POOL + 0x1B, 1) == full_length_byte
        assert get(POOL + 0x1C, 1) == expected_long
        assert get(POOL + 0x1D, 1) == min(current_long, expected_long) & 255
        assert get(POOL + 0x1E, 1) == expected_short
        expected_payload = short_data[:expected_short * 4] + long_data[:expected_long * 2]
        assert bytes(uc.mem_read(POOL + 0x20, len(expected_payload))) == expected_payload
        put(POOL + 8, 600 | 700 << 16)
        put(UNIT + 0x100, POOL)
        uc.mem_write(RESTORED, bytes([0xCC]) * 0x16C)
        unpack = call(0x48E7E0, ecx=UNIT, edx=POOL, arguments=(RESTORED,))
        assert get(RESTORED) == UNIT
        assert list(struct.unpack("<4H", uc.mem_read(RESTORED + 4, 8))) == [100, 200, 600, 700]
        assert get(RESTORED + 0x86, 2) == expected_long and get(RESTORED + 0x152, 2) == expected_short
        assert get(RESTORED + 0x150, 2) == 0
        assert get(RESTORED + 0x84, 2) == min(current_long, expected_long) & 0xFFFF
        assert get(RESTORED + 0x154, 1) == full_length_byte
        assert bytes(uc.mem_read(RESTORED + 0x20, expected_long * 2)) == long_data[:expected_long * 2]
        assert bytes(uc.mem_read(RESTORED + 0x88, expected_short * 4)) == short_data[:expected_short * 4]
        release = call(0x48E880, ecx=UNIT)
        assert get(UNIT + 0x100) == 0 and get(0x69B570) == POOL
        assert get(POOL) == 2 and get(POOL + 4) == 1
        reused = call(0x48E8D0, ecx=128)
        assert reused["returned"] == POOL and get(0x69B570) == POOL + 128
        cases.append({"name": "pack_restore_reuse_{}_{}_{}_{}".format(long_length, short_length, current_long, full_length_byte),
            "passed": True, "inputs": {"long_length": long_length, "short_length": short_length,
                "current_long_index": current_long, "full_length_low_byte": full_length_byte, "available_records": 2},
            "runs": [run, unpack, release, reused], "observed": {"packed_long_length": expected_long,
                "packed_short_length": expected_short, "packed_current_long_index": min(current_long, expected_long),
                "payload_sha256": assets.sha(expected_payload), "record_reused": True},
            "oracle": "typed packed header/payload, signed index restoration and actual release/pop reuse"})
    print(json.dumps({"phase": "pack/unpack/reuse", "cases": len(cases)}), flush=True)

    for timer in range(256):
        pool_setup(1)
        put(UNIT + 0x100, POOL)
        setter = call(0x48E970, ecx=UNIT, edx=timer)
        assert get(POOL + 0x19, 1) == timer
        first = call(0x48E950, ecx=UNIT)
        assert first["returned"] == int(timer == 0)
        assert get(POOL + 0x19, 1) == max(timer - 1, 0)
        second = call(0x48E950, ecx=UNIT)
        assert second["returned"] == int(timer <= 1)
        assert get(POOL + 0x19, 1) == max(timer - 2, 0)
        cases.append({"name": "retry_timer_{}".format(timer), "passed": True, "inputs": {"timer": timer},
            "runs": [setter, first, second], "observed": {"first_ready": int(timer == 0), "second_ready": int(timer <= 1),
                "remaining_timer": max(timer - 2, 0)}})
    for movement_type, distance, angle, at_last, region_matches in itertools.product((0, 1, 2), (0, 1, 8, 16, 17),
            (0, 2, 16, 17, 32, 33, 64, 128), (False, True), (False, True)):
        pool_setup(1)
        put(UNIT + 0x100, POOL)
        put(UNIT + 0x27, movement_type, 1)
        put(UNIT + 0x23, 0, 1)
        put(UNIT + 0x4B, angle, 1)
        put(UNIT + 0x10, 900 | 900 << 16)
        put(POOL + 4, (100 + distance) | 200 << 16)
        put(POOL + 0x1E, 2, 1)
        put(POOL + 0x1F, 1 if at_last else 0, 1)
        put(POOL + 0x1D, 0, 1)
        put(POOL + 0x28, 1 if region_matches else 2, 2)
        expected = int(distance != 0 and (movement_type != 0 or distance > 16 or distance * 2 >= angle or
                                          (at_last and not region_matches)))
        run = call(0x48EE40, ecx=UNIT, arguments=(0,))
        assert run["returned"] == expected, (movement_type, distance, angle, at_last, region_matches, expected, run)
        cases.append({"name": "waypoint_{}_{}_{}_{}_{}".format(movement_type, distance, angle, int(at_last), int(region_matches)),
            "passed": True, "inputs": {"movement_type": movement_type, "cardinal_distance": distance, "heading_error": angle,
                "at_last_short_node": at_last, "source_region_matches_stored_long_node": region_matches},
            "runs": [run], "observed": {"reuse": expected}, "oracle": "native waypoint/heading/region policy on declared positive path"})
    for movement_type, distance in itertools.product((0, 1, 2), (0, 10)):
        pool_setup(1)
        put(UNIT + 0x100, POOL)
        put(UNIT + 0x27, movement_type, 1)
        put(UNIT + 0x10, (100 + distance) | 200 << 16)
        put(POOL + 4, (100 + distance) | 200 << 16)
        run = call(0x48EE40, ecx=UNIT, arguments=(0,))
        assert run["returned"] == int(distance != 0)
        cases.append({"name": "waypoint_is_move_target_{}_{}".format(movement_type, distance), "passed": True,
            "inputs": {"movement_type": movement_type, "distance": distance}, "runs": [run],
            "observed": {"reuse": int(distance != 0)}, "oracle": "current-point equality is checked before move-target equality"})
    put(UNIT + 0x100, 0)
    null_runs = [call(0x48E970, ecx=UNIT, edx=123), call(0x48E950, ecx=UNIT),
                 call(0x48EE40, ecx=UNIT, arguments=(0,))]
    assert null_runs[1]["returned"] == null_runs[2]["returned"] == 0
    cases.append({"name": "absent_path_timer_and_waypoint", "passed": True, "runs": null_runs,
        "observed": {"timer_ready": 0, "waypoint_reuse": 0}})
    result = {"target_sha256": TARGET_HASH, "version": "1.17.0.1", "probe_script_sha256": assets.file_sha(Path(__file__)),
        "dependency_hashes": {Path(module.__file__).name: assets.file_sha(Path(module.__file__)) for module in (assets, regions, counts)},
        "case_count": len(cases), "total_native_checks": len(cases), "all_expected_results_matched": True, "failed_cases": 0,
        "complete_function_returns": sum(len(row["runs"]) for row in cases), "fragment_completions": 0,
        "no_helper_stubs": True, "no_import_execution": True, "full_game_or_campaign_executed": False,
        "maximum_instructions_per_execution": 100000,
        "runtime": {"python": platform.python_version(), "architecture": platform.machine(), "unicorn": unicorn.__version__},
        "cases": cases, "limits": "Explicit bounded search-output records, finite free-list and uniform region lookup; no path search, OS pool creation, natural path/status production or movement FSM"}
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({"checks": len(cases), "complete_returns": result["complete_function_returns"]}))


if __name__ == "__main__":
    main()
