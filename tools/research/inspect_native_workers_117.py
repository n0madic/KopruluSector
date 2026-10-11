#!/usr/bin/env python3
"""Extract known-hash 1.17 worker/town/placement instruction witnesses.

This reads the PE as data. Analytical labels are not original debug symbols.
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
    "region_at_point": (0x481B10, 0x481B98),
    "unit_region": (0x481BA0, 0x481BB0),
    "unit_inactive": (0x41FE40, 0x41FE55),
    "building_region_available": (0x4AEAA0, 0x4AEAEF),
    "worker_resume_or_repair_selection": (0x4852B0, 0x48538E),
    "worker_construction_service": (0x485400, 0x485692),
    "worker_migration": (0x486480, 0x4865A8),
    "worker_or_building_ai_add": (0x484CD0, 0x484F08),
    "worker_or_building_ai_remove": (0x486AF0, 0x486B9C),
    "worker_list_release": (0x483B70, 0x483BC1),
    "town_empty_or_migrate": (0x485840, 0x485963),
    "town_delete_if_empty": (0x4856A0, 0x485755),
    "town_main_building_available": (0x424460, 0x4244BE),
    "zerg_building_morph_predicate": (0x43C040, 0x43C084),
    "clear_resource_references": (0x486000, 0x486069),
    "racial_worker_type": (0x486070, 0x486095),
    "worker_race_accepts_building": (0x4860A0, 0x4860F3),
    "racial_gas_building_type": (0x486100, 0x486124),
    "error_id_set": (0x4B38B0, 0x4B38B7),
    "top_spending_town": (0x483530, 0x48353D),
    "top_spending_id": (0x483540, 0x48354E),
    "top_spending_kind": (0x483550, 0x48356C),
    "placement_wrapper": (0x4962C0, 0x49631F),
    "placement_search": (0x494FC0, 0x4953E2),
    "placement_block_set": (0x482EA0, 0x482EAE),
    "placement_block_get": (0x482EC0, 0x482ED2),
    "resume_building_search_wrapper": (0x432E00, 0x432E11),
    "resume_building_candidate": (0x432870, 0x4328CC),
    "worker_ai_decisions": (0x487580, 0x48772B),
    "unit_race_from_group": (0x428D60, 0x428D83),
    "worker_resource_order_selection": (0x485F20, 0x485F92),
    "placement_path_distance": (0x496930, 0x496ABF),
    "approximate_distance": (0x405CF0, 0x405D3D),
    "placement_validator": (0x412F10, 0x4130E6),
    "native_memset": (0x4D0C60, 0x4D0CF0),
    "placement_radius_filter": (0x4976D0, 0x497906),
    "placement_tile_flags": (0x412870, 0x412AB5),
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args()
    data = args.exe.read_bytes()
    if hashlib.sha256(data).hexdigest() != TARGET_HASH:
        parser.error("Unknown executable hash: reviewed addresses do not apply")
    pe = pefile.PE(data=data)
    base = pe.OPTIONAL_HEADER.ImageBase
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)

    def block(lo, hi):
        off = pe.get_offset_from_rva(lo - base)
        rows = list(md.disasm(data[off:off + hi - lo], lo))
        if not rows or rows[-1].address + rows[-1].size != hi:
            parser.error(f"Reviewed block {lo:#x} does not end on an instruction boundary")
        return [{"va": hex(i.address), "rva": hex(i.address - base),
                 "file_offset": hex(pe.get_offset_from_rva(i.address - base)),
                 "bytes": i.bytes.hex(), "mnemonic": i.mnemonic, "operands": i.op_str}
                for i in rows]

    result = {
        "target_sha256": TARGET_HASH, "version": "1.17.0.1", "image_base": hex(base),
        "evidence": "static instruction and data inspection; no original execution",
        "labels": "Analytical labels, not recovered original symbols",
        "placement_type_dispatch": {
            "first_unit_id": 106, "last_unit_id": 156,
            "map_va": "0x495408", "targets_va": "0x4953e4",
            "targets": [hex(x) for x in struct.unpack_from(
                "<9I", data, pe.get_offset_from_rva(0x4953E4 - base))],
            "indices": list(data[pe.get_offset_from_rva(0x495408 - base):
                                 pe.get_offset_from_rva(0x495408 - base) + 51]),
        },
        "witnesses": {name: block(*bounds) for name, bounds in BLOCKS.items()},
    }
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({"blocks": len(BLOCKS), "output": str(args.out)}))


if __name__ == "__main__":
    main()
