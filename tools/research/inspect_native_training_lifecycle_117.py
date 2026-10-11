#!/usr/bin/env python3
"""Extract exact-hash Train execution, creation, completion and AI witnesses."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import capstone
import pefile

TARGET_HASH = '94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0'
BLOCKS = {
    'raw_rng': (0x478200, 0x478232),
    'sprite_allocator': (0x4154A0, 0x415536),
    'sprite_initialize': (0x4162F0, 0x4163A0),
    'image_allocate': (0x40EBA0, 0x40EBC4),
    'sprite_add_image': (0x4151F0, 0x415256),
    'cancel_training_slot': (0x41B810, 0x41B8F3),
    'cancel_construction': (0x41A340, 0x41A4AA),
    'unit_inactive': (0x41FE40, 0x41FE55),
    'refund_full_unit_cost': (0x408460, 0x408485),
    'completed_worker_ai': (0x486340, 0x4863A7),
    'completed_overlord_ai': (0x486200, 0x486337),
    'worker_or_building_ai_add': (0x484CD0, 0x484F08),
    'building_ai_pool_allocate': (0x483E40, 0x483E85),
    'town_main_building_available': (0x424460, 0x4244BE),
    'racial_gas_building_type': (0x486100, 0x486124),
    'set_building_type': (0x422F00, 0x422F51),
    'completed_unit_add': (0x414060, 0x41406E),
    'completed_unit_account': (0x4145A0, 0x4146FA),
    'powerup_type': (0x429160, 0x4291A9),
    'train_secondary': (0x41B060, 0x41B1E8),
    'build_hp_gain': (0x41B290, 0x41B2A4),
    'initialize_build_hp_gain': (0x427D90, 0x427DDE),
    'initialize_build_shield_gain': (0x427DE0, 0x427E54),
    'building_ai_pool_release': (0x483B10, 0x483B61),
    'clear_town_gas_references': (0x486000, 0x486069),
    'begin_train': (0x41BC30, 0x41BCAB),
    'rally_trained_unit': (0x41BD70, 0x41BDD6),
    'progress_build': (0x41A280, 0x41A336),
    'unit_allocator': (0x424860, 0x4248F5),
    'create_unit_with_subunit': (0x424900, 0x4249E0),
    'creation_map_bounds': (0x424D30, 0x424D91),
    'finish_unit': (0x424DA0, 0x424DD3),
    'finish_unit_post': (0x424DE0, 0x424F36),
    'finish_unit_pre': (0x424F40, 0x425015),
    'creation_error_notification': (0x425020, 0x42504C),
    'unit_initialize': (0x425710, 0x425960),
    'unit_type_initialize': (0x425A70, 0x425C9C),
    'finish_unit_placement': (0x426600, 0x4266CD),
    'flingy_initialize': (0x40C2B0, 0x40C39E),
    'sprite_create_wrapper': (0x4183B0, 0x4183DF),
    'unit_flingy_initialize': (0x4178E0, 0x41793A),
    'sprite_animation': (0x417220, 0x417254),
    'rally_producer_type': (0x429250, 0x4292C1),
    'secondary_order_dispatcher': (0x439310, 0x43938C),
    'creation_error_string': (0x43F0C0, 0x43F0CD),
    'unit_completion_sound': (0x446650, 0x4466D0),
    'trained_unit_ai': (0x48D320, 0x48D37A),
    'finished_ordinary_unit_ai': (0x487990, 0x487ABD),
    'medic_ai_reference': (0x482520, 0x48254E),
    'clear_training_ai_slot': (0x484F70, 0x484F8E),
    'copy_training_ai_slot': (0x484F90, 0x484FBC),
    'military_ai_allocate': (0x4AE760, 0x4AE840),
    'unit_ai_region': (0x4B0960, 0x4B097B),
    'guard_ai_assign': (0x4B25B0, 0x4B25CA),
    'guard_ai_find_or_allocate': (0x4B2010, 0x4B2109),
    'ui_refresh_flags': (0x4A7ED0, 0x4A7EDF),
    'ui_dirty_command': (0x4A0E80, 0x4A0E8B),
    'ui_dirty_selection': (0x4A69C0, 0x4A69C8),
    'ui_dirty_portrait': (0x4A1E40, 0x4A1E5C),
}

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--exe', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True)
    a = p.parse_args()
    raw = a.exe.read_bytes()
    if hashlib.sha256(raw).hexdigest() != TARGET_HASH:
        p.error('Unknown executable hash')
    pe = pefile.PE(data=raw)
    assert pe.OPTIONAL_HEADER.ImageBase == 0x400000 and pe.FILE_HEADER.Machine == 0x14C
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    def read(va, count):
        off = pe.get_offset_from_rva(va-0x400000)
        return raw[off:off+count]
    witnesses = {}
    for name, (lo, hi) in BLOCKS.items():
        code = read(lo, hi-lo)
        ins = list(md.disasm(code, lo))
        count = sum(x.size for x in ins)
        witnesses[name] = dict(preferred_va=hex(lo), rva=hex(lo-0x400000), end_exclusive=hex(hi),
            requested_bytes=len(code), decoded_bytes=count, undecoded_tail=code[count:].hex(),
            instructions=[dict(va=hex(x.address), rva=hex(x.address-0x400000),
                file_offset=hex(pe.get_offset_from_rva(x.address-0x400000)), bytes=x.bytes.hex(),
                mnemonic=x.mnemonic, operands=x.op_str) for x in ins])
    destinations = struct.unpack('<10I', read(0x43938C, 40))
    mapping = read(0x4393B4, 95)
    result = dict(target_sha256=TARGET_HASH, target_version='1.17.0.1', original_code_executed=False,
        extractor_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(), witnesses=witnesses,
        secondary_dispatch=dict(table_va='0x43938c', mapping_va='0x4393b4',
            entries=[dict(order=i+37, case_va=hex(destinations[v])) for i, v in enumerate(mapping)]),
        source_pins=dict(GPTP='ce321f0fa83174aee741b91f1b2eaec30300773e',
            Teippi='05c006c2f74ad11285c39d37135aed03d1fb8806',
            OpenBW='4b046d5f65302b10cb0a745f0fecd37ec85b20a8'))
    a.out.parent.mkdir(parents=True, exist_ok=True)
    a.out.write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps(dict(witnesses=len(witnesses), instructions=sum(len(x['instructions']) for x in witnesses.values()),
        tails={k:v['undecoded_tail'] for k,v in witnesses.items() if v['undecoded_tail']})))

if __name__ == '__main__':
    main()
