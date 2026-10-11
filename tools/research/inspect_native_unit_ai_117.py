#!/usr/bin/env python3
"""Read exact-hash native unit-AI witnesses; never execute the PE."""
import argparse
import hashlib
import json
from pathlib import Path
import struct

import capstone
import pefile

TARGET_HASH = "94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0"
BLOCKS = {
    "approximate_distance": (0x405CF0, 0x405D3D),
    "heading_difference": (0x40B990, 0x40B9AB),
    "halt_distance": (0x40BF00, 0x40BF45),
    "set_attack_waypoint": (0x40CBD0, 0x40CBF7),
    "movement_state": (0x41A250, 0x41A27D),
    "is_reaver": (0x41B3D0, 0x41B3E7),
    "can_attack": (0x41DA50, 0x41DB87),
    "ground_weapon": (0x41FC20, 0x41FC3C),
    "is_queen": (0x41FF30, 0x41FF47),
    "maximum_hp": (0x409C90, 0x409CB9),
    "set_hp": (0x41BB50, 0x41BBBF),
    "shield_damage_refresh_tail": (0x41E11E, 0x41E14C),
    "hp_damage_refresh_branch": (0x41E23F, 0x41E24B),
    "type_change_refresh_tail": (0x424F2E, 0x424F36),
    "hp_rescale_refresh_branch": (0x4267E6, 0x42683A),
    "unit_initialization_refresh_branch": (0x4309D0, 0x4309E5),
    "timer_refresh_branch": (0x430A6B, 0x430A94),
    "attack_fixed_range": (0x41E400, 0x41E491),
    "attack_move": (0x41E4A0, 0x41E506),
    "computer_attack": (0x41E510, 0x41E5BE),
    "attack_unit": (0x41E5C0, 0x41E621),
    "guard": (0x41E700, 0x41E753),
    "hold_position": (0x41E760, 0x41E7EC),
    "computer_attack_move": (0x41E890, 0x41E9A4),
    "rounded_life": (0x41FAA0, 0x41FAD1),
    "removed_or_dying": (0x41FE20, 0x41FE3B),
    "maximum_life": (0x4200E0, 0x420120),
    "attack_move_reaction": (0x420600, 0x4206D4),
    "attack_move_pick_target": (0x4206E0, 0x42076C),
    "attack_unit_main": (0x420770, 0x420908),
    "attack_out_of_range": (0x420AB0, 0x420B43),
    "weapon_range": (0x420F00, 0x420FE8),
    "in_weapon_range": (0x421060, 0x421118),
    "moving_range_test": (0x421120, 0x42117F),
    "rectangle_distance_in_range": (0x4229A0, 0x422ADD),
    "can_see_target": (0x422C80, 0x422D02),
    "native_unit_array": (0x4255F0, 0x4255F6),
    "resolve_cargo": (0x428B50, 0x428B9C),
    "first_loaded_unit": (0x438140, 0x43817B),
    "cargo_space_used": (0x438350, 0x438385),
    "main_order_dispatcher": (0x439420, 0x4399BD),
    "ranged_rng": (0x478270, 0x4782C4),
    "refresh_strength": (0x4A8F30, 0x4A8F56),
    "strength_multiplier": (0x4A9630, 0x4A96A3),
    "current_strength": (0x4A96B0, 0x4A975A),
    "disabled_unit_reaction": (0x4AF270, 0x4AF2BE),
    "periodic_help_reaction": (0x4AF2C0, 0x4AF2F7),
}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--order-names", type=Path, required=True,
                        help="Pinned Teippi constants/order.h; labels only")
    args = parser.parse_args()
    data = args.exe.read_bytes()
    if hashlib.sha256(data).hexdigest() != TARGET_HASH:
        parser.error("Unknown executable hash")
    pe = pefile.PE(data=data)
    base = pe.OPTIONAL_HEADER.ImageBase
    assert base == 0x400000 and pe.FILE_HEADER.Machine == 0x14C
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    def read(va, size):
        off = pe.get_offset_from_rva(va-base)
        return data[off:off+size]
    def block(start, end):
        raw = read(start, end-start)
        instructions = list(md.disasm(raw, start))
        decoded = sum(x.size for x in instructions)
        return {"preferred_va": hex(start), "rva": hex(start-base),
                "end_exclusive": hex(end), "requested_bytes": len(raw),
                "decoded_bytes": decoded, "undecoded_tail": raw[decoded:].hex(),
                "instructions": [{"va": hex(x.address), "rva": hex(x.address-base),
                    "file_offset": hex(pe.get_offset_from_rva(x.address-base)),
                    "bytes": x.bytes.hex(), "mnemonic": x.mnemonic, "operands": x.op_str}
                    for x in instructions]}
    import re
    names_data = args.order_names.read_bytes()
    names = {int(n, 16): label for label, n in re.findall(
        r"constexpr OrderType (\w+)\(0x([0-9A-Fa-f]+)\)", names_data.decode())}
    def switch(label, table, count, mapping, low, size, unit_input=False):
        targets = struct.unpack("<" + "I"*count, read(table, count*4))
        indices = list(read(mapping, size))
        assert all(x < count for x in indices)
        entries = []
        for i, index in enumerate(indices):
            target = targets[index]
            body = list(md.disasm(read(target, 16), target))
            transfer = next((x for x in body if x.mnemonic in ("call", "jmp", "ret")), None)
            entry = {"unit_id" if unit_input else "order": i+low,
                "index": index, "case_va": hex(target),
                "first_transfer": None if transfer is None else {
                    "va": hex(transfer.address), "mnemonic": transfer.mnemonic,
                    "operands": transfer.op_str}}
            if not unit_input:
                entry["source_label"] = names.get(i+low)
            entries.append(entry)
        return {"label": label, "table_va": hex(table), "mapping_va": hex(mapping),
                "targets": [hex(x) for x in targets], "entries": entries}
    result = {"target_sha256": TARGET_HASH, "target_version": "1.17.0.1",
        "original_code_executed": False, "image_base": hex(base),
        "extractor_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        "witnesses": {name: block(*bounds) for name, bounds in BLOCKS.items()},
        "switches": [switch("unconditional special orders", 0x4399C0, 5, 0x4399D4, 0, 175),
                     switch("every-visit orders", 0x439A84, 37, 0x439B18, 4, 180),
                     switch("throttled orders", 0x439BCC, 100, 0x439D5C, 1, 187),
                     switch("weapon-range additions by type", 0x420FE8, 7, 0x421004, 0, 79, unit_input=True)],
        "source_labels": {"repository": "https://github.com/neivv/teippi",
            "commit": "05c006c2f74ad11285c39d37135aed03d1fb8806",
            "path": "src/constants/order.h",
            "file_sha256": hashlib.sha256(names_data).hexdigest(),
            "role": "Names for numeric order IDs; not native behavior proof"}}
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(result, indent=2)+"\n")
    tails = {name: item["undecoded_tail"] for name, item in result["witnesses"].items()
             if item["undecoded_tail"]}
    print(json.dumps({"witnesses": len(BLOCKS), "tails": tails,
                      "instructions": sum(len(x["instructions"]) for x in result["witnesses"].values())}))

if __name__ == "__main__":
    main()
