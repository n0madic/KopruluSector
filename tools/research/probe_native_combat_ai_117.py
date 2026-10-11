#!/usr/bin/env python3
"""Execute isolated reviewed combat-AI instructions in private x86 memory.

Default mode prints a reviewable plan. --execute-original-code enables bounded
Unicorn execution of selected functions from the known-hash retail PE. The PE
is never started as a Windows process. There are no replacement helper stubs,
Windows APIs, DLLs, host-file operations or network calls in the guest.
"""

import argparse
import hashlib
import json
import math
from pathlib import Path
import platform
import struct

import pefile


TARGET_HASH = "94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0"
FIXTURE = 0x800000
ATTACKER = FIXTURE
TARGET = FIXTURE + 0x200
SPRITES = FIXTURE + 0x6000
STACK = 0x900000
STOP = 0xA00000
ALLOW = [
    (0x405CF0, 0x405D3D),       # Approximate center distance
    (0x40A180, 0x40A1B8),       # Real cloak/detection rejection
    (0x41D9C0, 0x41DA45),       # Native has-weapon helper
    (0x41DA50, 0x41DB87),       # Native can-attack with only reviewed helper paths
    (0x41F320, 0x41F42D),       # Native seek range, with real upgrade helper
    (0x41FAA0, 0x41FAD1),       # Rounded HP and shields
    (0x420000, 0x420027),       # Real subunit-property test
    (0x4265E0, 0x4265FF),       # Order to tech/energy-cost columns
    (0x4319D0, 0x431A8D),       # Priority rank; no bunker/retaliation stubs
    (0x431C40, 0x431C90),       # Best-search range, stop before minimum-range call
    (0x433910, 0x433A4A),       # Real selection from populated priority bucket
    (0x434B50, 0x434B78),       # Energy/cheat boundary
    (0x48E5C0, 0x48E5FF),       # Reservation rejection and existing-order return
    (0x49A470, 0x49A496),       # Native upgrade-level accessor
    (0x4A8F60, 0x4A8F96),       # Native per-type strength modifiers
    (0x4A9000, 0x4A90C3),       # Native strength with reviewed CRT numeric helpers
    (0x4A98E0, 0x4A9A07),       # Native initialization of all 228 type strengths
    (0x4D4003, 0x4D414B),       # Finite double -> int64 mantissa conversion
    (0x4D415D, 0x4D41F2),       # Masked-exception return and conversion entry
    (0x4D48A0, 0x4D48F3),       # sqrt normal finite nonnegative branch
]
PLAN = {
    "target_sha256": TARGET_HASH,
    "functions": ["target_rank", "has_weapon/disabled rejection", "rounded_life",
                  "select_bucket_member", "approximate_distance", "seek_range",
                  "best-search range prefix", "unit_strength_modifier",
                  "type_strength_formula", "initialize_228_type_strengths",
                  "energy_insufficient/order_energy_cost", "spell reservation existing-order path"],
    "inputs": "Explicit artificial CUnit/sprite/DAT/bucket state in private mapped memory",
    "limits": "No game launch, complete acquisition scan, campaign or frame execution",
    "stubs": "None; fail if an instruction leaves reviewed allowlisted code",
    "partial_function": "best_acquire stops at 0x431c90 after storing search range; it does not return or search",
    "instruction_bounds": "10000 per call; 50000 for the bounded 228-type initializer",
}


def approx_distance(x1, y1, x2, y2):
    a, b = sorted((abs(x1 - x2), abs(y1 - y2)), reverse=True)
    if a >> 2 >= b:
        return a
    c = (3 * b) >> 3
    return a - (a >> 4) - (a >> 6) + c + (c >> 5)


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
        parser.error("--out is required for original-instruction evidence")
    import unicorn
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_ESP
    from unicorn.x86_const import UC_X86_REG_FPCW, UC_X86_REG_MXCSR

    pe = pefile.PE(data=data)
    base = pe.OPTIONAL_HEADER.ImageBase
    assert base == 0x400000 and pe.FILE_HEADER.Machine == 0x14C
    image = pe.get_memory_mapped_image()
    image_size = (pe.OPTIONAL_HEADER.SizeOfImage + 4095) & ~4095
    cases = []

    def new_machine():
        uc = Uc(UC_ARCH_X86, UC_MODE_32)
        uc.mem_map(base, image_size)
        uc.mem_write(base, image)
        uc.mem_map(FIXTURE, 0x10000)
        uc.mem_map(STACK, 4096)
        uc.mem_map(STOP, 4096)
        uc.reg_write(UC_X86_REG_FPCW, 0x37F)
        uc.reg_write(UC_X86_REG_MXCSR, 0x1F80)
        return uc

    def put(uc, address, value, size=4):
        uc.mem_write(address, int(value & ((1 << (size * 8)) - 1)).to_bytes(size, "little"))

    def get(uc, address, size=4):
        return int.from_bytes(uc.mem_read(address, size), "little")

    def unit(uc, address, unit_id=15, hp=25600, shields=0, status=1, ai=0, x=0, y=0):
        put(uc, address + 8, hp)
        put(uc, address + 0x60, shields)
        put(uc, address + 0x64, unit_id, 2)
        put(uc, address + 0xDC, status)
        put(uc, address + 0x134, ai)
        sprite = SPRITES + (address - FIXTURE) // 8
        put(uc, address + 0xC, sprite)
        put(uc, sprite + 0x14, x, 2)
        put(uc, sprite + 0x16, y, 2)
        put(uc, 0x5211D8 + unit_id, 0x82, 1)
        put(uc, 0x5213A8 + unit_id, 0x82, 1)
        return sprite

    def call(uc, start, ecx, edx=0, arguments=(), stop_at=STOP, instruction_limit=10000):
        reached = False
        visited = []

        def instruction(machine, address, size, _):
            nonlocal reached
            if address == stop_at:
                reached = True
                machine.emu_stop()
                return
            if not any(lo <= address and address + size <= hi for lo, hi in ALLOW):
                raise RuntimeError(f"Instruction outside reviewed code: {address:#x}")
            visited.append(address)

        uc.hook_add(UC_HOOK_CODE, instruction)
        sp = STACK + 0x800
        uc.mem_write(sp, struct.pack("<" + "I" * (len(arguments) + 1), STOP,
                                     *(x & 0xFFFFFFFF for x in arguments)))
        uc.reg_write(UC_X86_REG_ESP, sp)
        uc.reg_write(UC_X86_REG_ECX, ecx & 0xFFFFFFFF)
        uc.reg_write(UC_X86_REG_EDX, edx & 0xFFFFFFFF)
        uc.emu_start(start, STOP + 1, count=instruction_limit)
        if not reached:
            raise RuntimeError("Probe did not reach its verified return/breakpoint")
        return uc.reg_read(UC_X86_REG_EAX), visited

    def record(kind, inputs, observed, expected, visited):
        if observed != expected:
            raise AssertionError((kind, inputs, observed, expected))
        cases.append({"kind": kind, "input": inputs, "observed": observed,
                      "expected": expected, "instruction_count": len(visited),
                      "visited_functions": [hex(lo) for lo, hi in ALLOW
                                            if any(lo <= x < hi for x in visited)]})

    for hp, shields, enabled in [(0, 0, 0), (1, 255, 1), (255, 255, 1),
                                 (256, 255, 1), (257, 256, 1),
                                 (100 * 256 + 1, 200 * 256 + 255, 1),
                                 (100 * 256 + 1, 200 * 256 + 255, 0)]:
        uc = new_machine()
        unit(uc, TARGET, hp=hp, shields=shields)
        put(uc, 0x520458 + 15, enabled, 1)
        result, visited = call(uc, 0x41FAA0, TARGET)
        expected = (hp + 255) // 256 + (shields // 256 if enabled else 0)
        record("rounded_life", [hp, shields, enabled], result, expected, visited)

    for coords in [(0, 0, 0, 0), (0, 0, 100, 0), (0, 0, 100, 25),
                   (0, 0, 100, 26), (0, 0, 100, 100),
                   (320, 640, 321, 643), (500, 500, 100, 200),
                   (-32768, -32768, 32767, 32767)]:
        uc = new_machine()
        x1, y1, x2, y2 = coords
        result, visited = call(uc, 0x405CF0, x1, x2, [y1, y2])
        record("approximate_distance", list(coords), result,
               approx_distance(*coords), visited)

    # Non-worker cases use a real native disabled-unit early rejection, not
    # a fabricated return from can_attack_target. Rank's remaining weapon
    # test still executes original instructions with explicit None columns.
    for worker, building, completed in [(0, 0, 1), (0, 1, 1), (0, 0, 0),
                                        (0, 1, 0), (1, 0, 1), (1, 1, 0)]:
        uc = new_machine()
        status = 0x400 | (0x20000 if building else 0) | completed
        unit(uc, TARGET, status=status)
        unit(uc, ATTACKER, unit_id=0)
        put(uc, 0x521660 + 15 * 4, 8 if worker else 0)
        result, visited = call(uc, 0x4319D0, ATTACKER, arguments=[TARGET])
        expected = (2 if worker else 3 if building else 4) + (0 if completed else 1)
        record("target_rank_disabled_unarmed", [worker, building, completed, status],
               result, expected, visited)
    for unit_id in [0x23, 0x24, 0x3B, 0x61]:
        uc = new_machine()
        unit(uc, TARGET, unit_id=unit_id, status=0)
        result, visited = call(uc, 0x4319D0, ATTACKER, arguments=[TARGET])
        record("target_rank_ignored_type", unit_id, result, 5, visited)

    for completed, web in [(1, 0), (1, 1), (0, 0), (0, 1)]:
        uc = new_machine()
        unit(uc, ATTACKER, unit_id=0)
        unit(uc, TARGET, unit_id=72, status=completed | (0x8000 if web else 0))
        put(uc, 0x521660 + 72 * 4, 0)
        result, visited = call(uc, 0x4319D0, ATTACKER, arguments=[TARGET])
        expected = 1 if not completed or web else 0
        record("target_rank_native_carrier_retaliation", [completed, web],
               result, expected, visited)

    for weapon in [0x82, 0]:
        uc = new_machine()
        unit(uc, ATTACKER, unit_id=0)
        unit(uc, TARGET, status=0x401)
        put(uc, 0x521660 + 15 * 4, 0)
        put(uc, 0x5211D8 + 15, weapon, 1)
        result, visited = call(uc, 0x4319D0, ATTACKER, arguments=[TARGET])
        record("target_rank_disabled_armed", weapon, result, 4 if weapon == 0x82 else 2, visited)

    # Select directly from a populated bucket. These probes deliberately do
    # not assert anything about candidate eligibility or insertion order.
    bucket_cases = [
        {"label": "AI Scourge greatest life beats distance", "id": 47, "ai": 1,
         "targets": [(25600, 0, 1, 50, 0), (51200, 0, 1, 500, 0)], "expected": 1},
        {"label": "human Scourge nearest", "id": 47, "ai": 0,
         "targets": [(25600, 0, 1, 50, 0), (51200, 0, 1, 500, 0)], "expected": 0},
        {"label": "ordinary AI nearest", "id": 0, "ai": 1,
         "targets": [(25600, 0, 1, 50, 0), (51200, 0, 1, 500, 0)], "expected": 0},
        {"label": "Scourge rounded-life tie retains first", "id": 47, "ai": 1,
         "targets": [(257, 0, 0, 500, 0), (512, 0, 0, 10, 0)], "expected": 0},
        {"label": "Scourge raw-life ordering is different", "id": 47, "ai": 1,
         "targets": [(256, 1, 1, 500, 0), (255, 255, 1, 10, 0)], "expected": 0},
        {"label": "shield-disabled column ignores stored shields", "id": 47, "ai": 1,
         "targets": [(256, 65535, 0, 10, 0), (512, 0, 0, 500, 0)], "expected": 1},
        {"label": "distance tie retains first", "id": 0, "ai": 1,
         "targets": [(25600, 0, 0, -100, 0), (51200, 0, 0, 100, 0)], "expected": 0},
    ]
    for case in bucket_cases:
        uc = new_machine()
        unit(uc, ATTACKER, unit_id=case["id"], ai=case["ai"])
        addresses = []
        for i, (hp, shields, enabled, x, y) in enumerate(case["targets"]):
            address = TARGET + i * 0x200
            unit_id = 15 + i
            unit(uc, address, unit_id=unit_id, hp=hp, shields=shields, x=x, y=y)
            put(uc, 0x520458 + unit_id, enabled, 1)
            addresses.append(address)
        rank = 2
        put(uc, 0x665CF0 + rank * 4, len(addresses))
        for i, address in enumerate(addresses):
            put(uc, 0x665B70 + rank * 64 + i * 4, address)
        result, visited = call(uc, 0x433910, ATTACKER, rank)
        record("bucket_selection", case, addresses.index(result), case["expected"], visited)

    for unit_id, upgrade, bonus in [(0, 0x10, 1), (3, 0x36, 3), (4, 0x36, 3),
                                    (38, 0x1E, 1), (66, 0x21, 2)]:
        for level in [0, 1]:
            uc = new_machine()
            unit(uc, ATTACKER, unit_id=unit_id)
            put(uc, 0x5219F0 + unit_id, 4, 1)
            address = 0x534590 + upgrade if upgrade < 46 else 0x5365DE + upgrade
            put(uc, address, level, 1)
            result, visited = call(uc, 0x41F320, ATTACKER)
            record("seek_upgrade", [unit_id, upgrade, level], result & 255,
                   4 + (bonus if level else 0), visited)
    for unit_id, bonus in [(17, 3), (18, 3), (78, 2)]:
        uc = new_machine()
        unit(uc, ATTACKER, unit_id=unit_id)
        put(uc, 0x5219F0 + unit_id, 4, 1)
        put(uc, 0x536720, 1, 1)
        result, visited = call(uc, 0x41F320, ATTACKER)
        record("seek_hero_expansion_bonus", unit_id, result & 255, 4 + bonus, visited)
    for order in [0, 0x6B]:
        uc = new_machine()
        unit(uc, ATTACKER, unit_id=1, status=1 | 0x300)
        put(uc, ATTACKER + 0x4D, order, 1)
        put(uc, 0x5219F0 + 1, 4, 1)
        result, visited = call(uc, 0x41F320, ATTACKER)
        record("seek_cloaked_ghost", order, result & 255, 4 if order == 0x6B else 0, visited)

    for ai, in_building, seek, sight in [(0, 0, 4, 9), (1, 0, 4, 9),
                                        (1, 0, 9, 4), (1, 1, 4, 9),
                                        (0, 1, 4, 9)]:
        uc = new_machine()
        unit(uc, ATTACKER, status=1 | (0x20 if in_building else 0), ai=ai)
        put(uc, 0x5219F0 + 15, seek, 1)
        put(uc, 0x521AD8 + 15, sight, 1)
        _, visited = call(uc, 0x431C40, ATTACKER, stop_at=0x431C90)
        expected = (seek + 2 if in_building else max(seek, sight) if ai else seek) * 32
        record("best_acquire_range_prefix", [ai, in_building, seek, sight],
               get(uc, 0x665D1C), expected, visited)

    for unit_id, divisor, multiplier in [(7, 4, 1), (41, 4, 1), (64, 4, 1),
                                        (13, 1, 0), (73, 1, 0), (85, 1, 0),
                                        (32, 1, 2), (43, 1, 2), (65, 1, 2),
                                        (47, 16, 1), (50, 16, 1), (83, 10, 1),
                                        (0, 1, 1), (228, 1, 1)]:
        uc = new_machine()
        strength = 12345
        result, visited = call(uc, 0x4A8F60, unit_id, strength)
        record("unit_strength_modifier", [unit_id, strength], result,
               strength * multiplier // divisor, visited)

    for energy, cost, cheat in [(0, 0, 0), (99 * 256 + 255, 100 * 256, 0),
                                (100 * 256, 100 * 256, 0), (0, 100 * 256, 1)]:
        uc = new_machine()
        unit(uc, ATTACKER)
        put(uc, ATTACKER + 0xA2, energy, 2)
        put(uc, 0x5432FC, 0x800 if cheat else 0)
        result, visited = call(uc, 0x434B50, ATTACKER, arguments=[cost])
        record("energy_insufficient", [energy, cost, cheat], result,
               int(not cheat and energy < cost), visited)
    for tech, cost in [(0, 0), (8, 150), (43, 255), (44, 150)]:
        uc = new_machine()
        put(uc, 0x51B800 + 0x71, tech, 1)
        put(uc, 0x51F868 + tech * 2, cost, 2)
        result, visited = call(uc, 0x4265E0, 0x71)
        record("order_energy_cost", [tech, cost], result, cost * 256 if tech < 44 else 0, visited)

    for reserved, mask, same_target, present in [(0, 1, 1, 1), (0, 1, 0, 1),
                                                (1, 1, 1, 1), (1, 4, 0, 1),
                                                (4, 4, 1, 1), (0, 1, 1, 0)]:
        uc = new_machine()
        unit(uc, ATTACKER)
        unit(uc, TARGET)
        put(uc, ATTACKER + 0x4D, 0x71, 1)
        old_target = TARGET if same_target else TARGET + 0x200
        put(uc, ATTACKER + 0x5C, old_target)
        put(uc, TARGET + 0x92, reserved, 1)
        result, visited = call(uc, 0x48E5C0, ATTACKER, 0x71,
                               [TARGET if present else 0, mask])
        observed = [result, get(uc, ATTACKER + 0x5C), get(uc, TARGET + 0x92, 1)]
        expected = [int(bool(present) and reserved & mask == 0), old_target, reserved]
        record("spell_reservation_existing_order", [reserved, mask, same_target, present],
               observed, expected, visited)

    def strength_model(life, damage, cooldown, factor, attack_range):
        mask = 0xFFFFFFFF
        term = ((damage * factor * 2048) // cooldown * life) & mask
        score = ((term >> 8) + (attack_range // cooldown) * factor * damage) & mask
        return math.trunc(math.sqrt(score) * 758.0 / 100.0)

    for life, damage, cooldown, factor, attack_range, shields in [
        (0, 6, 15, 1, 128, 0), (40, 6, 15, 1, 128, 0),
        (80, 20, 30, 1, 192, 0), (80, 20, 30, 2, 192, 60),
        (400, 25, 22, 1, 192, 0), (1000, 200, 1, 8, 1024, 500),
    ]:
        uc = new_machine()
        unit_id, weapon_id = 15, 0
        put(uc, 0x520708 + unit_id * 4, life * 256)
        put(uc, 0x520458 + unit_id, int(bool(shields)), 1)
        put(uc, 0x520540 + unit_id * 2, shields, 2)
        put(uc, 0x525C80 + weapon_id * 2, damage, 2)
        put(uc, 0x525E90 + weapon_id, cooldown, 1)
        put(uc, 0x525F18 + weapon_id, factor, 1)
        put(uc, 0x5254B8 + weapon_id * 4, attack_range)
        result, visited = call(uc, 0x4A9000, unit_id, weapon_id)
        expected = strength_model(life + shields, damage, cooldown, factor, attack_range) if life + shields else 0
        record("type_strength_formula", [life, damage, cooldown, factor, attack_range, shields],
               result, expected, visited)

    uc = new_machine()
    for unit_id in range(228):
        put(uc, 0x51FB90 + unit_id * 2, 228, 2)
        put(uc, 0x5211D8 + unit_id, 0x82, 1)
        put(uc, 0x5213A8 + unit_id, 0x82, 1)
        put(uc, 0x520708 + unit_id * 4, 40 * 256)
        put(uc, 0x520458 + unit_id, 0, 1)
    put(uc, 0x525C80, 6, 2)
    put(uc, 0x525E90, 15, 1)
    put(uc, 0x525F18, 1, 1)
    put(uc, 0x5254B8, 128)
    # Marine has ground weapon; Interceptor has both. Carrier borrows the
    # Interceptor's weapon columns but keeps its own life and modifier ID.
    put(uc, 0x5211D8, 0, 1)
    put(uc, 0x5211D8 + 73, 0, 1)
    put(uc, 0x5213A8 + 73, 0, 1)
    _, visited = call(uc, 0x4A98E0, 0, instruction_limit=50000)
    raw = strength_model(40, 6, 15, 1, 128)
    observed = {str(unit_id): [get(uc, 0x6AC530 + unit_id * 4),
                               get(uc, 0x6AC8C0 + unit_id * 4)]
                for unit_id in [0, 15, 35, 36, 59, 97, 72, 73]}
    expected = {"0": [0, raw], "15": [1, 1], "35": [0, 0],
                "36": [0, 0], "59": [0, 0], "97": [0, 0],
                "72": [raw, raw], "73": [0, 0]}
    record("initialize_228_type_strengths", "explicit DAT fixture; selected output columns",
           observed, expected, visited)

    result = {
        "target_sha256": TARGET_HASH, "target_version": "1.17.0.1",
        "probe_script_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        "engine": {"name": "Unicorn", "version": unicorn.__version__,
                   "guest_arch": "x86", "guest_mode": 32,
                   "host_process_machine": platform.machine()},
        "original_code_executed": True, "complete_game_executed": False,
        "replacement_helper_stubs": [], "plan": PLAN,
        "checks": len(cases), "all_expected_results_matched": True, "cases": cases,
    }
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({"checks": len(cases), "all_expected_results_matched": True,
                      "output": str(args.out)}))


if __name__ == "__main__":
    main()
