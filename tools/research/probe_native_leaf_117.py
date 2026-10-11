#!/usr/bin/env python3
"""Plan or execute bounded native-function probes in an in-memory x86 emulator.

Default mode only prints the reviewable probe plan. --execute-original-code is
required to run instructions from the known-hash retail executable. No WinMain,
Windows API, installer, DLL, network, or host filesystem code is executed by the
emulated PE. Each probe has a private address space and instruction allowlist.
"""

import argparse
import hashlib
import json
from pathlib import Path
import platform
import struct

import pefile


TARGET_HASH = "94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0"
MASK = 0xFFFFFFFF
TOWN = 0x800000
OUTPUT = 0x800800
STACK = 0x900000
STOP = 0xA00000
ALLOW = [(0x478200, 0x478232), (0x478270, 0x4782C4),
         (0x483030, 0x4830CF), (0x485FA0, 0x485FFE),
         (0x487550, 0x487575), (0x482040, 0x482113),
         (0x482310, 0x4823C8), (0x4835E0, 0x483647),
         (0x4143C0, 0x4143D4)]
PLAN = {
    "target_sha256": TARGET_HASH,
    "engine": "Unicorn x86-32; private emulated memory, no PE process launch",
    "functions": ["raw RNG 0x478200", "ranged RNG 0x478270",
                  "resource percentages 0x483030", "build insertion 0x487550 -> 0x485fa0",
                  "spending heap push/pop 0x482040/0x482310 with native unit-limit predicate"],
    "checks": ["enabled/disabled RNG, 32-bit overflow, counters, ranged scaling",
               "resource demand and stock branch boundaries, ratio of total needs",
               "unit/count/player bounds, flag packing, first free slot",
               "full nominal town array: preserve observed index-100 branch behavior"],
    "limits": "This executes isolated original leaf functions, not a complete game or campaign.",
}


def signed32(value):
    value &= MASK
    return value if value < 0x80000000 else value - 0x100000000


def allocation_model(min_need, gas_need, minerals, gas):
    if gas_need == 0:
        return [100, 0]
    if min_need == 0:
        return [0, 100]
    min_deficit = signed32(min_need - minerals)
    gas_deficit = signed32(gas_need - gas)
    if min_deficit > 0 and gas_deficit <= 0:
        return [100, 0]
    if gas_deficit > 0 and min_deficit <= 0:
        return [0, 100]
    gas_percent = ((gas_need * 100) & MASK) // ((min_need + gas_need) & MASK)
    return [(100 - gas_percent) & 255, gas_percent & 255]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--out", type=Path)
    parser.add_argument("--execute-original-code", action="store_true")
    args = parser.parse_args()
    data = args.exe.read_bytes()
    if hashlib.sha256(data).hexdigest() != TARGET_HASH:
        parser.error("Unknown executable hash: reviewed function probes do not apply")
    if not args.execute_original_code:
        print(json.dumps(PLAN, indent=2))
        return
    if args.out is None:
        parser.error("--out is required for execution evidence")

    import unicorn
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_ESP

    pe = pefile.PE(data=data)
    image = pe.get_memory_mapped_image()
    base = pe.OPTIONAL_HEADER.ImageBase
    image_size = (pe.OPTIONAL_HEADER.SizeOfImage + 4095) & ~4095
    cases = []

    def new_machine():
        uc = Uc(UC_ARCH_X86, UC_MODE_32)
        uc.mem_map(base, image_size)
        uc.mem_write(base, image)
        for address in [TOWN, STACK, STOP]:
            uc.mem_map(address, 4096)
        return uc

    def write32(uc, address, value):
        uc.mem_write(address, struct.pack("<I", value & MASK))

    def read32(uc, address):
        return struct.unpack("<I", uc.mem_read(address, 4))[0]

    def call(uc, start, ecx, edx=0, arguments=()):
        reached_stop = False
        visited = []

        def instruction(machine, address, size, _):
            nonlocal reached_stop
            if address == STOP:
                reached_stop = True
                machine.emu_stop()
                return
            if not any(lo <= address and address + size <= hi for lo, hi in ALLOW):
                raise RuntimeError(f"Instruction outside approved leaf blocks: {address:#x}")
            visited.append(address)

        hook = uc.hook_add(UC_HOOK_CODE, instruction)
        sp = STACK + 0x800
        uc.mem_write(sp, struct.pack("<" + "I" * (len(arguments) + 1), STOP, *arguments))
        uc.reg_write(UC_X86_REG_ESP, sp)
        uc.reg_write(UC_X86_REG_ECX, ecx)
        uc.reg_write(UC_X86_REG_EDX, edx)
        uc.emu_start(start, STOP + 1, count=2000)
        uc.hook_del(hook)
        if not reached_stop:
            raise RuntimeError("Leaf probe did not return before the instruction bound")
        return uc.reg_read(UC_X86_REG_EAX), len(visited)

    for enabled in [0, 1]:
        for seed in [0, 1, 0x7FFFFFFF, 0x80000000, MASK]:
            uc = new_machine()
            write32(uc, 0x673930, enabled)
            write32(uc, 0x67393C, seed)
            result, count = call(uc, 0x478200, 52)
            next_seed = (seed * 22695477 + 1) & MASK if enabled else seed
            expected = (next_seed >> 16) & 0x7FFF if enabled else 0
            observed = [result, read32(uc, 0x67393C), read32(uc, 0x673938),
                        read32(uc, 0x673940 + 52 * 4)]
            assert observed == [expected, next_seed, enabled, enabled], observed
            cases.append({"kind": "raw_rng", "enabled": enabled, "seed": seed,
                          "observed": observed, "instructions": count})

    for lower, upper in [(0, 0), (1, 100), (0, 255), (0, 32767),
                         (0, MASK), (0xFFFFFFFE, 1)]:
        for enabled in [0, 1]:
            uc = new_machine()
            seed = MASK
            write32(uc, 0x673930, enabled)
            write32(uc, 0x67393C, seed)
            result, count = call(uc, 0x478270, lower, upper, [52])
            state = (seed * 22695477 + 1) & MASK if enabled else seed
            raw = (state >> 16) & 0x7FFF if enabled else 0
            span = (upper - lower + 1) & MASK
            expected = ((((raw * span) & MASK) >> 15) + lower) & MASK
            observed = [result, read32(uc, 0x67393C), read32(uc, 0x673938)]
            assert observed == [expected, state, enabled], observed
            cases.append({"kind": "ranged_rng", "bounds": [lower, upper],
                          "enabled": enabled, "observed": observed, "instructions": count})

    for min_need, gas_need, minerals, gas in [
        (0, 0, 0, 0), (100, 0, 0, 0), (0, 100, 0, 0),
        (300, 100, 0, 0), (300, 100, 299, 99), (300, 100, 300, 0),
        (300, 100, 0, 100), (300, 100, 300, 100), (300, 100, 500, 500),
        (1, 2, 0, 0), (1, 2, MASK, 0), (1, 2, 0, MASK),
    ]:
        uc = new_machine()
        write32(uc, 0x67D068, min_need)
        write32(uc, 0x67D06C, gas_need)
        write32(uc, 0x5263D0, minerals)
        write32(uc, 0x526400, gas)
        _, count = call(uc, 0x483030, 0, OUTPUT, [OUTPUT + 1])
        observed = list(uc.mem_read(OUTPUT, 2))
        assert observed == allocation_model(min_need, gas_need, minerals, gas), observed
        cases.append({"kind": "resource_percentages", "input": [min_need, gas_need, minerals, gas],
                      "observed": observed, "instructions": count})

    for requested, unit_id, player_type, occupied, sentinel in [
        (1, 106, 1, 0, 0), (30, 227, 1, 0, 0), (31, 106, 1, 0, 0),
        (1, 228, 1, 0, 0), (1, 106, 2, 0, 0), (0, 106, 1, 0, 0),
        (3, 106, 1, 2, 0), (1, 106, 1, 100, 0), (1, 106, 1, 100, 8),
    ]:
        uc = new_machine()
        uc.mem_write(TOWN + 0x18, b"\0")
        uc.mem_write(0x6726D0, bytes([player_type]))
        for i in range(occupied):
            uc.mem_write(TOWN + 0x3C + 4 * i, b"\x08\x11\x22\x33")
        uc.mem_write(TOWN + 0x1CC, bytes([sentinel, 0, 0, 0]))
        before = bytes(uc.mem_read(TOWN + 0x3C, 404))
        _, count = call(uc, 0x487550, TOWN, requested, [unit_id, 150, 0])
        observed = bytes(uc.mem_read(TOWN + 0x3C, 404))
        expected = bytearray(before)
        accepted = requested <= 30 and unit_id < 228 and player_type == 1
        if accepted and (occupied < 100 or sentinel & 0xF8 == 0):
            expected[occupied * 4:occupied * 4 + 4] = struct.pack("<BBH", requested << 3, 150, unit_id)
        assert observed == bytes(expected), (requested, unit_id, occupied, observed.hex())
        cases.append({"kind": "build_insert", "input": [requested, unit_id, player_type, occupied, sentinel],
                      "changed_offsets": [i for i, (a, b) in enumerate(zip(before, observed)) if a != b],
                      "instructions": count})

    def heap_push_model(entries, value):
        pos = len(entries) if len(entries) < 63 else 62
        if pos == len(entries):
            entries.append(value)
        else:
            entries[pos] = value
        while pos > 0:
            parent = (pos - 1) // 2
            if entries[pos][0] <= entries[parent][0]:
                break
            entries[pos], entries[parent] = entries[parent], entries[pos]
            pos = parent

    def heap_pop_model(entries):
        entries[0] = entries[-1]
        entries.pop()
        pos = 0
        while 2 * pos + 1 < len(entries):
            left = 2 * pos + 1
            right = left + 1
            child = left if right >= len(entries) or entries[left][0] > entries[right][0] else right
            if entries[pos][0] >= entries[child][0]:
                break
            entries[pos], entries[child] = entries[child], entries[pos]
            pos = child

    def read_heap(uc):
        n = uc.mem_read(0x67D068 + 0x210, 1)[0]
        return [list(struct.unpack("<BBHI", uc.mem_read(0x67D068 + 0x18 + i * 8, 8)))
                for i in range(n)]

    for priorities in [[10], [10, 20, 20, 5, 255, 0], list(range(63)),
                       [200] * 63 + [1], [1] * 63 + [200]]:
        uc = new_machine()
        expected = []
        for index, priority in enumerate(priorities):
            entry = [priority, 3, index, TOWN]
            heap_push_model(expected, entry)
            _, count = call(uc, 0x482040, 0, 3, [index, priority, TOWN])
            observed = read_heap(uc)
            assert observed == expected, (index, observed, expected)
            cases.append({"kind": "spending_heap_push", "priority": priority, "id": index,
                          "observed": observed, "instructions": count})
        while expected:
            removed = expected[0]
            heap_pop_model(expected)
            _, count = call(uc, 0x482310, 0)
            observed = read_heap(uc)
            assert observed == expected, (observed, expected)
            cases.append({"kind": "spending_heap_pop", "removed": removed,
                          "observed": observed, "instructions": count})

    for limit, unit_id, own_count, other_tank_count, kind, accepted in [
        (0, 7, 999, 0, 1, True), (255, 7, 0, 0, 1, False),
        (3, 7, 2, 0, 1, True), (3, 7, 3, 0, 1, False),
        (3, 5, 1, 2, 1, False), (3, 30, 1, 2, 1, True),
        (255, 7, 0, 0, 3, True), (255, 7, 0, 0, 4, False),
    ]:
        uc = new_machine()
        uc.mem_write(0x67D068 + 0x3F0 + unit_id, bytes([limit]))
        write32(uc, 0x529604 + 4 * (unit_id * 12), own_count)
        if unit_id == 5:
            write32(uc, 0x529604 + 4 * (30 * 12), other_tank_count)
        if unit_id == 30:
            write32(uc, 0x529604 + 4 * (5 * 12), other_tank_count)
        _, count = call(uc, 0x482040, 0, kind, [unit_id, 150, TOWN])
        observed = read_heap(uc)
        assert bool(observed) == accepted, (limit, unit_id, kind, observed)
        cases.append({"kind": "spending_unit_limit", "limit": limit, "unit_id": unit_id,
                      "counts": [own_count, other_tank_count], "request_kind": kind,
                      "observed": observed, "instructions": count})

    result = {"plan": PLAN,
              "probe_script_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
              "execution_runtime": {"unicorn": unicorn.__version__,
                                    "python": platform.python_version(),
                                    "host_architecture": platform.machine()},
              "original_code_executed": True, "complete_game_executed": False,
              "checks": len(cases), "all_expected_results_matched": True, "cases": cases}
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({"checks": len(cases), "all_expected_results_matched": True,
                      "output": str(args.out)}))


if __name__ == "__main__":
    main()
