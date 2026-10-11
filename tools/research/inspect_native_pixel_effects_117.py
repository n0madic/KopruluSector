#!/usr/bin/env python3
"""Inspect exact original draw effects, callback state and palette-cycle leaves."""
import argparse,hashlib,json,struct
from pathlib import Path
import capstone,pefile
import inspect_native_pixel_blitters_117 as pixel_source
import inspect_native_image_initialization_117 as image_source
HASH=pixel_source.HASH
PINNED={'inspect_native_pixel_blitters_117.py':'063df6541e477b9543efd3b6ee3b68f7fe4b3e96507da8c676beb4bb7f348f55',
 'inspect_native_image_initialization_117.py':'cb3cc883fe51a3109910928a51fd0c2a65251bf23782bec1cebb093c722342a6'}
BLOCKS={k:pixel_source.BLOCKS[k] for k in ('normal','normal_mirrored','blend','blend_mirrored','shadow','shadow_mirrored','background_remap','background_remap_mirrored','fire','fire_mirrored','set_player_color')}
BLOCKS.update({k:image_source.BLOCKS[k] for k in ('iscript_vm','image_draw_function_methods','image_bounds','native_memset','image_draw_update_noop')})
BLOCKS.update({
 'mask_texture':(0x405F64,0x4060C3),'mask_texture_mirrored':(0x4060C3,0x406235),
 'threshold':(0x406235,0x4063B5),'threshold_mirrored':(0x406A84,0x406C09),
 'cloak_displacement':(0x4063B5,0x406533),'cloak_displacement_mirrored':(0x406C09,0x406D8E),
 'raw_displacement':(0x406533,0x4066A1),'raw_displacement_mirrored':(0x406D8E,0x406F03),
 'cloak_in_update':(0x40E570,0x40E5EA),'cloak_out_update':(0x40E5F0,0x40E64E),'warp_in_update':(0x40E650,0x40E6BB),
 'cloak_transition':(0x40E6C0,0x40E6F3),'cloak_transition_mirrored':(0x40E700,0x40E733),
 'detected_transition':(0x40E740,0x40E777),'detected_transition_mirrored':(0x40E780,0x40E7B7),
 'detected':(0x40E7C0,0x40E7D8),'detected_mirrored':(0x40E7E0,0x40E7F8),
 'warp_texture':(0x40E800,0x40E873),'warp_texture_mirrored':(0x40E880,0x40E8F3),
 'size_outline':(0x40E9D0,0x40EA5C),'horizontal_line':(0x4CCB30,0x4CCB6A),'vertical_line':(0x4CCB70,0x4CCBB5),
 'copy_cloak_parent':(0x40EF80,0x40EFEC),'unit_draw_status':(0x41D4C0,0x41D4E1),
 'cloak_begin':(0x40ED00,0x40ED30),'decloak_begin':(0x40ED30,0x40ED88),
 'sprite_draw':(0x415A80,0x415B2B),'image_dirty_rectangle_predicate':(0x417790,0x41783D),
 'palette_brightness_indices':(0x445F30,0x445F7C),
 'stock_cycle_bootstrap_selector':(0x46A390,0x46A3A6),'cycle_bootstrap':(0x46A3B0,0x46A418),
 'cycle_schedule':(0x4CCBC0,0x4CCC52),'cycle_pattern':(0x4CCC60,0x4CCCAC),'cycle_rotate':(0x4CCCF0,0x4CCD2F),
 'cycle_driver':(0x4CCD30,0x4CCD5E),'cycle_reset':(0x4CCF00,0x4CCF15),
 'cycle_set':(0x4CD000,0x4CD035),'cycle_enable':(0x4CD070,0x4CD07E),'palette_set':(0x4CD040,0x4CD064),
 'brightness_set':(0x4CF930,0x4CF956),
 # Complete original return paths selected by declared CPU flag and aligned,
 # non-overlapping palette-set input; no replacement of the native memmove.
 'memmove_forward_rep_entry':(0x4D16A0,0x4D16D1),'memmove_forward_return':(0x4D19E8,0x4D19EF),
 'cycle_memmove_entry':(0x4D2360,0x4D2391),'cycle_memmove_forward_return':(0x4D26A8,0x4D26AF),
 'cycle_memmove_backward_aligned':(0x4D26E8,0x4D271C),
 'cycle_memmove_backward_small':(0x4D27E8,0x4D2834),'cycle_memmove_backward_return':(0x4D2844,0x4D284B),
 'palette_upload_static':(0x4CF8C0,0x4CF8FF),'palette_fade_static':(0x4CCD60,0x4CCE48),
 'palette_device_static':(0x4CFD60,0x4CFD92),'palette_device_thunk_static':(0x4D0106,0x4D010C),
 'health_bars_static':(0x464D20,0x464F8A),
 'palette_brightness_rgb_static':(0x4CF760,0x4CF86F),
})
STATIC_ONLY={'palette_upload_static','palette_fade_static','palette_brightness_rgb_static','palette_device_static','palette_device_thunk_static','health_bars_static'}
def dependencies():
 result={name:hashlib.sha256((Path(__file__).parent/name).read_bytes()).hexdigest() for name in PINNED}
 assert result==PINNED,(result,PINNED)
 return result
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--exe',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
 raw=a.exe.read_bytes();assert hashlib.sha256(raw).hexdigest()==HASH;deps=dependencies();pe=pefile.PE(data=raw);md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);blocks={}
 for name,(lo,hi) in BLOCKS.items():
  b=pe.get_data(lo-0x400000,hi-lo);ins=list(md.disasm(b,lo));assert sum(i.size for i in ins)==len(b),(name,len(b),sum(i.size for i in ins))
  blocks[name]=[dict(va=hex(i.address),rva=hex(i.address-0x400000),file_offset=hex(pe.get_offset_from_rva(i.address-0x400000)),bytes=i.bytes.hex(),mnemonic=i.mnemonic,operands=i.op_str) for i in ins]
 table=[dict(method=i,update=hex(struct.unpack('<I',pe.get_data(0x103044+i*8,4))[0]),normal=hex(struct.unpack('<I',pe.get_data(0x1030D4+i*12,4))[0]),mirrored=hex(struct.unpack('<I',pe.get_data(0x1030D8+i*12,4))[0])) for i in range(18)]
 result=dict(target_sha256=HASH,target_version='1.17.0.1',original_code_executed=False,extractor_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),source_dependencies_sha256=deps,blocks=blocks,drawing_dispatch=table,static_only_blocks=sorted(STATIC_ONLY),evidence_boundary='Draw kernels and callback/palette memory contracts; original device upload and RGB gamma path static only; no rendered observation')
 a.out.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(dict(blocks=len(blocks),instructions=sum(map(len,blocks.values())),sha256=result['extractor_sha256'])))
if __name__=='__main__':main()
