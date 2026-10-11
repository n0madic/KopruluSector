#!/usr/bin/env python3
"""Extract exact-hash visible Death and image/sprite/Thingy reclamation witnesses."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import capstone
import pefile
TARGET_HASH='94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0'
BLOCKS={
 'object_step_order_static_only':(0x445070,0x4450F1),
 'primary_order_dispatcher':(0x439420,0x4399C0),
 'death_order_consumer':(0x41E630,0x41E6F4),
 'transfer_subunit_main_image':(0x415540,0x415589),
 'hidden_unit_add':(0x425DC0,0x425DD0),
 'active_unit_remove':(0x427080,0x427090),
 'hide_unit':(0x422BA0,0x422C18),
 'sprite_map_row':(0x416AB0,0x416AD3),
 'iscript_overlay_sprite':(0x410320,0x410371),
 'create_lone_thingy':(0x417B80,0x417BF4),
 'thingy_initial_visibility':(0x417E50,0x417EE7),
 'thingy_footprint_visibility':(0x417C10,0x417D13),
 'thingy_custom_footprint_visibility':(0x417D20,0x417E4C),
 'thingy_footprint_explored_mask':(0x418D00,0x418D74),
 'thingy_footprint_visible_mask':(0x418D80,0x418DF3),
 'lone_thingy_object_step':(0x418990,0x4189C8),
 'thingy_update_and_reclaim':(0x418B50,0x418BD8),
 'iscript_sound_at_image':(0x40F5A0,0x40F5CF),
 'sound_position_backend_guard':(0x446A20,0x446B2C),
 'image_release':(0x40FE10,0x40FE44),
 'sound_backend_guard':(0x446940,0x446A1C),
}
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--exe',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
 raw=a.exe.read_bytes();assert hashlib.sha256(raw).hexdigest()==TARGET_HASH
 pe=pefile.PE(data=raw);assert pe.OPTIONAL_HEADER.ImageBase==0x400000 and pe.FILE_HEADER.Machine==0x14C
 md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);witnesses={}
 for name,(lo,hi) in BLOCKS.items():
  code=pe.get_data(lo-0x400000,hi-lo);ins=list(md.disasm(code,lo));length=sum(x.size for x in ins)
  witnesses[name]=dict(preferred_va=hex(lo),rva=hex(lo-0x400000),end_exclusive=hex(hi),requested_bytes=len(code),decoded_bytes=length,undecoded_tail=code[length:].hex(),instructions=[dict(va=hex(x.address),rva=hex(x.address-0x400000),file_offset=hex(pe.get_offset_from_rva(x.address-0x400000)),bytes=x.bytes.hex(),mnemonic=x.mnemonic,operands=x.op_str) for x in ins])
 result=dict(target_sha256=TARGET_HASH,target_version='1.17.0.1',original_code_executed=False,extractor_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),witnesses=witnesses,primary_death_dispatch=dict(preferred_va='0x4399c0',destinations=[hex(x) for x in struct.unpack('<5I',pe.get_data(0x399C0,20))],mapping=list(pe.get_data(0x399D4,175)),death_order=0,death_case_va='0x439448',tail_target_va='0x41e630'),iscript_dispatch=dict(preferred_va='0x4111e4',destinations=[hex(x) for x in struct.unpack('<69I',pe.get_data(0x111E4,276))]),native_dat_descriptors={key:[dict(index=i,target_va=hex(struct.unpack('<3I',pe.get_data(base-0x400000+i*12,12))[0]),entry_width=struct.unpack('<3I',pe.get_data(base-0x400000+i*12,12))[1],count=struct.unpack('<3I',pe.get_data(base-0x400000+i*12,12))[2]) for i in range(count)] for key,base,count in (('units',0x5014F0,54),('flingy',0x501148,7),('sprites',0x501408,6),('images',0x5011A8,14))},unit_remove_switch=dict(destinations=[hex(x) for x in struct.unpack('<6I',pe.get_data(0x25500,24))],mapping=list(pe.get_data(0x25518,143))),draw_update_callbacks=[hex(x) for x in struct.unpack('<36I',pe.get_data(0x103044,144))[::2]])
 a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_text(json.dumps(result,indent=2)+'\n')
 print(json.dumps(dict(blocks=len(witnesses),instructions=sum(len(x['instructions']) for x in witnesses.values()),tails={k:v['undecoded_tail'] for k,v in witnesses.items() if v['undecoded_tail']})))
if __name__=='__main__':main()
