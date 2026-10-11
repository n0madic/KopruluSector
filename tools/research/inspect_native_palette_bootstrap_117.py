#!/usr/bin/env python3
"""Inspect exact original all-tileset palette definitions and pure RGB math."""
import argparse,hashlib,json,struct
from pathlib import Path
import capstone,pefile
import inspect_native_pixel_effects_117 as pixel
HASH=pixel.HASH
PINNED='048d7c647846c80c7d1f753c05f140a7b9332418f7d9b28e1bb83709292b907e'
BLOCKS={k:pixel.BLOCKS[k] for k in ('stock_cycle_bootstrap_selector','cycle_bootstrap','cycle_schedule','cycle_pattern','cycle_rotate','cycle_driver','cycle_reset','cycle_set','cycle_enable','native_memset','cycle_memmove_entry','cycle_memmove_forward_return','cycle_memmove_backward_aligned','cycle_memmove_backward_small','cycle_memmove_backward_return','palette_set','palette_brightness_rgb_static','brightness_set','palette_upload_static')}
BLOCKS.update({'cycling_palette_index_predicate':(0x4697C0,0x469801),'native_pow_sse':(0x4D5C70,0x4D67E0)})
BLOCKS.update({'palette_grayscale_fade_entry':(0x4CCF40,0x4CCFE9),'palette_fade_from_black':(0x4CCE50,0x4CCEAB),'palette_fade_to_black':(0x4CCEB0,0x4CCEFB),'palette_fade_clock_boundary':(0x4CCD60,0x4CCE48),'palette_device_static':(0x4CFD60,0x4CFD92)})
# Selected ordinary forward memmove implementation for brightness100 copy.
for key in ('memmove_forward_rep_entry','memmove_forward_return'):
 BLOCKS[key]=pixel.BLOCKS[key]
TILESETS=['badlands','platform','install','ashworld','jungle','desert','ice','twilight']
def dependencies():
 h=hashlib.sha256(Path(pixel.__file__).read_bytes()).hexdigest();assert h==PINNED;return {Path(pixel.__file__).name:h}
def definitions(pe):
 image=pe.get_memory_mapped_image();result=[]
 for i,name in enumerate(TILESETS):
  ptr=struct.unpack('<I',pe.get_data(0x1089B0+i*4,4))[0];pos=ptr+4;rows=[]
  while image[pos-0x400000+4]:
   pattern,speed,low,high,count=struct.unpack_from('<I4B',image,pos-0x400000);rows.append(dict(pattern_pointer=hex(pattern),speed=speed,low=low,high=high,count=count));pos+=8;assert len(rows)<=8
  size=pos-ptr+8;blob=image[ptr-0x400000:pos-0x400000+8];assert len(blob)==size
  initialized=any(s.VirtualAddress<=ptr-0x400000 and ptr-0x400000+size<=s.VirtualAddress+s.SizeOfRawData for s in pe.sections)
  result.append(dict(tileset=i,name=name,definition_va=hex(ptr),active_mask=struct.unpack_from('<H',blob)[0],definition_bytes=size,definition_sha256=hashlib.sha256(blob).hexdigest(),initialized_file_data=initialized,mapped_pe_zero_only=not initialized,cycles=rows))
 return result
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--exe',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args();raw=a.exe.read_bytes();assert hashlib.sha256(raw).hexdigest()==HASH;pe=pefile.PE(data=raw);md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);md.skipdata=True;blocks={}
 for key,(lo,hi) in BLOCKS.items():
  b=pe.get_data(lo-0x400000,hi-lo);ins=list(md.disasm(b,lo));assert sum(i.size for i in ins)==len(b);assert all(i.mnemonic!='.byte' for i in ins),(key,'undecoded data');blocks[key]=[dict(va=hex(i.address),rva=hex(i.address-0x400000),file_offset=hex(pe.get_offset_from_rva(i.address-0x400000)),bytes=i.bytes.hex(),mnemonic=i.mnemonic,operands=i.op_str) for i in ins]
 result=dict(target_sha256=HASH,target_version='1.17.0.1',original_code_executed=False,extractor_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),source_dependencies_sha256=dependencies(),blocks=blocks,tileset_definitions=definitions(pe),static_boundaries=['Device adapter4cfd60 and all external imports remain unexecuted; upload4cf8c0 preparation is an explicit prefix','Clock fade4ccd60 and presentation4c5360 are unexecuted','SSE pow fallback4d67c4->4e0824 is not an approved execution range','MappedPE zero definition66fd40 is not a proved post-loader mutation invariant'])
 a.out.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(dict(blocks=len(blocks),instructions=sum(map(len,blocks.values())),sha256=result['extractor_sha256'])))
if __name__=='__main__':main()
