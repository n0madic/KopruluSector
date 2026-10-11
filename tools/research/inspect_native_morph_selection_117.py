#!/usr/bin/env python3
"""Recover and explicitly probe original morph-parent and merge-pair selectors.

Static by default; isolated execution requires an explicit flag and x86_64.
No helper stubs, WinMain, Windows API or host services are emulated.
"""
import argparse
import hashlib
import itertools
import json
from pathlib import Path
import platform
import struct

import capstone
import pefile

TARGET_HASH = "94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0"
WITNESSES = {
    "player_unit_list_head": (0x4256F0, 0x4256F8),
    "unit_absent_or_die_predicate": (0x41FE40, 0x41FE55),
    "select_hydralisk_parent": (0x4B0350, 0x4B03BB),
    "select_mutalisk_parent": (0x4B03C0, 0x4B042B),
    "first_second_pair_write": (0x4ADB10, 0x4ADB3A),
    "select_dark_templar_pair": (0x4B0250, 0x4B0349),
    "select_high_templar_pair": (0x4B0430, 0x4B0572),
}


def execute_probes(data, pe):
    if platform.machine().lower() not in ("x86_64", "amd64"):
        raise RuntimeError("Use the reviewed x86_64 Rosetta runtime")
    import unicorn
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_ESP

    first, ai_base, region_base, output, stack, stop = 0x800000, 0x810000, 0x820000, 0x840000, 0x900000, 0xA00000
    image = pe.get_memory_mapped_image()
    image_size = (pe.OPTIONAL_HEADER.SizeOfImage + 4095) & ~4095
    cases = []

    def write32(u, addr, value):
        u.mem_write(addr, struct.pack("<I", value & 0xFFFFFFFF))

    def write8(u, addr, value):
        u.mem_write(addr, bytes([value & 255]))

    def eligibility(record, wanted, pair=False):
        if not record.get("sprite", True) or record.get("order", 1) == 0 or record.get("type", wanted) != wanted:
            return False
        if pair:
            status = record.get("status", 1)
            if not status & 1 or status & 0x40 or record.get("order", 1) == (0xB7 if wanted == 61 else 0x69):
                return False
        elif record.get("auto_target", 0) or record.get("target", 0):
            return False
        return record.get("ai_type", 4) != 0

    def parent_contract(records, wanted):
        fallback = None
        for index, r in enumerate(records):
            if not eligibility(r, wanted):
                continue
            kind = r.get("ai_type", 4)
            if kind == 1:
                return index
            if kind == 4:
                fallback = index
                if r.get("region_state", 3) in (0, 4, 5):
                    return index
        return fallback

    def pair_contract(records, wanted):
        chosen, fallback_first, fallback_second = [None, None], None, None
        first_energy, second_energy = 9999, 9999
        count = 0
        for index, r in enumerate(records):
            if not eligibility(r, wanted, pair=True):
                continue
            kind = r.get("ai_type", 4)
            if kind == 1:
                chosen[count] = index
                count += 1
                if count == 2:
                    return 1, chosen
            if kind != 4:
                continue
            energy = r.get("energy", 100)
            if wanted == 61:
                if fallback_first is None:
                    fallback_first = index
                else:
                    fallback_second = index
            elif fallback_first is None or energy < first_energy:
                fallback_first, first_energy = index, energy
            elif fallback_second is None or energy < second_energy:
                fallback_second, second_energy = index, energy
            if wanted == 67 and energy < 75:
                continue
            if r.get("region_state", 3) in (0, 4, 5, 8, 9):
                chosen[count] = index
                count += 1
                if count == 2:
                    return 1, chosen
        if fallback_first is not None and fallback_second is not None:
            return 1, [fallback_first, fallback_second]
        return 0, chosen

    def run(label, records, wanted, pair=False):
        u = Uc(UC_ARCH_X86, UC_MODE_32)
        u.mem_map(pe.OPTIONAL_HEADER.ImageBase, image_size)
        u.mem_write(pe.OPTIONAL_HEADER.ImageBase, image)
        u.mem_map(first, 0x50000)
        u.mem_map(stack, 0x2000)
        u.mem_map(stop, 0x1000)
        write32(u, 0x665B08 + 7 * 4, first if records else 0)
        for index, r in enumerate(records):
            ptr, ai, region = first + index * 0x400, ai_base + index * 0x100, region_base + index * 0x100
            write32(u, ptr + 0x6C, ptr + 0x400 if index + 1 < len(records) else 0)
            write32(u, ptr + 0xC, 0x830000 if r.get("sprite", True) else 0)
            u.mem_write(ptr + 0x64, struct.pack("<H", r.get("type", wanted)))
            write8(u, ptr + 0x4C, r.get("owner", 7))
            write8(u, ptr + 0x4D, r.get("order", 1))
            write32(u, ptr + 0xDC, r.get("status", 1))
            write32(u, ptr + 0x7C, r.get("auto_target", 0))
            write32(u, ptr + 0x5C, r.get("target", 0))
            write32(u, ptr + 0x134, ai if r.get("ai_type", 4) else 0)
            write8(u, ptr + 0xA3, r.get("energy", 100))
            write8(u, ai + 8, r.get("ai_type", 4))
            write32(u, ai + 0x10, region)
            write8(u, region + 5, r.get("region_state", 3))
        snapshot = bytes(u.mem_read(first, 0x30000))
        start = (0x4B0250 if wanted == 61 else 0x4B0430) if pair else (0x4B0350 if wanted == 38 else 0x4B03C0)
        trace, count, reached = [], 0, False

        def hook(m, addr, size, _):
            nonlocal count, reached
            if addr == stop:
                reached = True
                m.emu_stop()
                return
            if not any(a <= addr and addr + size <= b for a, b in WITNESSES.values()):
                raise RuntimeError(f"Unreviewed original instruction {addr:#x}")
            count += 1
            if addr in (0x4256F0, 0x41FE40, 0x4ADB10):
                trace.append(hex(addr))

        u.hook_add(UC_HOOK_CODE, hook)
        u.mem_write(stack + 0x1800, struct.pack("<II", stop, output + 4))
        u.reg_write(UC_X86_REG_ESP, stack + 0x1800)
        u.reg_write(UC_X86_REG_ECX, 7)
        u.reg_write(UC_X86_REG_EDX, output)
        u.emu_start(start, stop + 1, count=10000)
        if not reached:
            raise RuntimeError("Selector did not return within 10000 original instructions")
        result = u.reg_read(UC_X86_REG_EAX)
        if pair:
            expected_result, indices = pair_contract(records, wanted)
            expected = {"return": expected_result, "outputs": [0 if i is None else first + i * 0x400 for i in indices]}
            observed = {"return": result, "outputs": list(struct.unpack("<2I", u.mem_read(output, 8)))}
        else:
            index = parent_contract(records, wanted)
            expected = {"parent": 0 if index is None else first + index * 0x400}
            observed = {"parent": result}
        if observed != expected:
            raise AssertionError((label, wanted, records, observed, expected))
        if bytes(u.mem_read(first, 0x30000)) != snapshot:
            raise AssertionError("Selector changed unit/AI/region inputs")
        cases.append({"label": label, "selector_preferred_va": hex(start), "fixture_requested_parent_type": wanted,
                      "fixture_unit_list": records, "observed": observed, "expected_from_static_contract": expected,
                      "matched": True, "original_instruction_count": count, "native_call_trace": trace,
                      "unit_ai_region_inputs_unchanged": True, "execution_scope": "complete selector and actual callees",
                      "helper_stubs": []})

    for wanted in (38, 43):
        run("empty player list", [], wanted)
        for key, value in [("sprite", False), ("order", 0), ("type", 0), ("auto_target", 1), ("target", 1), ("ai_type", 0), ("ai_type", 2), ("status", 0), ("status", 0x41), ("owner", 0)]:
            run("parent filter and intentionally absent filter", [{key: value, "ai_type": value if key == "ai_type" else 1}], wanted)
        for state in range(11):
            run("military parent region state", [{"region_state": state}, {"ai_type": 1}], wanted)
        for records in ([{}, {}], [{}, {}, {}], [{}, {"ai_type": 1}, {}], [{"region_state": 5}, {"ai_type": 1}],
                        [{"ai_type": 1}, {"region_state": 0}], [{"ai_type": 2}, {}, {"target": 1}],
                        [{"order": 0x2A, "ai_type": 1}], [{"status": 0, "ai_type": 1}]):
            run("parent list priority and final fallback", records, wanted)

    for wanted in (61, 67):
        run("empty pair list", [], wanted, True)
        run("one candidate writes first output before failure", [{"ai_type": 1}], wanted, True)
        for key, value in [("sprite", False), ("order", 0), ("type", 0), ("status", 0), ("status", 0x41), ("order", 0xB7 if wanted == 61 else 0x69), ("ai_type", 0), ("ai_type", 2), ("auto_target", 1), ("target", 1), ("owner", 0)]:
            run("pair filter and intentionally absent filter", [{key: value, "ai_type": value if key == "ai_type" else 1}, {"ai_type": 1}], wanted, True)
        for state in range(11):
            run("preferred military pair region states", [{"region_state": state}, {"ai_type": 1}, {}], wanted, True)
        for records in ([{"ai_type": 1}, {"ai_type": 1}, {}], [{}, {"ai_type": 1}, {"ai_type": 1}],
                        [{}, {}, {}], [{"region_state": 8}, {}, {"region_state": 9}],
                        [{"ai_type": 1}, {}, {}], [{}, {"ai_type": 1}, {}]):
            run("preferred pair versus military fallback", records, wanted, True)
        for energies in itertools.permutations((25, 50, 100)):
            run("fallback energy ordering", [{"energy": e} for e in energies], wanted, True)
        for energy in (0, 74, 75, 255):
            run("military preferred energy threshold", [{"energy": energy, "region_state": 0}, {"ai_type": 1}, {}], wanted, True)
            run("guard pair ignores energy threshold", [{"energy": energy, "ai_type": 1}, {"energy": energy, "ai_type": 1}], wanted, True)

    return {"format_version": 1, "target_sha256": hashlib.sha256(data).hexdigest(),
            "probe_script_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
            "runtime_versions": {"python": platform.python_version(), "unicorn": unicorn.__version__, "pefile": pefile.__version__,
                                 "capstone": capstone.__version__, "architecture": platform.machine(), "system": platform.system()},
            "original_code_executed": True, "complete_game_executed": False, "helper_stubs": [],
            "allowlisted_preferred_va_ranges": [[hex(a), hex(b)] for a, b in WITNESSES.values()],
            "all_matched": True, "case_count": len(cases), "cases": cases}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--exe", type=Path, required=True)
    p.add_argument("--out", type=Path, required=True)
    p.add_argument("--execute-isolated-original-code", action="store_true")
    p.add_argument("--probe-out", type=Path)
    a = p.parse_args()
    if a.execute_isolated_original_code and a.probe_out is None:
        p.error("--probe-out required")
    data = a.exe.read_bytes()
    if hashlib.sha256(data).hexdigest() != TARGET_HASH:
        p.error("Wrong EXE hash")
    pe = pefile.PE(data=data)
    c = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    report = {"format_version": 1, "target_sha256": TARGET_HASH, "inspector_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
              "original_code_executed": False, "complete_game_executed": False, "witnesses": {}}
    for name, (start, end) in WITNESSES.items():
        raw = pe.get_data(start - 0x400000, end - start)
        report["witnesses"][name] = {"start_preferred_va": hex(start), "end_exclusive_preferred_va": hex(end),
                                     "bytes_sha256": hashlib.sha256(raw).hexdigest(),
                                     "instructions": [{"address": hex(i.address), "bytes": i.bytes.hex(), "mnemonic": i.mnemonic, "operands": i.op_str} for i in c.disasm(raw, start)]}
    a.out.parent.mkdir(parents=True, exist_ok=True)
    a.out.write_text(json.dumps(report, indent=2) + "\n")
    if a.execute_isolated_original_code:
        result = execute_probes(data, pe)
        a.probe_out.parent.mkdir(parents=True, exist_ok=True)
        a.probe_out.write_text(json.dumps(result, indent=2) + "\n")
        print(f"{result['case_count']} native morph/merge selector cases passed; no stubs")
    else:
        print("Wrote static morph/merge selectors; original code not executed")


if __name__ == "__main__":
    main()
