#!/usr/bin/env python3
"""Extract exact-hash ranged weapon and projectile witnesses."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import capstone
import pefile
TARGET_HASH='94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0'
BLOCKS={
 'weapon_projectile_issue':(0x420030,0x4200DF),
 'attack_with_selector':(0x41F5D0,0x41F62E),
 'allocate_bullet':(0x408F00,0x408F93),
 'initialize_bullet':(0x409D70,0x40A0AF),
 'initialize_generic_flingy_orders':(0x4178E0,0x41793A),
 'initialize_bullet_pool':(0x40A0E0,0x40A14B),
 'bullet_init_state':(0x40A8C0,0x40A9DE),
 'bullet_primary_dispatch':(0x40A6D0,0x40A704),
 'bullet_hit_dispatch':(0x4090E0,0x40951E),
 'bullet_move_to_point':(0x409990,0x409A30),
 'bullet_move_to_target':(0x409AA0,0x409AF7),
 'bullet_miss_chance':(0x41FD10,0x41FD82),
 'target_miss_chance':(0x41FA00,0x41FA9E),
 'projectile_target_point':(0x420970,0x420A84),
 'clip_segment_to_rectangle':(0x48C6D0,0x48C938),
 'tile_flags_at_coordinate':(0x412C10,0x412C2D),
 'terrain_ground_height':(0x445690,0x44571D),
 'bullet_death_state':(0x409950,0x409981),
 'direction_vector_distance':(0x40C270,0x40C2A2),
 'bullet_assign_state':(0x40A3D0,0x40A3FD),
 'flingy_order_position':(0x417A10,0x417A41),
 'flingy_order_target':(0x417A50,0x417A9F),
 'bullet_flingy_movement':(0x40CDC0,0x40CE01),
 'flingy_map_clamp':(0x40BAE0,0x40BBA3),
 'set_image_flag_40':(0x40F890,0x40F8A5),
 'clear_image_flag_40':(0x411820,0x411835),
 'image_dirty_rectangle':(0x4164C0,0x416511),
 'direction_from_slope':(0x40B940,0x40B982),
 'direction_between_points':(0x40BA10,0x40BA62),
 'flingy_accelerate_clamped':(0x40BD10,0x40BDAD),
 'goliath_image_type_predicate':(0x40FD10,0x40FD27),
 'flingy_follow_target_update':(0x40CC40,0x40CC96),
}


def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--exe',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
 raw=a.exe.read_bytes();assert hashlib.sha256(raw).hexdigest()==TARGET_HASH
 pe=pefile.PE(data=raw);assert pe.OPTIONAL_HEADER.ImageBase==0x400000 and pe.FILE_HEADER.Machine==0x14C
 md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);witnesses={}
 for name,(lo,hi) in BLOCKS.items():
  code=pe.get_data(lo-0x400000,hi-lo);ins=list(md.disasm(code,lo));length=sum(x.size for x in ins)
  witnesses[name]=dict(preferred_va=hex(lo),rva=hex(lo-0x400000),end_exclusive=hex(hi),requested_bytes=len(code),decoded_bytes=length,undecoded_tail=code[length:].hex(),instructions=[dict(va=hex(x.address),rva=hex(x.address-0x400000),file_offset=hex(pe.get_offset_from_rva(x.address-0x400000)),bytes=x.bytes.hex(),mnemonic=x.mnemonic,operands=x.op_str) for x in ins])
 result=dict(target_sha256=TARGET_HASH,target_version='1.17.0.1',original_code_executed=False,extractor_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),witnesses=witnesses,iscript_dispatch=dict(preferred_va='0x4111e4',destinations=[hex(x) for x in struct.unpack('<69I',pe.get_data(0x111E4,276))]),native_dat_descriptors={key:[dict(index=i,target_va=hex(struct.unpack('<3I',pe.get_data(base-0x400000+i*12,12))[0]),entry_width=struct.unpack('<3I',pe.get_data(base-0x400000+i*12,12))[1],count=struct.unpack('<3I',pe.get_data(base-0x400000+i*12,12))[2]) for i in range(count)] for key,base,count in (('units',0x5014F0,54),('flingy',0x501148,7),('sprites',0x501408,6),('images',0x5011A8,14),('weapons',0x501828,24))})
 for key,table,n in (('bullet_step_dispatch',0x40A6B4,7),('bullet_tail_dispatch',0x40A704,7),('bullet_init_behavior_dispatch',0x40A0B0,10),('bullet_hit_explosion_dispatch',0x409520,25),('cannot_attack_weapon_dispatch',0x408F94,2)):
  result[key]=dict(preferred_va=hex(table),destinations=[hex(x) for x in struct.unpack('<'+str(n)+'I',pe.get_data(table-0x400000,n*4))])
 result['cannot_attack_weapon_dispatch']['mapping']=list(pe.get_data(0x8F9C,103));result['cannot_attack_weapon_dispatch']['mapping_first_weapon']=6
 result['direction_table']=dict(preferred_va='0x5026b0',vectors=[list(x) for x in struct.iter_unpack('<2i',pe.get_data(0x1026B0,2048))])
 result['tangent_table']=dict(preferred_va='0x502eb0',values=list(struct.unpack('<64I',pe.get_data(0x102EB0,256))))
 a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_text(json.dumps(result,indent=2)+'\n')
 print(json.dumps(dict(blocks=len(witnesses),instructions=sum(len(x['instructions']) for x in witnesses.values()),tails={k:v['undecoded_tail'] for k,v in witnesses.items() if v['undecoded_tail']})))
if __name__=='__main__':main()
