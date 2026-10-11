#!/usr/bin/env python3
"""Extract exact-hash 1.17 damage witnesses without executing the PE."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import capstone
import pefile

TARGET_HASH = "94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0"
BLOCKS = {
    "bullet_damage_amount": (0x409010, 0x4090DD),
    "weapon_damage": (0x41DFC0, 0x41E14C),
    "hp_damage": (0x41E150, 0x41E28D),
    "bullet_deal_damage": (0x41E290, 0x41E2EB),
    "hallucinated_weapon_hit": (0x41E2F0, 0x41E36A),
    "melee_deal_damage": (0x41FC70, 0x41FD0F),
    "on_damage_reaction": (0x4203D0, 0x42051F),
    "on_hit_should_change_target": (0x4212B0, 0x421316),
    "damage_overlay": (0x422DD0, 0x422E6A),
    "remove_overlay": (0x422E70, 0x422EAD),
    "shield_overlay_direction": (0x422FE0, 0x422FFE),
    "matrix_impact_overlay": (0x422AE0, 0x422AF4),
    "matrix_size_class": (0x428D90, 0x428DAB),
    "matrix_reduce": (0x4384C0, 0x438552),
    "armor_upgrade_bonus": (0x43AB10, 0x43AB78),
    "damage_type_size_scale": (0x43EB20, 0x43EB42),
    "find_overlay": (0x416940, 0x41696B),
    "selected_redraw": (0x416BB0, 0x416BCA),
    "attacker_undetected": (0x40A180, 0x40A1B8),
    "attack_notification": (0x446F00, 0x446FC8),
    "unit_outside_camera": (0x422C20, 0x422C78),
    "rectangle_unit_search": (0x422330, 0x422454),
    "spatial_axis_lower_bound": (0x419500, 0x419555),
    "release_unit_search": (0x4222B0, 0x4222C8),
    "allocate_image": (0x40EBA0, 0x40EBC4),
    "image_lo_offset": (0x40F6A0, 0x40F6D1),
    "add_matrix_image": (0x415260, 0x4152C4),
    "add_shield_image": (0x415370, 0x4153F2),
    "upgrade_level": (0x49A470, 0x49A496),
    "is_transport": (0x4381F0, 0x43822D),
    "loaded_unit_index": (0x4381D0, 0x4381E7),
    "kill_count": (0x41FC40, 0x41FC64),
    "death_scores": (0x414080, 0x414177),
    "kill_score_value": (0x41FB30, 0x41FB3D),
    "inactive_player": (0x4BD590, 0x4BD5C9),
    "target_enemy": (0x4290E0, 0x429110),
    "remove_unit_entry": (0x42A4D0, 0x42A529),
    "post_kill_callback": (0x4BE950, 0x4BE951),
    "ai_damage_help_entry": (0x4AEBC0, 0x4AEC68),
    "ai_damage_help_return": (0x4AF217, 0x4AF220),
}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    data = args.exe.read_bytes()
    if hashlib.sha256(data).hexdigest() != TARGET_HASH:
        parser.error("Unknown executable hash")
    pe = pefile.PE(data=data)
    assert pe.OPTIONAL_HEADER.ImageBase == 0x400000 and pe.FILE_HEADER.Machine == 0x14C
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    def read(va, count):
        offset = pe.get_offset_from_rva(va - 0x400000)
        return data[offset:offset+count]
    witnesses = {}
    for label, (start, end) in BLOCKS.items():
        code = read(start, end-start)
        instructions = list(md.disasm(code, start))
        decoded = sum(x.size for x in instructions)
        witnesses[label] = {"preferred_va": hex(start), "rva": hex(start-0x400000),
            "end_exclusive": hex(end), "requested_bytes": len(code), "decoded_bytes": decoded,
            "undecoded_tail": code[decoded:].hex(),
            "instructions": [{"va": hex(x.address), "rva": hex(x.address-0x400000),
                "file_offset": hex(pe.get_offset_from_rva(x.address-0x400000)),
                "bytes": x.bytes.hex(), "mnemonic": x.mnemonic, "operands": x.op_str}
                for x in instructions]}
    rows = [list(struct.unpack("<5I", read(0x50323C+i*20, 20))) for i in range(5)]
    result = {"target_sha256": TARGET_HASH, "target_version": "1.17.0.1",
        "original_code_executed": False,
        "extractor_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        "witnesses": witnesses,
        "damage_factor_table": {"preferred_va": "0x50323c", "rva": "0x10323c",
            "row_width_bytes": 20, "columns": ["damage_type", "independent", "small", "medium", "large"],
            "rows": rows},
        "source_pins": {"GPTP": "ce321f0fa83174aee741b91f1b2eaec30300773e",
            "Teippi": "05c006c2f74ad11285c39d37135aed03d1fb8806",
            "OpenBW": "4b046d5f65302b10cb0a745f0fecd37ec85b20a8"}}
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(result, indent=2)+"\n")
    print(json.dumps({"witnesses": len(witnesses),
        "instructions": sum(len(x["instructions"]) for x in witnesses.values()),
        "tails": {k:v["undecoded_tail"] for k,v in witnesses.items() if v["undecoded_tail"]}}))

if __name__ == "__main__":
    main()
