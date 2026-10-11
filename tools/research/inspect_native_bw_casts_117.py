#!/usr/bin/env python3
"""Read exact-hash remaining natural ability-cast witnesses as data."""
import argparse,hashlib,json,struct
from pathlib import Path
import pefile,capstone
TARGET_HASH='94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0'
BLOCKS={'MindControl_order':(0x430CE0,0x430D8D),'MindControl_transfer':(0x430F60,0x430F9F),'give_register_wrapper':(0x4B6A60,0x4B6A68),'Feedback_order':(0x430D90,0x430EC5),'Feedback_lone_hit':(0x430B90,0x430BCF),'Web_create':(0x4307A0,0x430820),'Maelstrom_area':(0x430820,0x430874),'Maelstrom_candidate':(0x4308D0,0x43091A),'Maelstrom_apply':(0x438A90,0x438AE2),'Maelstrom_expire':(0x438730,0x43877A),'initial_Archon_order_state':(0x42FFD0,0x42FFE5),'MindControl_linked_visitor':(0x4B82C0,0x4B82C9),'hangar_visitor':(0x41CA90,0x41CB12),'Web_border_marker':(0x43A880,0x43A8DA)}
BLOCKS['cancel_all_training_queue']=(0x41B5C0,0x41B6E2)
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--exe',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
 raw=a.exe.read_bytes();assert hashlib.sha256(raw).hexdigest()==TARGET_HASH;pe=pefile.PE(data=raw)
 assert pe.OPTIONAL_HEADER.ImageBase==0x400000 and pe.FILE_HEADER.Machine==0x14c
 md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);witnesses={}
 for name,(lo,hi)in BLOCKS.items():
  code=pe.get_data(lo-0x400000,hi-lo);ins=list(md.disasm(code,lo));assert sum(i.size for i in ins)==len(code)
  witnesses[name]={'start_preferred_va':hex(lo),'end_exclusive_preferred_va':hex(hi),'bytes_sha256':hashlib.sha256(code).hexdigest(),'instructions':[{'address':hex(i.address),'bytes':i.bytes.hex(),'mnemonic':i.mnemonic,'operands':i.op_str}for i in ins]}
 dispatch=[]
 for order,index_want,wrapper_want,handler in((180,59,0x439985,0x434460),(181,59,0x439985,0x434460),(182,96,0x43999D,0x430CE0),(184,97,0x4399A5,0x430D90),(186,59,0x439985,0x434460)):
  index=pe.get_data(0x439D5C-0x400000+order-1,1)[0];wrapper=struct.unpack('<I',pe.get_data(0x439BCC-0x400000+index*4,4))[0];assert(index,wrapper)==(index_want,wrapper_want)
  dispatch.append(dict(order_id=order,stage=3,index_table='0x439d5c',index_bias=1,case_index=index,destination_table='0x439bcc',wrapper=hex(wrapper),handler=hex(handler),preceding_gate='0x439640 decrements byte+0x85; only old zero resets8 and dispatches'))
 explosion={str(i):hex(struct.unpack('<I',pe.get_data(0x409520-0x400000+i*4,4))[0])for i in(16,17,19,20,22)}
 r={'target_sha256':TARGET_HASH,'target_version':'1.17.0.1','inspector_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),'original_code_executed':False,'primary_dispatch':dispatch,'explosion_dispatch_table':{'preferred_va':'0x409520','selected_entries':explosion,'19_and20_are_assertion_paths_not_delivery_handlers':True},'witnesses':witnesses}
 a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_text(json.dumps(r,indent=2)+'\n');print(json.dumps({'witnesses':len(witnesses),'instructions':sum(len(w['instructions'])for w in witnesses.values())}))
if __name__=='__main__':main()
