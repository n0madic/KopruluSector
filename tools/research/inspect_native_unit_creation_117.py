#!/usr/bin/env python3
"""Extract exact-hash placement, hidden-unit show and spatial insertion code."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import capstone
import pefile

TARGET_HASH='94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0'
BLOCKS={
    'find_placement':(0x47DC30,0x47DEE6),
    'search_placement_ring':(0x47E340,0x47EA19),
    'placement_rectangle_map_bounds':(0x47E300,0x47E33C),
    'unit_bounds_rectangle':(0x409CC0,0x409D18),
    'unit_bounds_at_position':(0x409D20,0x409D6F),
    'placement_path_gate':(0x48E980,0x48E9A5),
    'placement_collision_callback':(0x47EA20,0x47EAF3),
    'ground_footprint_trampoline':(0x412C30,0x412C39),
    'ground_footprint_available':(0x42ED50,0x42EDF1),
    'pixel_walkability':(0x42EA10,0x42EAAA),
    'terrain_collision_extents':(0x4891C0,0x48926D),
    'terrain_edges_available':(0x48B780,0x48B9D4),
    'indexed_boxes_overlap':(0x419F40,0x419FCB),
    'relocate_unit':(0x43A270,0x43A31E),
    'clamp_unit_position':(0x41A4B0,0x41A53D),
    'set_flingy_position':(0x40CD80,0x40CDB3),
    'set_flingy_destination':(0x40CCA0,0x40CCDE),
    'set_unit_destination':(0x43A0D0,0x43A180),
    'mark_path_update':(0x42C790,0x42C79F),
    'move_sprite':(0x417330,0x4173F3),
    'move_spatial_bounds':(0x419820,0x419CF7),
    'spatial_swap_x':(0x41A090,0x41A102),
    'spatial_swap_y':(0x41A110,0x41A182),
    'show_unit':(0x422F60,0x422FD3),
    'unit_vision':(0x423260,0x423328),
    'unit_vision_special_source':(0x4273C0,0x427402),
    'transport_vision_special_cargo':(0x438290,0x4382E3),
    'unit_sight_range':(0x423150,0x4231E9),
    'vision_reveal':(0x46BBB0,0x46BCBB),
    'vision_template_grid':(0x466990,0x466B83),
    'vision_template_records':(0x466BD0,0x466E32),
    'vision_template_allocator':(0x466E40,0x466F1B),
    'reveal_air_interior':(0x46C570,0x46C5A1),
    'reveal_air_edge':(0x46C5B0,0x46C611),
    'reveal_ground_interior':(0x46C620,0x46C69C),
    'reveal_ground_edge':(0x46C6A0,0x46C78B),
    'unit_sprite_visibility':(0x438C20,0x438CC3),
    'begin_cloak':(0x41D200,0x41D2FD),
    'spatial_radius_callback_query':(0x442B20,0x442B5D),
    'detection_mask':(0x4316B0,0x431795),
    'update_detection_mask':(0x4325A0,0x43261B),
    'force_detectable_status':(0x434AC0,0x434B12),
    'detection_candidate_callback':(0x4318F0,0x431979),
    'detector_unit_gate':(0x431A90,0x431ADF),
    'special_unit_list':(0x4255D0,0x4255D6),
    'clear_lost_detection_targets':(0x426B80,0x426D2F),
    'sprite_begin_cloak_drawing':(0x415770,0x41579F),
    'sprite_mark_images_dirty':(0x416210,0x416284),
    'unit_set_undetected_drawing':(0x432560,0x432592),
    'sprite_remove_cloak_selection_overlay':(0x416D50,0x416D96),
    'sprite_set_undetected_drawing':(0x4161C0,0x416201),
    'sprite_set_detected_drawing':(0x417400,0x41745A),
    'positional_sound_request':(0x446940,0x446A1C),
    'sprite_visibility_rectangle':(0x417D20,0x417E4C),
    'tile_visibility_rectangle':(0x418D80,0x418DF3),
    'sprite_set_visibility':(0x417280,0x417327),
    'activate_spatial_unit':(0x41A050,0x41A08F),
    'spatial_insert':(0x4195A0,0x419814),
    'spatial_open_x_slot':(0x419E20,0x419EAC),
    'spatial_open_y_slot':(0x419EB0,0x419F3C),
    'mark_unit_overlaps':(0x42E600,0x42E667),
    'overlap_callback':(0x42E670,0x42E6B7),
    'initialize_movement':(0x42C7E0,0x42C809),
    'discard_path':(0x42C8A0,0x42C8BE),
    'air_occupancy_add':(0x481760,0x481823),
    'hidden_list_remove':(0x427070,0x427080),
    'active_list_add':(0x425DD0,0x425DE0),
    'memmove_rep_forward_entry':(0x4D2360,0x4D2391),
    'memmove_forward_return':(0x4D26A8,0x4D26AF),
    'memmove_aligned_backward_branch':(0x4D26E8,0x4D273B),
    'memmove_aligned_backward_small_tail':(0x4D27E8,0x4D2834),
    'memmove_aligned_backward_return':(0x4D2844,0x4D284B),
}

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--exe',type=Path,required=True);p.add_argument('--out',type=Path,required=True)
    a=p.parse_args();raw=a.exe.read_bytes()
    if hashlib.sha256(raw).hexdigest()!=TARGET_HASH:p.error('Unknown executable hash')
    pe=pefile.PE(data=raw);assert pe.OPTIONAL_HEADER.ImageBase==0x400000 and pe.FILE_HEADER.Machine==0x14C
    md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32)
    def read(va,n):return pe.get_data(va-0x400000,n)
    witnesses={}
    for name,(lo,hi) in BLOCKS.items():
        code=read(lo,hi-lo);ins=list(md.disasm(code,lo));length=sum(x.size for x in ins)
        witnesses[name]=dict(preferred_va=hex(lo),rva=hex(lo-0x400000),end_exclusive=hex(hi),
            requested_bytes=len(code),decoded_bytes=length,undecoded_tail=code[length:].hex(),
            instructions=[dict(va=hex(x.address),rva=hex(x.address-0x400000),file_offset=hex(pe.get_offset_from_rva(x.address-0x400000)),
                bytes=x.bytes.hex(),mnemonic=x.mnemonic,operands=x.op_str) for x in ins])
    result=dict(target_sha256=TARGET_HASH,target_version='1.17.0.1',original_code_executed=False,
        extractor_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),witnesses=witnesses,
        sight_range_switch=dict(destinations=[hex(x) for x in struct.unpack('<5I',read(0x4231EC,20))],mapping=list(read(0x423200,84)),input_low=1),
        vision_template_dimensions=[list(struct.unpack('<7I',read(0x508A50+i*28,28))) for i in range(12)],
        vision_record_scan_directions=list(struct.unpack('<8b',read(0x4EE644,8))),
        vision_callbacks=dict(preferred_va='0x4eeda0',entries=[hex(x) for x in struct.unpack('<4I',read(0x4EEDA0,16))]))
    a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(dict(witnesses=len(witnesses),instructions=sum(len(x['instructions']) for x in witnesses.values()),
        tails={k:v['undecoded_tail'] for k,v in witnesses.items() if v['undecoded_tail']})))

if __name__=='__main__':main()
