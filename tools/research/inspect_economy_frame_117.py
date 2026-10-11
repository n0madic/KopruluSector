#!/usr/bin/env python3
"""Read native 1.17 frame, town-request, worker-allocation, and RNG witnesses.

This is static inspection. It neither loads the PE into a process nor executes
its instructions. Reviewed addresses are guarded by the exact executable hash.
"""

import argparse
import hashlib
import json
from pathlib import Path
import struct

import capstone
import pefile


TARGET_HASH = "94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0"
BLOCKS = {
    "object_step": (0x445070, 0x4450F1),
    "accepted_frame_and_trigger_step": (0x444EEF, 0x444FA4),
    "ai_step": (0x48DD90, 0x48DDA4),
    "ai_slow_update": (0x48DF50, 0x48DFE9),
    "player_ai_round_robin": (0x483400, 0x4834AD),
    "town_round_robin": (0x486C80, 0x486E74),
    "build_request_wrapper": (0x487550, 0x487575),
    "technology_request_wrapper": (0x487890, 0x4878A9),
    "upgrade_request_wrapper": (0x4878B0, 0x4878CA),
    "town_request_insert": (0x485FA0, 0x485FFE),
    "town_request_initialization": (0x4857C3, 0x485823),
    "town_index_to_pointer": (0x4877D0, 0x4877E4),
    "resource_allocation_percentages": (0x483030, 0x4830CF),
    "town_worker_allocation": (0x485020, 0x4852AC),
    "player_completed_unit_count": (0x4143E0, 0x4143F4),
    "unit_inactive_predicate": (0x41FE40, 0x41FE55),
    "player_all_unit_count": (0x4143C0, 0x4143D4),
    "spending_heap_push": (0x482040, 0x482113),
    "spending_heap_pop": (0x482310, 0x4823C8),
    "unit_limit_predicate": (0x4835E0, 0x483647),
    "town_to_spending_request": (0x482B60, 0x482BBF),
    "request_cost_accumulation": (0x4836D0, 0x483744),
    "unit_cost": (0x4087C0, 0x408814),
    "gas_resource_class": (0x485E80, 0x485EEB),
    "nth_gas_resource": (0x485DE0, 0x485E74),
    "resource_area_gas_count": (0x496F40, 0x496F4E),
    "resource_area_mineral_count": (0x496F20, 0x496F3D),
    "rng_enable": (0x4781C0, 0x4781CC),
    "rng_live_get": (0x4781D0, 0x4781D6),
    "rng_initial_get": (0x4781E0, 0x4781E6),
    "rng_initial_set": (0x4781F0, 0x4781F7),
    "rng_reset": (0x478240, 0x478269),
    "rng_initialization_transfers": (0x444024, 0x4440B5),
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    data = args.exe.read_bytes()
    if hashlib.sha256(data).hexdigest() != TARGET_HASH:
        parser.error("Unknown executable hash: reviewed addresses do not apply")
    pe = pefile.PE(data=data)
    base = pe.OPTIONAL_HEADER.ImageBase
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)

    def block(start, end):
        offset = pe.get_offset_from_rva(start - base)
        instructions = list(md.disasm(data[offset:offset + end - start], start))
        if not instructions or instructions[-1].address + instructions[-1].size != end:
            parser.error(f"Reviewed block {start:#x} does not end on an instruction boundary")
        return [
            {"va": hex(i.address), "rva": hex(i.address - base),
             "file_offset": hex(pe.get_offset_from_rva(i.address - base)),
             "bytes": i.bytes.hex(), "mnemonic": i.mnemonic, "operands": i.op_str}
            for i in instructions
        ]

    offset = pe.get_offset_from_rva(0x503360 - base)
    result = {
        "target_sha256": TARGET_HASH,
        "version": "1.17.0.1",
        "image_base": hex(base),
        "evidence": "static instruction and data inspection; no original execution",
        "labels": "Analytical labels, not recovered original symbols",
        "speed_table": {
            "preferred_va": "0x503360", "element_width": 4,
            "values": list(struct.unpack_from("<7I", data, offset)),
        },
        "spending_limit_dispatch": {
            "table_preferred_va": "0x482114",
            "targets": [hex(x) for x in struct.unpack_from(
                "<2I", data, pe.get_offset_from_rva(0x482114 - base))],
            "kind_1_through_8_to_target_index": list(data[
                pe.get_offset_from_rva(0x48211C - base):
                pe.get_offset_from_rva(0x48211C - base) + 8]),
        },
        "witnesses": {name: block(*bounds) for name, bounds in BLOCKS.items()},
    }
    args.out.mkdir(parents=True, exist_ok=True)
    output = args.out / "native-economy-frame-static.json"
    output.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({"target_sha256": TARGET_HASH, "blocks": len(BLOCKS),
                      "output": str(output)}))


if __name__ == "__main__":
    main()
