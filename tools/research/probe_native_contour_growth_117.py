#!/usr/bin/env python3
"""Recover original 1.17 contour growth across explicit allocation boundaries.

Each prefix or suffix is a separately recorded fragment. Storm allocation/free
never executes, is never a callable stub, and no complete growth-call claim is
made. Native capacity arithmetic, clear/copy and pointer publication execute.
"""

import argparse
from collections import Counter
import json
from pathlib import Path
import platform
import struct

import capstone
import pefile
import inspect_terrain_assets as assets
import probe_native_region_generation_117 as generation

TARGET_HASH = generation.TARGET_HASH
CONTEXT, OLD, NEW, STACK, STOP = 0x800000, 0x810000, 0xC00000, 0x900000, 0xA00000
BLOCKS = {"contour_growth": (0x48B040, 0x48B1B7),
          "native_memset": generation.BLOCKS["native_memset"],
          "native_copy_prefix": (0x4D16A0, 0x4D16D1),
          "native_copy_return": (0x4D19E8, 0x4D19EF)}
STAGES = ((0x48B040, 0x48B063, 0x48B068, 0x48B09B),
          (0x48B0A0, 0x48B0BB, 0x48B0C0, 0x48B0F5),
          (0x48B0FA, 0x48B116, 0x48B11B, 0x48B150),
          (0x48B155, 0x48B171, 0x48B176, 0x48B1AB))


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
    witnesses, allowed = {}, set()
    for name, (lo, hi) in BLOCKS.items():
        rows = list(md.disasm(pe.get_data(lo - base, hi - lo), lo))
        assert rows[-1].address + rows[-1].size == hi
        witnesses[name] = [{"va": hex(i.address), "bytes": i.bytes.hex(), "mnemonic": i.mnemonic,
                            "operands": i.op_str} for i in rows]
        allowed.update((i.address, i.size) for i in rows)
    if args.static_out:
        args.static_out.parent.mkdir(parents=True, exist_ok=True)
        args.static_out.write_text(json.dumps({"target_sha256": TARGET_HASH, "version": "1.17.0.1",
            "evidence": "static; analytical labels; copy jump tables excluded", "witnesses": witnesses}, indent=2) + "\n")
    if not args.execute_original_code:
        print(json.dumps({"mode": "static only", "blocks": len(BLOCKS)}))
        return
    if args.out is None:
        parser.error("Execution requires --out")
    import unicorn
    from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_ESI,
        UC_X86_REG_EDI, UC_X86_REG_EBX, UC_X86_REG_ESP, UC_X86_REG_EIP)
    image = pe.get_memory_mapped_image()
    image_size = (pe.OPTIONAL_HEADER.SizeOfImage + 4095) & ~4095
    cases = []
    for capacity in (5000, 7500, 10000, 12500):
        for profile in ("empty", "before_full", "full"):
            uc = unicorn.Uc(unicorn.UC_ARCH_X86, unicorn.UC_MODE_32)
            for address, size in ((base, image_size), (CONTEXT, 0x100000), (NEW, 0x100000),
                                  (STACK, 0x10000), (STOP, 0x1000)):
                uc.mem_map(address, size)
            uc.mem_write(base, image)

            def put(address, value, width=4):
                uc.mem_write(address, (value & ((1 << (width * 8)) - 1)).to_bytes(width, "little"))

            def get(address, width=4):
                return int.from_bytes(uc.mem_read(address, width), "little")

            counts = [0] * 4 if profile == "empty" else [capacity - 1 - i for i in range(4)] if profile == "before_full" else [capacity - i for i in range(4)]
            payloads = []
            for direction in range(4):
                payload = bytes((i * 29 + direction * 7) & 255 for i in range(capacity * 8))
                payloads.append(payload)
                uc.mem_write(OLD + direction * 0x20000, payload)
                uc.mem_write(NEW + direction * 0x20000, bytes([0xCC]) * ((capacity + 2500) * 8))
                put(CONTEXT + direction * 4, OLD + direction * 0x20000)
                put(CONTEXT + 0x10 + direction * 2, counts[direction], 2)
                put(CONTEXT + 0x18 + direction * 2, capacity, 2)
            # Select original REP MOVSB and scalar memset; no host copy helper.
            put(0x6D319C, 2)
            put(0x50F310, 0)
            sp = STACK + 0xF000
            put(sp, STOP)
            runs = []

            def execute(start, end, expected_sp, scope):
                count, trace = 0, Counter()
                def check(cpu, address, size, unused):
                    nonlocal count
                    if address == end:
                        cpu.emu_stop()
                        return
                    count += 1
                    if (address, size) not in allowed:
                        raise RuntimeError("Unreviewed original instruction " + hex(address))
                    if address == 0x4D16A0:
                        trace["native_copy"] += 1
                    if address == 0x4D0C60:
                        trace["native_memset"] += 1
                hook = uc.hook_add(unicorn.UC_HOOK_CODE, check)
                try:
                    uc.emu_start(start, end + 1, count=2000000)
                    assert uc.reg_read(UC_X86_REG_EIP) == end
                    assert uc.reg_read(UC_X86_REG_ESP) == expected_sp
                finally:
                    uc.hook_del(hook)
                runs.append({"entry": hex(start), "stop": hex(end), "scope": scope,
                             "instructions": count, "calls": dict(trace)})

            for direction, (prefix, alloc_call, copied, free_call) in enumerate(STAGES):
                # Standalone prefix context: the previous free boundary is an
                # explicit fixture, never an imported result or called stub.
                uc.reg_write(UC_X86_REG_ESP, sp if direction == 0 else sp - 12)
                uc.reg_write(UC_X86_REG_ECX, CONTEXT)
                uc.reg_write(UC_X86_REG_EDI, CONTEXT)
                uc.reg_write(UC_X86_REG_EBX, 2500)
                if direction:
                    uc.reg_write(UC_X86_REG_ESI, NEW + (direction - 1) * 0x20000)
                execute(prefix, alloc_call, sp - 28, "prefix before imported allocation")
                assert get(CONTEXT + 0x18 + direction * 2, 2) == capacity + 2500
                assert get(sp - 28) == (capacity + 2500) * 8
                if direction:
                    assert get(CONTEXT + (direction - 1) * 4) == NEW + (direction - 1) * 0x20000
                # Standalone post-allocation fragment receives private storage.
                # EAX/storage/stack are declared inputs, not a completed SAlloc.
                uc.reg_write(UC_X86_REG_ESP, sp - 12)
                uc.reg_write(UC_X86_REG_EAX, NEW + direction * 0x20000)
                execute(copied, free_call, sp - 28, "post-allocation copy before imported free")
                nbytes = counts[direction] * 8
                actual = bytes(uc.mem_read(NEW + direction * 0x20000, (capacity + 2500) * 8))
                assert actual == payloads[direction][:nbytes] + bytes(len(actual) - nbytes)
                assert get(CONTEXT + direction * 4) == OLD + direction * 0x20000
                assert bytes(uc.mem_read(OLD + direction * 0x20000, capacity * 8)) == payloads[direction]
            uc.reg_write(UC_X86_REG_ESP, sp - 12)
            execute(0x48B1B0, STOP, sp + 4, "post-free pointer publication and return fragment")
            assert all(get(CONTEXT + i * 4) == NEW + i * 0x20000 for i in range(4))
            assert all(get(CONTEXT + 0x10 + i * 2, 2) == counts[i] for i in range(4))
            cases.append({"name": "growth_{}_{}".format(capacity, profile), "passed": True,
                "inputs": {"old_capacities": [capacity] * 4, "counts": counts, "cpu_copy_flag": 2},
                "runs": runs, "observed": {"new_capacities": [capacity + 2500] * 4,
                    "counts": counts, "all_payload_bytes_match": True, "all_unused_bytes_zero": True,
                    "old_storage_unchanged": True, "pointers_published_after_each_free_boundary": True},
                "oracle": "native unsigned WORD capacity increase, requested allocation sizes, full clear/copy bytes and publication order"})
    result = {"target_sha256": TARGET_HASH, "version": "1.17.0.1", "probe_script_sha256": assets.file_sha(Path(__file__)),
        "dependency_hashes": {Path(module.__file__).name: assets.file_sha(Path(module.__file__)) for module in (assets, generation)},
        "case_count": len(cases), "total_native_checks": len(cases), "all_expected_results_matched": True,
        "failed_cases": 0, "complete_growth_function_returns": 0, "fragment_completions": sum(len(case["runs"]) for case in cases),
        "runtime": {"python": platform.python_version(), "architecture": platform.machine(), "unicorn": unicorn.__version__},
        "maximum_instructions_per_execution": 2000000, "no_helper_stubs": True, "no_import_execution": True,
        "full_game_or_campaign_executed": False, "cases": cases,
        "limits": "Imported allocation/free outcomes, failures, addresses and full growth caller remain outside these explicitly separated fragments."}
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({"checks": len(cases), "fragments": result["fragment_completions"], "complete_growth_returns": 0}))


if __name__ == "__main__":
    main()
