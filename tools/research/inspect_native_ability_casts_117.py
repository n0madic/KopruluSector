#!/usr/bin/env python3
"""Read exact-hash remaining natural ability-cast witnesses as data."""
import argparse,hashlib,json,struct
from pathlib import Path
import pefile,capstone
TARGET_HASH='94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0'
BLOCKS={'Lockdown_apply':(0x4389C0,0x438A12),'Consume_effect':(0x43DB70,0x43DBEE),'OpticalFlare_effect':(0x438A20,0x438A8A),'Broodlings_effect':(0x43E120,0x43E273),'Lockdown_expire':(0x4386E0,0x43872A),'Medic_idle':(0x4361B0,0x436242),'Medic_heal_target':(0x431CE0,0x431E82),'Restoration_effect':(0x4383E0,0x4384BB),'generic_spell_primary_wrapper':(0x439985,0x43998D),'slow_primary_dispatch':(0x439640,0x43968A)}
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--exe',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
 raw=a.exe.read_bytes();assert hashlib.sha256(raw).hexdigest()==TARGET_HASH;pe=pefile.PE(data=raw)
 assert pe.OPTIONAL_HEADER.ImageBase==0x400000 and pe.FILE_HEADER.Machine==0x14c
 md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);witnesses={}
 for name,(lo,hi)in BLOCKS.items():
  code=pe.get_data(lo-0x400000,hi-lo);ins=list(md.disasm(code,lo));assert sum(i.size for i in ins)==len(code)
  witnesses[name]={'start_preferred_va':hex(lo),'end_exclusive_preferred_va':hex(hi),'bytes_sha256':hashlib.sha256(code).hexdigest(),'instructions':[{'address':hex(i.address),'bytes':i.bytes.hex(),'mnemonic':i.mnemonic,'operands':i.op_str}for i in ins]}
 dispatch=[]
 for order in(113,115,120,121,145,185):
  index=pe.get_data(0x439D5C-0x400000+order-1,1)[0];wrapper=struct.unpack('<I',pe.get_data(0x439BCC-0x400000+index*4,4))[0]
  assert index==59 and wrapper==0x439985
  dispatch.append({'order_id':order,'stage':3,'index_table':'0x439d5c','index_bias':1,'case_index':index,'destination_table':'0x439bcc','wrapper':hex(wrapper),'handler':'0x434460','preceding_gate':'0x439640 decrements byte+0x85; only old zero resets8 and dispatches'})
 explosion={str(i):hex(struct.unpack('<I',pe.get_data(0x409520-0x400000+i*4,4))[0]) for i in range(25)}
 r={'target_sha256':TARGET_HASH,'target_version':'1.17.0.1','inspector_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),'original_code_executed':False,'primary_dispatch':dispatch,'explosion_dispatch_table':{'preferred_va':'0x409520','entries':explosion},'witnesses':witnesses}
 a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_text(json.dumps(r,indent=2)+'\n');print(json.dumps({'witnesses':len(witnesses),'instructions':sum(len(w['instructions'])for w in witnesses.values())}))
if __name__=='__main__':main()
