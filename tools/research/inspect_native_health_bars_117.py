#!/usr/bin/env python3
"""Inspect original HP/shield/energy/timed bar arithmetic and draw consumers."""
import argparse,hashlib,json,struct
from pathlib import Path
import pefile,capstone
import inspect_native_pixel_effects_117 as pixel_source
import inspect_native_primary_orders_117 as list_source
HASH=pixel_source.HASH
PINNED={'inspect_native_pixel_effects_117.py':'048d7c647846c80c7d1f753c05f140a7b9332418f7d9b28e1bb83709292b907e','inspect_native_primary_orders_117.py':'1888da84f424eff2157e68db2af5604e940de4cb6d84d3f29fe11543c8e0e839'}
BLOCKS={k:pixel_source.BLOCKS[k] for k in ('native_memset','horizontal_line','vertical_line','image_bounds','sprite_draw','image_dirty_rectangle_predicate','set_player_color','size_outline','iscript_vm')}
BLOCKS.update({
 'health_bar_percent_round':(0x4648A0,0x464915),'other_bar_percent_round':(0x464920,0x464992),
 'health_or_energy_bar_draw':(0x4649A0,0x464AA5),'shield_bar_draw':(0x464AB0,0x464BB5),
 'shield_top_bar_draw':(0x464BC0,0x464D1A),'unit_bars_draw':(0x464D20,0x464F8A),
 'maximum_energy':(0x434940,0x4349D6),'maximum_energy_display':(0x434A80,0x434A89),
 'maximum_timed_life':(0x434A90,0x434AB2),'upgrade_level':(0x49A470,0x49A496),
 'detected_blend_owner_rows':(0x416BD0,0x416CC7),'get_owner_color':(0x41D4F0,0x41D537),
 'health_image_attach':(0x40EB60,0x40EB9D),'health_image_configure':(0x40F6E0,0x40F810),
 'health_image_pool_initialize':(0x40F810,0x40F890),'image_find_iscript_header':(0x40F340,0x40F398),
 'cycling_palette_index_predicate':(0x4697C0,0x469801),
 'mark_existing_health_images_dirty':(0x416BB0,0x416BCA),
 'sprite_selected_health_attach':(0x417110,0x417153),
 'sprite_selected_images_remove':(0x415A00,0x415A78),
 'health_image_release':(0x40F540,0x40F56D),
 'selection_circle_release':(0x40F570,0x40F59D),
 'image_invalidate_rectangle':(0x4164C0,0x416511),
 'screen_rectangle_dirty':(0x43EB50,0x43EBF4),
})
BLOCKS.update({k:list_source.BLOCKS[k] for k in ('list_insert_before','list_remove','list_insert_after_head')})
def dependencies():
 result={name:hashlib.sha256((Path(__file__).parent/name).read_bytes()).hexdigest() for name in PINNED};assert result==PINNED;return result
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--exe',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args();raw=a.exe.read_bytes();assert hashlib.sha256(raw).hexdigest()==HASH
 deps=dependencies();pe=pefile.PE(data=raw);md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);blocks={}
 for name,(lo,hi) in BLOCKS.items():
  b=pe.get_data(lo-0x400000,hi-lo);ins=list(md.disasm(b,lo));assert sum(i.size for i in ins)==len(b),(name,len(b),sum(i.size for i in ins));blocks[name]=[dict(va=hex(i.address),rva=hex(i.address-0x400000),file_offset=hex(pe.get_offset_from_rva(i.address-0x400000)),bytes=i.bytes.hex(),mnemonic=i.mnemonic,operands=i.op_str) for i in ins]
 result=dict(target_sha256=HASH,target_version='1.17.0.1',original_code_executed=False,extractor_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),source_dependencies_sha256=deps,blocks=blocks,bar_class_tables=[dict(va=hex(addr),bytes=pe.get_data(addr-0x400000,n).hex()) for addr,n in ((0x5085D0,18),(0x5085E4,6),(0x5085EC,5),(0x5085F4,24),(0x50860C,7),(0x508614,4),(0x508618,4))],evidence_boundary='Reviewed native instructions, not original execution or rendered observation')
 a.out.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(dict(blocks=len(blocks),instructions=sum(map(len,blocks.values())),sha256=result['extractor_sha256'])))
if __name__=='__main__':main()
