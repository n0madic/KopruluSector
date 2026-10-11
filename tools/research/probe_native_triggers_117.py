#!/usr/bin/env python3
"""Execute authorized isolated original trigger functions in Unicorn x86-32.

Default mode prints the probe plan. The explicit execution flag is required.
The emulator only maps private memory and executes an address allowlist. It
cannot reach WinMain, DLL imports, Windows APIs, network or host filesystem code.
"""

import argparse
import hashlib
import json
from pathlib import Path
import platform
import struct

import pefile


TARGET_HASH = "94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0"
TRIGGER = 0x800000
SECOND_TRIGGER = 0x801000
STACK = 0x900000
STOP = 0xA00000
EXPIRY_STOP = 0x4BE463
ALLOW = [
    (0x4B5FC0, 0x4B6016),  # Preserve, Wait, including their real helper callees below.
    (0x4BD560, 0x4BD587), (0x4BD5D0, 0x4BD5DD), (0x4BDA60, 0x4BDA76),
    (0x4BDED0, 0x4BDF0C), (0x4BDF10, 0x4BDFC8),
    (0x4B8E70, 0x4B8E83),  # Real Always/Never/Comment handlers.
    (0x4B8E90, 0x4B8EC7), (0x4B92E0, 0x4B9317),
    (0x4B9280, 0x4B92D8), (0x4B6690, 0x4B6712), (0x4B6720, 0x4B675D),
    (0x478200, 0x478232), (0x4BBAE0, 0x4BBB13),
    (0x4BE410, EXPIRY_STOP),  # Stop before unrelated timer/UI helpers.
]
PLAN = {
    "target_sha256": TARGET_HASH,
    "engine": "Unicorn x86-32 with private memory and per-instruction allowlist",
    "original_functions": ["Wait 0x4b5fd0", "Preserve 0x4b5fc0",
                           "conditions 0x4bded0 with Always/Never",
                           "actions 0x4bdf10 with Wait/Preserve/Comment",
                           "timer-expiry loop 0x4be410..0x4be463",
                           "Switch and SetSwitch", "countdown/elapsed comparisons",
                           "SetCountdownTimer", "campaign map finder 0x4bbae0"],
    "real_callees": ["current player", "wait-active read", "skip-UI flag read",
                     "wait setter", "preserve setter", "Always/Never constants"],
    "stubs": [],
    "limits": "Synthetic inputs; no complete trigger scheduler, UI or campaign execution",
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--out", type=Path)
    parser.add_argument("--execute-original-code", action="store_true")
    args = parser.parse_args()
    data = args.exe.read_bytes()
    if hashlib.sha256(data).hexdigest() != TARGET_HASH:
        parser.error("Unknown executable hash: reviewed probes do not apply")
    if not args.execute_original_code:
        print(json.dumps(PLAN, indent=2))
        return
    if args.out is None:
        parser.error("--out required for execution evidence")

    import unicorn
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_ESP

    pe = pefile.PE(data=data)
    base = pe.OPTIONAL_HEADER.ImageBase
    image = pe.get_memory_mapped_image()
    image_size = (pe.OPTIONAL_HEADER.SizeOfImage + 4095) & ~4095
    cases = []

    def new_machine(player=0):
        uc = Uc(UC_ARCH_X86, UC_MODE_32)
        uc.mem_map(base, image_size)
        uc.mem_write(base, image)
        uc.mem_map(TRIGGER, 8192)
        uc.mem_map(STACK, 4096)
        uc.mem_map(STOP, 4096)
        write32(uc, 0x6CA754, player)
        write32(uc, 0x6CA790, TRIGGER)
        return uc

    def write32(uc, address, value):
        uc.mem_write(address, struct.pack("<I", value & 0xFFFFFFFF))

    def read32(uc, address):
        return struct.unpack("<I", uc.mem_read(address, 4))[0]

    def byte(uc, address):
        return uc.mem_read(address, 1)[0]

    def call(uc, start, ecx, stop=STOP):
        visited = []
        reached_stop = False

        def instruction(machine, address, size, _):
            nonlocal reached_stop
            if address == stop:
                reached_stop = True
                machine.emu_stop()
                return
            if not any(lo <= address and address + size <= hi for lo, hi in ALLOW):
                raise RuntimeError(f"Instruction outside approved blocks: {address:#x}")
            visited.append(address)

        hook = uc.hook_add(UC_HOOK_CODE, instruction)
        sp = STACK + 0x800
        write32(uc, sp, STOP)
        uc.reg_write(UC_X86_REG_ESP, sp)
        uc.reg_write(UC_X86_REG_ECX, ecx)
        try:
            uc.emu_start(start, STOP + 1, count=4000)
        finally:
            uc.hook_del(hook)
        if not reached_stop:
            raise RuntimeError("Instruction limit reached without approved stop")
        return uc.reg_read(UC_X86_REG_EAX), len(visited)

    def action(uc, trigger, index, kind, duration=0, flags=0):
        ptr = trigger + 0x148 + index * 32
        write32(uc, ptr + 0xC, duration)
        uc.mem_write(ptr + 0x1A, bytes([kind]))
        uc.mem_write(ptr + 0x1C, bytes([flags]))
        return ptr

    def trigger_state(uc, trigger=TRIGGER):
        return {"flags": read32(uc, trigger + 0x948),
                "action_index": byte(uc, trigger + 0x967)}

    for player in (0, 7):
        variants = [(1, 0, 0, 42), (1, 1, 0, 42), (0, 1, 0, 42),
                    (0, 0, 0x10, 42)]
        variants += [(0, 0, 0, n) for n in (0, 41, 42, 43, 0xFFFFFFFF)]
        variants += [(0, 4, 0, 42)]
        for active, flags, trig_flags, duration in variants:
            uc = new_machine(player)
            ptr = action(uc, TRIGGER, 0, 4, duration, flags)
            write32(uc, TRIGGER + 0x948, trig_flags)
            uc.mem_write(0x6CA760 + player, bytes([active]))
            write32(uc, 0x6CA768 + 4 * player, 123)
            returned, count = call(uc, 0x4B5FD0, ptr)
            observed = [returned, byte(uc, 0x6CA760 + player),
                        read32(uc, 0x6CA768 + 4 * player), byte(uc, ptr + 0x1C)]
            if active:
                expected = [0, active, 123, flags]
            elif flags & 1:
                expected = [1, 0, 123, flags & ~1]
            elif trig_flags & 0x10:
                expected = [1, 0, 123, flags]
            else:
                expected = [0, 1, duration, flags | 1]
            assert observed == expected, (observed, expected)
            cases.append({"kind": "wait", "player": player,
                          "input": [active, flags, trig_flags, duration],
                          "observed": observed, "instructions": count})

    for flags in (0, 1, 8, 0x10, 0x40, 0xFFFFFFFF):
        uc = new_machine()
        write32(uc, TRIGGER + 0x948, flags)
        returned, count = call(uc, 0x4B5FC0, 0)
        observed = [returned, read32(uc, TRIGGER + 0x948)]
        assert observed == [1, flags | 4], observed
        cases.append({"kind": "preserve", "initial_flags": flags,
                      "observed": observed, "instructions": count})

    condition_cases = [
        ([], 1), ([(22, 0)], 1), ([(23, 0)], 0),
        ([(22, 0), (23, 0)], 0), ([(23, 2), (22, 0)], 1),
        ([(0, 0), (23, 0)], 1), ([(22, 0), (0, 0), (23, 0)], 1),
        ([(22, 0)] * 16, 1), ([(23, 2)] * 16, 1),
        ([(22, 0)] * 15 + [(23, 0)], 0),
    ]
    for values, expected in condition_cases:
        uc = new_machine()
        for i, (kind, flags) in enumerate(values):
            uc.mem_write(TRIGGER + 8 + 20 * i + 15, bytes([kind]))
            uc.mem_write(TRIGGER + 8 + 20 * i + 17, bytes([flags]))
        returned, count = call(uc, 0x4BDED0, TRIGGER)
        assert returned == expected, returned
        cases.append({"kind": "conditions", "input": values, "observed": returned,
                      "instructions": count})

    for player in (0, 7):
        for duration in (0, 41, 42, 43, 0xFFFFFFFF):
            uc = new_machine(player)
            uc.mem_write(0x6CA760 + player, b"\1")
            write32(uc, 0x6CA768 + 4 * player, duration)
            uc.mem_write(0x6CA748, struct.pack("<H", 30))
            _, count = call(uc, 0x4BE410, 42, stop=EXPIRY_STOP)
            observed = [byte(uc, 0x6CA760 + player), read32(uc, 0x6CA768 + 4 * player),
                        struct.unpack("<H", uc.mem_read(0x6CA748, 2))[0]]
            expired = duration <= 42
            expected = [0, 0, 1] if expired else [1, duration if duration == 0xFFFFFFFF else duration - 42, 30]
            assert observed == expected, observed
            cases.append({"kind": "wait_expiry_loop", "player": player,
                          "remaining": duration, "delta": 42, "observed": observed,
                          "stop_before_call_va": hex(EXPIRY_STOP), "instructions": count})

    for preserve in (False, True):
        uc = new_machine()
        ptr = action(uc, TRIGGER, 0, 4, 0)
        if preserve:
            action(uc, TRIGGER, 1, 3)
        _, c1 = call(uc, 0x4BDF10, TRIGGER)
        first = trigger_state(uc)
        assert first == {"flags": 1, "action_index": 0}, first
        _, c2 = call(uc, 0x4BE410, 42, stop=EXPIRY_STOP)
        _, c3 = call(uc, 0x4BDF10, TRIGGER)
        final = trigger_state(uc)
        expected = {"flags": 4, "action_index": 0} if preserve else {"flags": 9, "action_index": 64}
        assert final == expected and byte(uc, ptr + 0x1C) == 0, final
        cases.append({"kind": "wait_resume_and_finish", "preserve": preserve,
                      "after_first_call": first, "after_expiry_and_resume": final,
                      "instructions": c1 + c2 + c3})

    uc = new_machine()
    action(uc, TRIGGER, 0, 23, flags=2)  # KillUnitAtLocation must be skipped before dispatch.
    action(uc, TRIGGER, 1, 47)
    action(uc, TRIGGER, 2, 3)
    _, count = call(uc, 0x4BDF10, TRIGGER)
    observed = trigger_state(uc)
    assert observed == {"flags": 4, "action_index": 0}, observed
    cases.append({"kind": "disabled_action_comment_preserve", "observed": observed,
                  "instructions": count})

    # Two different triggers share one player timer. Each real runner is called
    # in list order; the complete outer scheduler is intentionally not emulated.
    uc = new_machine()
    action(uc, TRIGGER, 0, 4, 42)
    action(uc, SECOND_TRIGGER, 0, 4, 0)
    _, c1 = call(uc, 0x4BDF10, TRIGGER)
    write32(uc, 0x6CA790, SECOND_TRIGGER)
    _, c2 = call(uc, 0x4BDF10, SECOND_TRIGGER)
    both_blocked = [trigger_state(uc), trigger_state(uc, SECOND_TRIGGER)]
    assert both_blocked == [{"flags": 1, "action_index": 0}] * 2, both_blocked
    _, c3 = call(uc, 0x4BE410, 42, stop=EXPIRY_STOP)
    write32(uc, 0x6CA790, TRIGGER)
    _, c4 = call(uc, 0x4BDF10, TRIGGER)
    write32(uc, 0x6CA790, SECOND_TRIGGER)
    _, c5 = call(uc, 0x4BDF10, SECOND_TRIGGER)
    after = [trigger_state(uc), trigger_state(uc, SECOND_TRIGGER), byte(uc, 0x6CA760)]
    assert after == [{"flags": 9, "action_index": 64}, {"flags": 1, "action_index": 0}, 1], after
    cases.append({"kind": "shared_player_wait_interference", "initial": both_blocked,
                  "after_expiry_and_ordered_calls": after, "instructions": c1 + c2 + c3 + c4 + c5})

    for function, global_va, kind in ((0x4B8E90, 0x5349D4, "countdown_condition"),
                                     (0x4B92E0, 0x5349D8, "elapsed_condition")):
        for value, amount in ((0, 0), (298, 299), (299, 299), (300, 299), (0xFFFFFFFF, 299)):
            for comparison in (0, 1, 10, 2):
                uc = new_machine()
                write32(uc, global_va, value)
                condition_ptr = TRIGGER + 8
                write32(uc, condition_ptr + 8, amount)
                uc.mem_write(condition_ptr + 14, bytes([comparison]))
                returned, count = call(uc, function, condition_ptr)
                expected = int(value >= amount) if comparison == 0 else int(value <= amount) if comparison == 1 else int(value == amount) if comparison == 10 else 0
                assert returned == expected, returned
                cases.append({"kind": kind, "value": value, "amount": amount,
                              "comparison": comparison, "observed": returned,
                              "instructions": count})

    for initial, operand, modifier in ((0, 1800, 7), (1800, 299, 9), (100, 101, 9),
                                       (359998, 2, 8), (0, 0xFFFFFFFF, 7),
                                       (0x7FFFFFFF, 1, 8), (42, 100, 2)):
        uc = new_machine()
        ptr = action(uc, TRIGGER, 0, 14, operand)
        uc.mem_write(ptr + 0x1B, bytes([modifier]))
        write32(uc, 0x5349D4, initial)
        returned, count = call(uc, 0x4B6720, ptr)
        value = operand if modifier == 7 else initial + operand if modifier == 8 else initial - operand if modifier == 9 else initial
        value &= 0xFFFFFFFF
        signed = value if value < 0x80000000 else value - 0x100000000
        expected = min(359999, max(0, signed))
        observed = read32(uc, 0x5349D4)
        assert observed == expected and returned == 1, observed
        cases.append({"kind": "set_countdown", "initial": initial, "operand": operand,
                      "modifier": modifier, "observed": observed, "instructions": count})

    for index in (0, 31, 32, 255):
        for modifier, initial in ((4, 0), (5, 1), (6, 0), (6, 1)):
            uc = new_machine()
            ptr = action(uc, TRIGGER, 0, 13)
            write32(uc, ptr + 0x14, index)
            uc.mem_write(ptr + 0x1B, bytes([modifier]))
            word_va, bit = 0x534F20 + 4 * (index // 32), 1 << (index % 32)
            write32(uc, word_va, bit if initial else 0)
            returned, count = call(uc, 0x4B6690, ptr)
            expected = 1 if modifier == 4 else 0 if modifier == 5 else 1 - initial
            observed = int(bool(read32(uc, word_va) & bit))
            assert observed == expected and returned == 1, observed
            # Query the actual condition handler as a second native witness.
            cond = TRIGGER + 8
            uc.mem_write(cond + 0x10, bytes([index]))
            uc.mem_write(cond + 0xE, b"\2")
            is_set, c2 = call(uc, 0x4B9280, cond)
            uc.mem_write(cond + 0xE, b"\3")
            is_clear, c3 = call(uc, 0x4B9280, cond)
            assert bool(is_set) == bool(expected) and bool(is_clear) != bool(expected)
            cases.append({"kind": "switch_set_clear_toggle_and_query", "index": index,
                          "modifier": modifier, "initial": initial,
                          "observed": [observed, is_set, is_clear], "instructions": count + c2 + c3})
    uc = new_machine()
    ptr = action(uc, TRIGGER, 0, 13)
    write32(uc, ptr + 0x14, 256)
    uc.mem_write(ptr + 0x1B, b"\4")
    before = bytes(uc.mem_read(0x534F20, 32))
    returned, count = call(uc, 0x4B6690, ptr)
    assert bytes(uc.mem_read(0x534F20, 32)) == before and returned == 1
    cases.append({"kind": "switch_out_of_range_ignored", "index": 256,
                  "unchanged": True, "instructions": count})
    for enabled, seed in ((0, 0), (1, 0), (1, 1), (1, 0xFFFFFFFF)):
        uc = new_machine()
        ptr = action(uc, TRIGGER, 0, 13)
        uc.mem_write(ptr + 0x1B, bytes([11]))
        write32(uc, 0x673930, enabled)
        write32(uc, 0x67393C, seed)
        write32(uc, 0x534F20, 1)
        _, count = call(uc, 0x4B6690, ptr)
        state = (seed * 22695477 + 1) & 0xFFFFFFFF if enabled else seed
        rng_value = (state >> 16) & 0x7FFF if enabled else 0
        expected = int(bool(rng_value & 0x80))
        observed = read32(uc, 0x534F20) & 1
        assert observed == expected and read32(uc, 0x67393C) == state
        cases.append({"kind": "random_switch", "enabled": enabled, "seed": seed,
                      "observed": observed, "instructions": count})

    # Campaign lookup executes only native table traversal, without string/UI helpers.
    for table_va in (0x50E0B8, 0x50E0C4):
        expected_maps = {}
        for race in range(3):
            table = struct.unpack_from("<I", data, pe.get_offset_from_rva(table_va + race * 4 - base))[0]
            for row in range(64):
                off = pe.get_offset_from_rva(table + row * 8 - base)
                _, index, movie = struct.unpack_from("<HHH", data, off)
                if index == 0:
                    break
                if movie == 0:
                    expected_maps[index] = table + row * 8
        for index in (0, 1, 11, 12, 31, 32, 39, 40, 48, 49, 52, 53, 57, 63, 64, 65, 0xFFFF):
            uc = new_machine()
            # This function uses EDX for the requested map index.
            from unicorn.x86_const import UC_X86_REG_EDX
            uc.reg_write(UC_X86_REG_EDX, index)
            returned, count = call(uc, 0x4BBAE0, table_va)
            assert returned == expected_maps.get(index, 0), (index, hex(returned))
            cases.append({"kind": "campaign_find_map", "pointer_table_va": hex(table_va),
                          "map_index": index, "observed_row_va": hex(returned),
                          "instructions": count})

    result = {"plan": PLAN, "unicorn_version": unicorn.__version__,
              "probe_script_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
              "runtime_architecture": platform.machine(), "python_version": platform.python_version(),
              "original_code_executed": True, "complete_game_executed": False,
              "stubs": [], "checks": len(cases), "all_expected_results_matched": True,
              "cases": cases,
              "limits": "Real original instructions with synthetic memory inputs; scheduler and UI not executed"}
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({"checks": len(cases), "all_expected_results_matched": True,
                      "out": str(args.out)}))


if __name__ == "__main__":
    main()
