#!/usr/bin/env python3
"""Extract exact-hash worker construction witnesses."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import capstone
import pefile
TARGET_HASH='94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0'
BLOCKS={
 'next_waypoint_equals_position':(0x40ba70,0x40ba8d),
 'mineral_resource_predicate_with_worker_argument':(0x4243b0,0x4243d5),
 'terran_stop_construction_restore_worker':(0x41b1f0,0x41b248),
 'reset_collision_order':(0x428a10,0x428a27),
 'reset_collision_order_movement':(0x43a4e0,0x43a535),
 'build_progress_hp_and_completion':(0x41a280,0x41a336),
 'construction_hp_gain_cwal':(0x41b290,0x41b2a4),
 'overlap_escape_destination':(0x42ce60,0x42ceeb),
 'unwalkable_region_escape_destination':(0x42cef0,0x42d297),
 'unit_overlap_escape_destination':(0x42d2a0,0x42d773),
 'expanded_overlap_rectangle':(0x42d780,0x42d82a),
 'rectangle_list_intersection_or_subtraction':(0x42cb90,0x42cc6f),
 'filtered_blockers_in_rectangle':(0x42e280,0x42e353),
 'region_flag_1ffd_predicate':(0x481be0,0x481c06),
 'movement_overlap_escape_state13':(0x42be70,0x42bf07),
 'overlap_escape_move_step':(0x42d830,0x42d8fb),
 'path_record_free_to_encoded_head':(0x48e910,0x48e944),
 'cancel_construction':(0x41a340,0x41a4aa),
 'unit_dead_or_missing_sprite':(0x41fe40,0x41fe55),
 'building_three_quarter_refund':(0x408490,0x4084cf),
 'terran_place_building_order':(0x41a580,0x41a762),
 'terran_self_build_order':(0x41a770,0x41a854),
 'terran_construction_order':(0x41a870,0x41ad04),
 'queued_build_validate_and_pay':(0x41bcb0,0x41bd63),
 'construction_position_clamp':(0x41a4b0,0x41a53d),
 'construction_ai_assignment':(0x486200,0x486337),
 'ui_dirty_all_helpers':(0x4a7ed0,0x4a7edf),
 'ui_dirty_selection':(0x4a0e80,0x4a0e8b),
 'ui_dirty_portrait':(0x4a69c0,0x4a69c8),
 'ui_dirty_buttons':(0x4a1e40,0x4a1e5c),
 'placement_mode_get':(0x47d070,0x47d076),
 'building_completion_voice':(0x446280,0x44635b),
 'build_placement_feedback':(0x4464e0,0x446564),
}

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--exe',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
 raw=a.exe.read_bytes();assert hashlib.sha256(raw).hexdigest()==TARGET_HASH
 pe=pefile.PE(data=raw);assert pe.OPTIONAL_HEADER.ImageBase==0x400000 and pe.FILE_HEADER.Machine==0x14C
 md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);witnesses={}
 for name,(lo,hi) in BLOCKS.items():
  code=pe.get_data(lo-0x400000,hi-lo);ins=list(md.disasm(code,lo));length=sum(x.size for x in ins)
  assert length==len(code),(name,length,len(code))
  witnesses[name]=dict(preferred_va=hex(lo),rva=hex(lo-0x400000),end_exclusive=hex(hi),requested_bytes=len(code),decoded_bytes=length,undecoded_tail=code[length:].hex(),instructions=[dict(va=hex(x.address),rva=hex(x.address-0x400000),file_offset=hex(pe.get_offset_from_rva(x.address-0x400000)),bytes=x.bytes.hex(),mnemonic=x.mnemonic,operands=x.op_str) for x in ins])
 result=dict(target_sha256=TARGET_HASH,target_version='1.17.0.1',original_code_executed=False,extractor_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),witnesses=witnesses,terran_self_build_switch=dict(destinations=[hex(x) for x in struct.unpack('<7I',pe.get_data(0x1a854,28))]),terran_construction_switch=dict(destinations=[hex(x) for x in struct.unpack('<9I',pe.get_data(0x1ad04,36))]),iscript_dispatch=dict(preferred_va='0x4111e4',destinations=[hex(x) for x in struct.unpack('<69I',pe.get_data(0x111E4,276))]),native_dat_descriptors={key:[dict(index=i,target_va=hex(struct.unpack('<3I',pe.get_data(base-0x400000+i*12,12))[0]),entry_width=struct.unpack('<3I',pe.get_data(base-0x400000+i*12,12))[1],count=struct.unpack('<3I',pe.get_data(base-0x400000+i*12,12))[2]) for i in range(count)] for key,base,count in (('units',0x5014F0,54),('flingy',0x501148,7),('sprites',0x501408,6),('images',0x5011A8,14),('weapons',0x501828,24),('orders',0x501278,19))})
 a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_text(json.dumps(result,indent=2)+'\n')
 print(json.dumps(dict(blocks=len(witnesses),instructions=sum(len(x['instructions']) for x in witnesses.values()),tails={k:v['undecoded_tail'] for k,v in witnesses.items() if v['undecoded_tail']})))
if __name__=='__main__':main()
