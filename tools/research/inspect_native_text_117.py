#!/usr/bin/env python3
"""Inspect stock plaintext fonts, byte string tables and original text code."""
import argparse, hashlib, json, struct
from collections import Counter
from pathlib import Path
import capstone, pefile
HASH='94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0'
BLOCKS={
 'byte_string_table_lookup':(0x4C13B0,0x4C13D7),
 'glyph_pointer':(0x4CDE90,0x4CDEB7),'character_width':(0x4CDC30,0x4CDCD1),
 'glyph_packet_blitter':(0x4CFDF0,0x4CFE5F),'bounded_text':(0x4CD670,0x4CD9C7),
 'line_parser':(0x4CD100,0x4CD2E4),'multiline_layout':(0x4CD9F0,0x4CDBE9),
 'vertical_alignment':(0x4CDBF0,0x4CDC2D),'font_setup':(0x4CE160,0x4CE23F),
 'highlight_setter':(0x4CE240,0x4CE247),'color_setter':(0x4CE250,0x4CE367),
 'font_loader_static':(0x4CDEC0,0x4CDF1F),'text_layout_wrapper_static':(0x4CDF20,0x4CE157),
 'locale_boundary_static':(0x4C7730,0x4C77C1),'font_read_decrypt_wrapper_static':(0x4657F0,0x46584A),
}
TABLES={'char_width_classes':(0x4CDCE4,32,1),'char_width_targets':(0x4CDCD4,4,4),
 'bounded_text_classes':(0x4CD9D0,31,1),'bounded_text_targets':(0x4CD9C8,2,4),
 'line_parser_classes':(0x4CD2EC,31,1),'line_parser_targets':(0x4CD2E4,2,4),
 'color_targets':(0x4CE368,30,4)}
FONTS=['font8','font10','font14','font16','font16x']
def sha(b):return hashlib.sha256(b).hexdigest()
def font(b):
 assert b[:4]==b'FONT';low,high,maxw,maxh=b[4:8];offsets=struct.unpack_from('<'+'I'*(high-low+1),b,8);glyphs={}
 for char,offset in enumerate(offsets,low):
  if not offset:continue
  assert 8+4*len(offsets)<=offset<len(b)-4
  width,height,x,y=b[offset:offset+4];assert width and height
  pixels,consumed=packets(b[offset+4:],width,height)
  glyphs[char]=dict(offset=offset,width=width,height=height,x_offset=x,y_offset=y,packet_bytes=consumed,pixel_sha256=sha(pixels))
 return dict(low=low,high=high,max_width=maxw,max_height=maxh,offsets=offsets,glyphs=glyphs)
def packets(b,width,height,initial=0xA5,lookup=bytes(range(8))):
 # A positive skip with mask0 consumes no glyph pixel. A zero skip with
 # mask0 does write lookup[0]. Skips can cross a row or the final boundary.
 out=bytearray([initial]*(width*height));position=0;at=0
 while position<len(out):
  value=b[at];at+=1;skip,mask=value>>3,value&7;position+=skip
  if position>=len(out):break
  if not skip or mask:out[position]=lookup[mask];position+=1
  assert at<=len(b)
 return bytes(out),at
def string_table(b):
 count=struct.unpack_from('<H',b)[0];offsets=struct.unpack_from('<'+'H'*count,b,2);assert all(2+2*count<=x<len(b) for x in offsets)
 unique=sorted(set(offsets));bounds={start:end for start,end in zip(unique,unique[1:]+[len(b)])};rows=[]
 for index,off in enumerate(offsets,1):
  block=b[off:bounds[off]];nul=block.find(b'\0');assert nul>=0
  first=block[:nul];rows.append(dict(index=index,offset=off,allocated_bytes=len(block),substring_bytes=len(first),embedded_nuls=max(0,block.count(0)-1),first_sha256=sha(first),high_bytes=sum(x>=128 for x in first),control_histogram=dict(Counter(str(x) for x in first if x<32))))
 return dict(count=count,unique_offsets=len(unique),rows=rows),[b[x:b.index(0,x)] for x in offsets]
def main():
 p=argparse.ArgumentParser(description=__doc__)
 for key in ('exe','assets','asset-manifest','out'):p.add_argument('--'+key,type=Path,required=True)
 p.add_argument('--campaign-data',type=Path);a=p.parse_args();raw=a.exe.read_bytes();assert sha(raw)==HASH;pe=pefile.PE(data=raw);md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);blocks={}
 for key,(lo,hi) in BLOCKS.items():
  blob=pe.get_data(lo-0x400000,hi-lo);ins=list(md.disasm(blob,lo));assert sum(i.size for i in ins)==len(blob),(key,len(blob),sum(i.size for i in ins));blocks[key]=[dict(va=hex(i.address),rva=hex(i.address-0x400000),file_offset=hex(pe.get_offset_from_rva(i.address-0x400000)),bytes=i.bytes.hex(),mnemonic=i.mnemonic,operands=i.op_str) for i in ins]
 tables={key:dict(va=hex(lo),values=list(struct.unpack('<'+('B' if n==1 else 'I')*count,pe.get_data(lo-0x400000,count*n)))) for key,(lo,count,n) in TABLES.items()};am=json.loads(a.asset_manifest.read_text());fonts={};strings={}
 for name in FONTS:
  path='files\\font\\'+name+'.fnt';b=(a.assets/path.replace('\\','/')).read_bytes();assert sha(b)==am['assets'][path]['sha256'];f=font(b);f.pop('offsets');fonts[path]=dict(asset=am['assets'][path],**f)
 for path in ('rez\\stat_txt.tbl','rez\\gluAll.tbl'):
  b=(a.assets/path.replace('\\','/')).read_bytes();assert sha(b)==am['assets'][path]['sha256'];strings[path]=dict(asset=am['assets'][path],**string_table(b)[0])
 campaigns=[]
 if a.campaign_data:
  for path in sorted((a.campaign_data/'campaign').rglob('scenario.chk')):
   b=path.read_bytes();at=0;sections=[]
   while at<len(b):
    key=b[at:at+4];size=struct.unpack_from('<I',b,at+4)[0];part=b[at+8:at+8+size];assert len(part)==size;at+=8+size
    if key==b'STR ':sections.append(dict(section_bytes=size,section_sha256=sha(part),**string_table(part)[0]))
   campaigns.append(dict(path=str(path.relative_to(a.campaign_data)),chk_sha256=sha(b),str_sections=sections))
 result=dict(target_sha256=HASH,target_version='1.17.0.1',original_code_executed=False,extractor_sha256=sha(Path(__file__).read_bytes()),asset_manifest_sha256=sha(a.asset_manifest.read_bytes()),blocks=blocks,tables=tables,fonts=fonts,string_tables=strings,campaigns=campaigns,limits=['Plaintext BroodWar files\\font inputs are genuine stock resources, but decoded effective StarDat font\\ equivalence is unproved','Only byte-preserving string inventories; Latin1 is a reversible transport, not a proved display encoding','Visible bounded-text and nonwrapping line parser call GetUserDefaultLangID; no imports execute','Encrypted key/owner resources remain private; no decoding or secret publication'])
 a.out.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(dict(blocks=len(blocks),instructions=sum(map(len,blocks.values())),fonts=len(fonts),campaigns=len(campaigns),sha256=result['extractor_sha256'])))
if __name__=='__main__':main()
