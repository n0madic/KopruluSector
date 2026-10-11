#!/usr/bin/env python3
"""Inspect exact native THG2 producer contracts and authenticated special-object refs."""
import argparse
from collections import Counter,defaultdict
import hashlib,json,struct
from pathlib import Path
import capstone,pefile
HASH='94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0'
TRAPS={203,209,210,211,212,213};DOORS={205,206,207,208};POWERUPS={128,129,*range(215,228)};SPECIAL=TRAPS|DOORS|POWERUPS
BLOCKS={'thg2_section_loader':(0x468BA0,0x468CD4),'initial_doodad_disabled':(0x428F70,0x429065),'death_issue_and_drop':(0x42A4D0,0x42A529),'created_unit_complete_producer':(0x4261F0,0x426527)}

def sections(blob):
 at=0
 while at<len(blob):
  assert at+8<=len(blob);tag,n=struct.unpack_from('<4sI',blob,at);assert at+8+n<=len(blob)
  yield tag,blob[at+8:at+8+n];at+=8+n

def campaign_inputs(data,manifest):
 result=dict(maps=[],thg2_special=[],unit_special=[],create_actions=[]);thg_hist=Counter();flag_hist=Counter();sprite_count=0;total=0
 for maprow in manifest['campaign_found']:
  b=(data/maprow['path'].replace('\\','/')).read_bytes();assert hashlib.sha256(b).hexdigest()==maprow['sha256'];secs=defaultdict(list)
  for tag,v in sections(b):secs[tag].append(v)
  row=dict(path=maprow['path'],sha256=maprow['sha256'],version=struct.unpack('<H',secs[b'VER '][0])[0],dimensions=list(struct.unpack('<2H',secs[b'DIM '][0])),thg2_sections=len(secs[b'THG2']),thg2_records=sum(len(v)//10 for v in secs[b'THG2']))
  result['maps'].append(row)
  for secidx,v in enumerate(secs[b'THG2']):
   assert len(v)%10==0
   for j in range(0,len(v),10):
    typ,x,y,owner,unused,flags=struct.unpack_from('<HHHBBH',v,j);flag_hist[flags]+=1;total+=1
    if flags&0x1000:sprite_count+=1
    else:thg_hist[typ]+=1
    if not flags&0x1000 and typ in SPECIAL:
     result['thg2_special'].append(dict(map=row['path'],map_sha256=row['sha256'],section_index=secidx,record_index=j//10,type=typ,x=x,y=y,owner=owner,unused=unused,flags=flags,record_sha256=hashlib.sha256(v[j:j+10]).hexdigest()))
  for secidx,v in enumerate(secs[b'UNIT']):
   assert len(v)%36==0
   for j in range(0,len(v),36):
    typ=struct.unpack_from('<H',v,j+8)[0]
    if typ in SPECIAL:result['unit_special'].append(dict(map=row['path'],map_sha256=row['sha256'],section_index=secidx,record_index=j//36,type=typ,x=struct.unpack_from('<H',v,j+4)[0],y=struct.unpack_from('<H',v,j+6)[0],owner=v[j+16],record_sha256=hashlib.sha256(v[j:j+36]).hexdigest()))
  for secidx,v in enumerate(secs[b'TRIG']):
   assert len(v)%2400==0
   for trig in range(len(v)//2400):
    raw=v[trig*2400:(trig+1)*2400];terminated=False
    for index in range(64):
     act=raw[320+index*32:352+index*32];typ=struct.unpack_from('<H',act,24)[0]
     if act[26] in (11,44) and typ in SPECIAL:result['create_actions'].append(dict(map=row['path'],map_sha256=row['sha256'],section_index=secidx,trigger_index=trig,action_index=index,type=typ,action=act[26],quantity=act[27],player=struct.unpack_from('<I',act,16)[0],location=struct.unpack_from('<I',act)[0],properties=struct.unpack_from('<I',act,20)[0],flags=act[28],after_enabled_none=terminated,execution_flags=struct.unpack_from('<I',raw,2368)[0],owners=list(raw[2372:2400]),action_sha256=hashlib.sha256(act).hexdigest()))
     if act[26]==0 and not act[28]&2:terminated=True
 result.update(physical_maps=len(result['maps']),supported_version_maps=sum(r['version'] in (59,63,205) for r in result['maps']),thg2_records=total,thg2_sprite_flag_records=sprite_count,thg2_unitlike_records=sum(thg_hist.values()),thg2_unitlike_type_histogram=dict(sorted(thg_hist.items())),thg2_flags_histogram={hex(k):v for k,v in sorted(flag_hist.items())},thg2_selected_types_histogram=dict(sorted(Counter(r['type'] for r in result['thg2_special']).items())),unit_selected_types_histogram=dict(sorted(Counter(r['type'] for r in result['unit_special']).items())),create_action_type_histogram=dict(sorted(Counter(r['type'] for r in result['create_actions']).items())),scope='Raw static source refs, including ignored/disabled actions; input coverage does not prove mission execution or campaign play order')
 return result

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--exe',type=Path,required=True);p.add_argument('--data',type=Path,required=True);p.add_argument('--manifest',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
 raw=a.exe.read_bytes();assert hashlib.sha256(raw).hexdigest()==HASH;pe=pefile.PE(data=raw);assert pe.OPTIONAL_HEADER.ImageBase==0x400000 and pe.FILE_HEADER.Machine==0x14C
 md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);witnesses={}
 for name,(lo,hi) in BLOCKS.items():
  code=pe.get_data(lo-0x400000,hi-lo);ins=list(md.disasm(code,lo));assert sum(i.size for i in ins)==len(code),name
  witnesses[name]=dict(preferred_va=hex(lo),rva=hex(lo-0x400000),end_exclusive=hex(hi),instructions=[dict(va=hex(i.address),rva=hex(i.address-0x400000),file_offset=hex(pe.get_offset_from_rva(i.address-0x400000)),bytes=i.bytes.hex(),mnemonic=i.mnemonic,operands=i.op_str) for i in ins])
 manifest=json.loads(a.manifest.read_text());result=dict(target_sha256=HASH,target_version='1.17.0.1',original_code_executed=False,extractor_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),retail_manifest_sha256=hashlib.sha256(a.manifest.read_bytes()).hexdigest(),witnesses=witnesses,campaign_inputs=campaign_inputs(a.data,manifest),thg2_loader=dict(preferred_va='0x468ba0',stride=10,length_failure_return='0x468bc4',allocation_call='0x468bd5',allocation_import_thunk='0x4d001c',body_fragment_entry='0x468c0f',body_fragment_stop_before='0x468cb6',free_call='0x468cc3',free_import_thunk='0x4d0022',sprite_flag='0x1000',disabled_flag='0x8000',forced_neutral_types=[205,206,207,208],positive_whole_loader_executed=False))
 a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(dict(blocks=len(witnesses),instructions=sum(len(v['instructions']) for v in witnesses.values()),thg2_selected=len(result['campaign_inputs']['thg2_special']),create_refs=len(result['campaign_inputs']['create_actions']))))
if __name__=='__main__':main()
