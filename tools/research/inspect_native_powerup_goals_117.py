#!/usr/bin/env python3
"""Inspect carried-object destruction and authentic campaign goal consumers."""
import argparse,hashlib,json,struct
from pathlib import Path
from collections import Counter,defaultdict
import capstone,pefile
from inspect_native_special_producers_117 import HASH,POWERUPS,sections
BLOCKS={'kill_carried_powerup':(0x4271D0,0x427203),'carried_powerup_getter':(0x421600,0x421618),'remove_issuer':(0x425700,0x42570C),'ctf_mode_guard':(0x447CB0,0x447CBD)}
def campaign_inputs(data,manifest):
 result=dict(maps=[],goal_conditions=[],destruction_actions=[]);unit_counts=Counter();created=Counter();thg=Counter()
 for row in manifest['campaign_found']:
  raw=(data/row['path'].replace('\\','/')).read_bytes();assert hashlib.sha256(raw).hexdigest()==row['sha256'];parts=defaultdict(list)
  for tag,v in sections(raw):parts[tag].append(v)
  result['maps'].append(dict(path=row['path'],sha256=row['sha256'],type_tags=[v.hex() for v in parts[b'TYPE']]))
  for v in parts[b'UNIT']:
   for j in range(0,len(v),36):unit_counts[struct.unpack_from('<H',v,j+8)[0]]+=1
  for v in parts[b'THG2']:
   for j in range(0,len(v),10):
    if not struct.unpack_from('<H',v,j+8)[0]&0x1000:thg[struct.unpack_from('<H',v,j)[0]]+=1
  for si,v in enumerate(parts[b'TRIG']):
   assert len(v)%2400==0
   for ti in range(len(v)//2400):
    t=v[ti*2400:(ti+1)*2400];base=dict(map=row['path'],map_sha256=row['sha256'],section_index=si,trigger_index=ti,execution_flags=struct.unpack_from('<I',t,2368)[0],owners=list(t[2372:2400]),stored_condition_types=[t[i*20+15] for i in range(16) if t[i*20+15]],stored_action_types=[t[320+i*32+26] for i in range(64) if t[320+i*32+26]]);ended=False
    for ci in range(16):
     c=t[ci*20:(ci+1)*20];fields=struct.unpack('<IIIH6B',c);loc,player,amount,typ,comparison,kind,resource,flags,pad0,pad1=fields
     if kind in (2,3,6,7,16,17) and typ in POWERUPS:result['goal_conditions'].append(dict(base,index=ci,type=typ,condition=kind,location=loc,player=player,amount=amount,comparison=comparison,flags=flags,after_enabled_none=ended,record_sha256=hashlib.sha256(c).hexdigest()))
     if kind==0 and not flags&2:ended=True
    ended=False
    for ai in range(64):
     c=t[320+ai*32:352+ai*32];typ=struct.unpack_from('<H',c,24)[0];kind=c[26]
     if kind in (11,44):created[typ]+=1
     if kind in (22,23,24,25) and typ in POWERUPS:result['destruction_actions'].append(dict(base,index=ai,type=typ,action=kind,location=struct.unpack_from('<I',c)[0],player=struct.unpack_from('<I',c,16)[0],quantity=c[27],flags=c[28],after_enabled_none=ended,record_sha256=hashlib.sha256(c).hexdigest()))
     if kind==0 and not c[28]&2:ended=True
 result.update(physical_maps=len(result['maps']),condition_types=dict(Counter(str(x['type']) for x in result['goal_conditions'])),destruction_action_types=dict(Counter(str(x['type']) for x in result['destruction_actions'])),flag215_producers=dict(unit_records=unit_counts[215],create_actions=created[215],thg2_unitlike_records=thg[215]),flag_beacon197_199_producers={str(t):dict(unit_records=unit_counts[t],create_actions=created[t],thg2_unitlike_records=thg[t]) for t in (197,198,199)},scope='Raw authenticated references, not whole trigger activation or campaign progression')
 return result

def main():
 p=argparse.ArgumentParser(description=__doc__)
 for name in ('exe','data','manifest','out'):p.add_argument('--'+name,type=Path,required=True)
 a=p.parse_args();raw=a.exe.read_bytes();assert hashlib.sha256(raw).hexdigest()==HASH;pe=pefile.PE(data=raw);md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);witnesses={}
 for name,(lo,hi) in BLOCKS.items():
  code=pe.get_data(lo-0x400000,hi-lo);ins=list(md.disasm(code,lo));assert sum(x.size for x in ins)==len(code)
  witnesses[name]=dict(preferred_va=hex(lo),rva=hex(lo-0x400000),end_exclusive=hex(hi),instructions=[dict(va=hex(i.address),rva=hex(i.address-0x400000),file_offset=hex(pe.get_offset_from_rva(i.address-0x400000)),bytes=i.bytes.hex(),mnemonic=i.mnemonic,operands=i.op_str) for i in ins])
 result=dict(target_sha256=HASH,target_version='1.17.0.1',extractor_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),retail_manifest_sha256=hashlib.sha256(a.manifest.read_bytes()).hexdigest(),original_code_executed=False,witnesses=witnesses,campaign_inputs=campaign_inputs(a.data,json.loads(a.manifest.read_text())))
 a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(dict(blocks=len(witnesses),instructions=sum(len(v['instructions']) for v in witnesses.values()),goal_conditions=len(result['campaign_inputs']['goal_conditions']),destruction_actions=len(result['campaign_inputs']['destruction_actions']),flag_producers=result['campaign_inputs']['flag215_producers'])))
if __name__=='__main__':main()
