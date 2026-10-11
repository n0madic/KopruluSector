#!/usr/bin/env python3
"""Extract reviewed static combat-AI witnesses from the exact local 1.17 PE.

This script reads a PE as data. It never loads or executes original code.
Addresses are preferred VAs, independently located in the known-hash image.
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
    "approximate_distance": (0x405CF0, 0x405D3D),
    "cloak_detection_rejection": (0x40A180, 0x40A1B8),
    "has_weapon": (0x41D9C0, 0x41DA45),
    "can_attack_target": (0x41DA50, 0x41DB87),
    "computer_return": (0x41E370, 0x41E3FB),
    "ai_guard": (0x41E7F0, 0x41E88D),
    "seek_range": (0x41F320, 0x41F42D),
    "rounded_life": (0x41FAA0, 0x41FAD1),
    "is_subunit": (0x420000, 0x420027),
    "in_weapon_range": (0x421060, 0x421118),
    "rectangle_candidate_list": (0x422330, 0x422454),
    "order_energy_cost": (0x4265E0, 0x4265FF),
    "target_rank": (0x4319D0, 0x431A8D),
    "random_acquire": (0x431B70, 0x431C19),
    "best_acquire": (0x431C40, 0x431CD3),
    "spell_area_enemy_life_score": (0x432090, 0x4320F7),
    "spell_area_allied_life_score": (0x432100, 0x432144),
    "nuke_area_score": (0x432150, 0x4321DE),
    "maelstrom_area_enemy_life_score": (0x4321E0, 0x432242),
    "candidate_bucket_callback": (0x432280, 0x432384),
    "spell_candidate_callbacks_a1": (0x432650, 0x4327A4),
    "spell_candidate_callbacks_a2": (0x4327E0, 0x432C50),
    "spell_selectors_a": (0x432BF0, 0x433020),
    "spell_selectors_b": (0x433020, 0x433260),
    "spell_candidate_callbacks_b1": (0x433260, 0x4334E0),
    "spell_candidate_callbacks_b2": (0x433550, 0x4337EC),
    "spell_candidate_callbacks_b3": (0x433840, 0x433910),
    "select_bucket_member": (0x433910, 0x433A4A),
    "spell_candidate_callbacks_c": (0x433A50, 0x433C4E),
    "minimum_range": (0x433C50, 0x433CC2),
    "spell_candidate_callbacks_d1": (0x433D10, 0x433F10),
    "spell_candidate_callbacks_d2": (0x433F20, 0x433FB5),
    "energy_insufficient": (0x434B50, 0x434B78),
    "first_loaded_unit": (0x438140, 0x43817B),
    "main_order_dispatcher": (0x439420, 0x4399BC),
    "radius_callback_iteration": (0x442B20, 0x442BBD),
    "nearest_rectangle_search_setup": (0x442BC0, 0x442D90),
    "nearest_rectangle_search_loop": (0x442D90, 0x44310C),
    "campaign_ai_flag": (0x4831E0, 0x4831FA),
    "global_spell_cooldown_zero": (0x4833C0, 0x4833DB),
    "computer_ai": (0x48CD70, 0x48CEDD),
    "cannot_chase_target": (0x48D2E0, 0x48D319),
    "reaction_spell_dispatch": (0x48D3A0, 0x48DAFC),
    "spell_ready": (0x48E580, 0x48E5BB),
    "reserve_spell_target": (0x48E5C0, 0x48E5FF),
    "upgrade_level": (0x49A470, 0x49A496),
    "unit_strength_modifier": (0x4A8F60, 0x4A8F96),
    "unit_strength_formula": (0x4A9000, 0x4A90C3),
    "initialize_type_strength": (0x4A98E0, 0x4A9A08),
    "read_cached_air_strength": (0x4A9A10, 0x4A9A18),
    "read_cached_ground_strength": (0x4A9A20, 0x4A9A28),
    "region_attack_grouping": (0x4AC870, 0x4AC9B0),
    "change_region_state": (0x4AB900, 0x4ABA40),
    "region_pick_attack_target": (0x4ABA70, 0x4ABC8B),
    "region_update_strength": (0x4ADBD0, 0x4ADCE3),
    "region_update_target": (0x4ADCF0, 0x4ADDD1),
    "region_state_dispatch": (0x4B0CE0, 0x4B0D3C),
    "region_scheduler": (0x4B0D70, 0x4B0E10),
    "sqrt_normal_branch": (0x4D48A0, 0x4D499E),
    "finite_double_integer_conversion": (0x4D4003, 0x4D414B),
    "masked_numeric_exception": (0x4D415D, 0x4D41A1),
    "double_integer_conversion_entry": (0x4D41A1, 0x4D41F2),
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args()
    data = args.exe.read_bytes()
    if hashlib.sha256(data).hexdigest() != TARGET_HASH:
        parser.error("Unknown EXE hash: reviewed addresses do not apply")
    pe = pefile.PE(data=data)
    base = pe.OPTIONAL_HEADER.ImageBase
    if base != 0x400000 or pe.FILE_HEADER.Machine != 0x14C:
        parser.error("Reviewed target must be PE32 x86 at preferred base 0x400000")
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)

    def read(va, size):
        offset = pe.get_offset_from_rva(va - base)
        return data[offset:offset + size]

    def disassemble(start, end):
        code = read(start, end - start)
        insns = list(md.disasm(code, start))
        decoded = sum(ins.size for ins in insns)
        # A requested slice may terminate immediately before a complete next
        # instruction. Record this explicitly instead of inventing its bytes.
        return {
            "preferred_va": hex(start), "rva": hex(start - base),
            "file_offset": hex(pe.get_offset_from_rva(start - base)),
            "end_exclusive": hex(end), "decoded_bytes": decoded,
            "requested_bytes": len(code), "undecoded_tail": code[decoded:].hex(),
            "instructions": [
                {"va": hex(ins.address), "rva": hex(ins.address - base),
                 "file_offset": hex(pe.get_offset_from_rva(ins.address - base)),
                 "bytes": ins.bytes.hex(), "mnemonic": ins.mnemonic,
                 "operands": ins.op_str}
                for ins in insns
            ],
        }

    def switch(name, table, count, mapping=None, low=0, map_count=None):
        targets = struct.unpack("<" + "I" * count, read(table, count * 4))
        record = {"label": name, "table_va": hex(table),
                  "targets": [hex(x) for x in targets]}
        if mapping is not None:
            indices = list(read(mapping, map_count))
            assert all(index < count for index in indices)
            record.update({"mapping_va": hex(mapping), "lowest_input": low,
                           "indices": indices,
                           "input_to_target": [
                               {"input": low + i, "index": index,
                                "target_va": hex(targets[index])}
                               for i, index in enumerate(indices)]})
        return record

    descriptors = []
    for label, va in (("shields_enabled", 0x501538),
                      ("unit_properties", 0x5015F8),
                      ("seek_range", 0x501604),
                      ("sight_range", 0x501610)):
        pointer, element_size, count = struct.unpack("<III", read(va, 12))
        descriptors.append({"column_name_from_units_dat_schema": label,
                            "descriptor_va": hex(va), "pointer_va": hex(pointer),
                            "element_bytes": element_size, "count": count})
    doubles = [
        {"va": hex(va), "stored_bytes": read(va, 8).hex(),
         "ieee754_double": struct.unpack("<d", read(va, 8))[0]}
        for va in (0x4F0590, 0x4F0598, 0x4EE7E0, 0x4EE7E8)
    ]
    result = {
        "target_sha256": TARGET_HASH, "target_version": "1.17.0.1",
        "image_base": hex(base), "original_code_executed": False,
        "evidence_kind": "static PE data and disassembly; analytical function labels",
        "witnesses": {name: disassemble(*bounds) for name, bounds in BLOCKS.items()},
        "switches": [
            switch("seek-range additions", 0x41F430, 7, 0x41F44C, 0, 79),
            switch("reaction spell by unit ID", 0x48DAFC, 11, 0x48DB28, 1, 71),
            switch("strength modifier by unit ID", 0x4A8F98, 6, 0x4A8FB0, 7, 79),
            switch("regional state", 0x4B0D3C, 10),
            switch("change region state", 0x4ABA40, 10),
            switch("Disruption Web unit exceptions", 0x4327A4, 2, 0x4327AC, 124, 39),
            switch("Mind Control unit exceptions", 0x4334E0, 2, 0x4334E8, 5, 99),
            switch("Maelstrom ignored units", 0x4337EC, 2, 0x4337F4, 35, 63),
            switch("Dark Swarm weapon behavior", 0x433F10, 2, 0x433F18, 0, 8),
        ],
        "dat_descriptor_witnesses": descriptors,
        "strength_double_constants": doubles,
        "source_pins": {
            "GPTP": "ce321f0fa83174aee741b91f1b2eaec30300773e",
            "OpenBW": "4b046d5f65302b10cb0a745f0fecd37ec85b20a8",
            "Teippi": "05c006c2f74ad11285c39d37135aed03d1fb8806",
            "Samase_scarf": "f1681dbac8c7d1ac184dc84918ab7dd5771061ed",
            "AISE": "da4fed681dc09bda7a21c0051f60cdad98cc7226",
        },
    }
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(result, indent=2) + "\n")
    tails = {name: value["undecoded_tail"] for name, value in result["witnesses"].items()
             if value["undecoded_tail"]}
    print(json.dumps({"output": str(args.out), "blocks": len(BLOCKS),
                      "instruction_count": sum(len(v["instructions"])
                                               for v in result["witnesses"].values()),
                      "undecoded_tails": tails}))


if __name__ == "__main__":
    main()
