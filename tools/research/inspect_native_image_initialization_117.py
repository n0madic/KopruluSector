#!/usr/bin/env python3
"""Extract exact-hash unit sprite/image/IScript initialization witnesses."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import capstone
import pefile
TARGET_HASH='94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0'
BLOCKS={
 'image_initialize':(0x40FC50,0x40FD02),
 'image_drawing_and_init_animation':(0x40FB50,0x40FBDD),
 'image_find_iscript_header':(0x40F340,0x40F398),
 'image_animation':(0x411730,0x4117C1),
 'iscript_vm':(0x410480,0x4111E2),
 'image_draw_function_methods':(0x4116E0,0x41172F),
 'image_playframe':(0x411690,0x4116C2),
 'image_overlay_allocate':(0x4101B0,0x41031E),
 'image_frame_refresh':(0x40EE50,0x40EE6C),
 'image_bounds':(0x40EE70,0x40EF49),
 'image_set_direction':(0x40F1C0,0x40F280),
 'sprite_set_direction':(0x417190,0x4171B5),
 'image_set_flingy_direction':(0x411580,0x41168B),
 'sprite_images_update':(0x417860,0x4178C8),
 'unit_update_images':(0x418B20,0x418B41),
 'image_draw_update_noop':(0x4BE950,0x4BE951),
 'unit_decorative_type':(0x4292F0,0x42931B),
 'unit_initial_primary_order':(0x4179B0,0x4179D5),
 'maximum_energy':(0x434940,0x4349D6),
 'sprite_damage_overlay_count':(0x416050,0x41605E),
 'unit_update_speed':(0x43AEA0,0x43AEE1),
 'image_overlay_offset':(0x40F5D0,0x40F619),
 'iscript_animation_table':(0x40EF50,0x40EF5B),
 'iscript_speed_scale':(0x429350,0x429394),
 'flingy_set_direction':(0x40C1B0,0x40C219),
 'abort_created_unit':(0x425CD0,0x425D0C),
 'unit_remove_body':(0x425050,0x4251BE),
 'unit_mineral_type':(0x424380,0x4243A1),
 'unit_special_remove':(0x4253A0,0x4254FE),
 'unit_release_special_or_hidden':(0x425270,0x42539F),
 'unit_remove_hangar_children':(0x425D10,0x425D79),
 'unit_remove_building_progress':(0x4251C0,0x425268),
 'unit_clear_all_references':(0x4269B0,0x426B75),
 'unit_clear_hidden_references':(0x426D30,0x426E75),
 'unit_bullet_references':(0x40A5A0,0x40A5E6),
 'unit_remove_owner_and_global':(0x426530,0x4265C7),
 'ai_mark_building_change':(0x48DD50,0x48DD6F),
 'unit_stop_movement':(0x43A490,0x43A4D4),
 'unit_mark_movement_stop':(0x42AD10,0x42AD25),
 'flingy_stop':(0x40CE10,0x40CEA6),
 'hangar_unit_predicate':(0x4381F0,0x43822D),
 'unburrow_list_remove':(0x434DA0,0x434E25),
 'ui_hover_unit_remove':(0x4A6E60,0x4A6E87),
 'sprite_release_with_images':(0x416720,0x41678D),
 'image_release':(0x40FE10,0x40FE44),
 'image_invalidate_screen_rectangle':(0x4164C0,0x416511),
 'screen_rectangle_dirty':(0x43EB50,0x43EBF4),
 'native_memset':(0x4D0C60,0x4D0CF0),
 'image_damage_overlay_counts_initialize':(0x40FBE0,0x40FC45),
 'image_set_offsets':(0x4117D0,0x4117F2),
}
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--exe',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
 raw=a.exe.read_bytes();assert hashlib.sha256(raw).hexdigest()==TARGET_HASH
 pe=pefile.PE(data=raw);assert pe.OPTIONAL_HEADER.ImageBase==0x400000 and pe.FILE_HEADER.Machine==0x14C
 md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);witnesses={}
 for name,(lo,hi) in BLOCKS.items():
  code=pe.get_data(lo-0x400000,hi-lo);ins=list(md.disasm(code,lo));length=sum(x.size for x in ins)
  witnesses[name]=dict(preferred_va=hex(lo),rva=hex(lo-0x400000),end_exclusive=hex(hi),requested_bytes=len(code),decoded_bytes=length,undecoded_tail=code[length:].hex(),instructions=[dict(va=hex(x.address),rva=hex(x.address-0x400000),file_offset=hex(pe.get_offset_from_rva(x.address-0x400000)),bytes=x.bytes.hex(),mnemonic=x.mnemonic,operands=x.op_str) for x in ins])
 result=dict(target_sha256=TARGET_HASH,target_version='1.17.0.1',original_code_executed=False,extractor_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),witnesses=witnesses,iscript_dispatch=dict(preferred_va='0x4111e4',destinations=[hex(x) for x in struct.unpack('<69I',pe.get_data(0x111E4,276))]),native_dat_descriptors={key:[dict(index=i,target_va=hex(struct.unpack('<3I',pe.get_data(base-0x400000+i*12,12))[0]),entry_width=struct.unpack('<3I',pe.get_data(base-0x400000+i*12,12))[1],count=struct.unpack('<3I',pe.get_data(base-0x400000+i*12,12))[2]) for i in range(count)] for key,base,count in (('units',0x5014F0,54),('flingy',0x501148,7),('sprites',0x501408,6),('images',0x5011A8,14))},unit_remove_switch=dict(destinations=[hex(x) for x in struct.unpack('<6I',pe.get_data(0x25500,24))],mapping=list(pe.get_data(0x25518,143))),draw_update_callbacks=[hex(x) for x in struct.unpack('<36I',pe.get_data(0x103044,144))[::2]])
 a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_text(json.dumps(result,indent=2)+'\n')
 print(json.dumps(dict(blocks=len(witnesses),instructions=sum(len(x['instructions']) for x in witnesses.values()),tails={k:v['undecoded_tail'] for k,v in witnesses.items() if v['undecoded_tail']})))
if __name__=='__main__':main()
