#!/usr/bin/env python3
"""Read exact-hash Computer AI nuclear-script and return-to-base witnesses as data."""
import argparse,hashlib,json,struct
from pathlib import Path
import pefile,capstone
TARGET_HASH='94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0'
BLOCKS={'computer_target_acquisition':(0x41f6c0,0x41f9f4),'regional_air_target_for_AI_unit':(0x4a9760,0x4a97d2),'regional_ground_target_for_AI_unit':(0x4a9860,0x4a98d2),'AI_unit_local_acquisition_gate':(0x4ae040,0x4ae071),'choose_preferred_unit_target':(0x4ae080,0x4ae212),'campaign_rectangle_nuke_selector':(0x432f60,0x432f9e),'campaign_nuke_candidate_callback':(0x433620,0x4336a1),'script_create_unit_at_point':(0x4261F0,0x426527),'assign_new_unit_AI':(0x4B1FE0,0x4B2009),'computer_approach_cloak':(0x4B0210,0x4B0244),'unit_cloak_technology':(0x434900,0x434935),'request_personal_cloak':(0x434270,0x4342F7),'shared_military_pool_initialize':(0x4A9A90,0x4A9B12),'return_to_nearest_owned_base':(0x4AFD40,0x4AFEB8),'idle_owned_unit_selector':(0x4932A0,0x4932ED),'owned_building_worker_in_region':(0x4861A0,0x4861F7),'native_region_count':(0x481B00,0x481B09),'script_nuke_location':(0x494565,0x494610),'script_create_nuke':(0x49489D,0x4948D8),'script_nuke_position':(0x49493E,0x494990)}
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--exe',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
 raw=a.exe.read_bytes();assert hashlib.sha256(raw).hexdigest()==TARGET_HASH;pe=pefile.PE(data=raw)
 assert pe.OPTIONAL_HEADER.ImageBase==0x400000 and pe.FILE_HEADER.Machine==0x14c
 md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);witnesses={}
 for name,(lo,hi)in BLOCKS.items():
  code=pe.get_data(lo-0x400000,hi-lo);ins=list(md.disasm(code,lo));assert sum(i.size for i in ins)==len(code)
  witnesses[name]={'start_preferred_va':hex(lo),'end_exclusive_preferred_va':hex(hi),'bytes_sha256':hashlib.sha256(code).hexdigest(),'instructions':[{'address':hex(i.address),'bytes':i.bytes.hex(),'mnemonic':i.mnemonic,'operands':i.op_str}for i in ins]}
 dispatch=[]
 for order in range(123,130):
  stages=[]
  for stage,index_base,bias,dest_base in((1,0x4399D4,0,0x4399C0),(2,0x439B18,4,0x439A84),(3,0x439D5C,1,0x439BCC)):
   index=pe.get_data(index_base-0x400000+order-bias,1)[0];wrapper=struct.unpack('<I',pe.get_data(dest_base-0x400000+index*4,4))[0]
   stages.append(dict(stage=stage,index_table=hex(index_base),index_bias=bias,case_index=index,destination_table=hex(dest_base),wrapper=hex(wrapper)))
  dispatch.append(dict(order_id=order,stages=stages))
 r=dict(target_sha256=TARGET_HASH,target_version='1.17.0.1',inspector_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),original_code_executed=False,primary_dispatch=dispatch,launch_state_table=dict(preferred_va='0x4366d4',states=[hex(x)for x in struct.unpack('<7I',pe.get_data(0x366D4,28))]),witnesses=witnesses)
 a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_text(json.dumps(r,indent=2)+'\n');print(json.dumps({'witnesses':len(witnesses),'instructions':sum(len(w['instructions'])for w in witnesses.values())}))
if __name__=='__main__':main()
