#!/usr/bin/env python3
"""Extract exact-hash pure image/LO/row/depth helpers and static loader/wrapper boundaries."""
import argparse,hashlib,json,struct
from pathlib import Path
import capstone,pefile
import inspect_native_image_initialization_117 as image_source
import inspect_native_primary_orders_117 as order_source
import inspect_native_unit_creation_117 as creation_source
import inspect_native_training_lifecycle_117 as lifecycle_source
HASH=image_source.TARGET_HASH
BLOCKS={k:image_source.BLOCKS[k] for k in ('image_bounds','image_frame_refresh','image_set_direction','sprite_set_direction','image_set_flingy_direction','image_overlay_offset','image_set_offsets','image_playframe','image_draw_function_methods','image_initialize','image_drawing_and_init_animation','image_find_iscript_header','image_animation','iscript_vm','image_overlay_allocate','image_draw_update_noop')}
BLOCKS.update({k:order_source.BLOCKS[k] for k in ('list_insert_after','list_insert_after_head','list_insert_before','list_remove')})
BLOCKS.update({k:lifecycle_source.BLOCKS[k] for k in ('image_allocate','sprite_add_image')})
BLOCKS['move_sprite']=creation_source.BLOCKS['move_sprite']
BLOCKS.update({'sprite_depth_key':(0x4160B0,0x4160DE),'shield_lo_offset':(0x40F6A0,0x40F6D1),'image_follow_main_offset':(0x410380,0x4103C0),'image_follow_main_enable':(0x40F3A0,0x40F41C),'image_before_head':(0x415260,0x4152C4),'image_after_main':(0x4152D0,0x415335),'image_range_search':(0x416940,0x41696B),'image_resource_load_static':(0x40FEF0,0x41008A),'image_resource_release_static':(0x40F420,0x40F488),'raw_rng':(0x478200,0x47823E)})
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--exe',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
 raw=a.exe.read_bytes();assert hashlib.sha256(raw).hexdigest()==HASH
 pe=pefile.PE(data=raw);md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);out={}
 for name,(lo,hi) in BLOCKS.items():
  b=pe.get_data(lo-0x400000,hi-lo);ins=list(md.disasm(b,lo));assert sum(i.size for i in ins)==len(b),(name,len(b),sum(i.size for i in ins))
  out[name]=[dict(va=hex(i.address),rva=hex(i.address-0x400000),bytes=i.bytes.hex(),mnemonic=i.mnemonic,operands=i.op_str) for i in ins]
 result=dict(target_sha256=HASH,target_version='1.17.0.1',original_code_executed=False,extractor_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),blocks=out,static_only_blocks=['image_resource_load_static','image_resource_release_static'],evidence_boundary='Pure helper instruction inventory plus unexecuted resource loader/import wrappers; no renderer or pixel observation')
 a.out.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(dict(blocks=len(out),instructions=sum(len(v) for v in out.values()),sha256=result['extractor_sha256'])))
if __name__=='__main__':main()
