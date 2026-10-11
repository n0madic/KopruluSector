#!/usr/bin/env python3
"""Read exact-hash Nuclear Strike order witnesses as data."""
import argparse,hashlib,json,struct
from pathlib import Path
import pefile,capstone
TARGET_HASH='94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0'
BLOCKS={'Nuke_train_observer':(0x4356C0,0x43572D),'Nuke_launch':(0x436540,0x4366D3),'Nuke_paint':(0x4366F0,0x436715),'Nuke_track':(0x436720,0x436811),'Nuke_ground':(0x41EA40,0x41EB9E),'Nuke_unit':(0x41EBA0,0x41EBD7),'ready_silo_selector':(0x436F60,0x436FB4),'sound_disabled_guard':(0x446940,0x44698A),'stat_txt_lookup':(0x4A7D70,0x4A7D7D),'return_to_base_null_AI_guard':(0x4AFD40,0x4AFD59),'nuclear_disconnect':(0x435A40,0x435A66),'selected_nuclear_cancel':(0x4724F0,0x47252E),'selection_reset':(0x49CB10,0x49CB1C),'selection_next':(0x49CB20,0x49CB8E),'movement_edge_direction_clamp':(0x4814E0,0x4815CA),'throttled_point_movement_request':(0x438B80,0x438B9B),'command_dispatch_prefix_static':(0x475D30,0x475D9E),'cancel_command_case_static':(0x476266,0x47627C),'command_dispatch_tail_static':(0x476347,0x476361),'script_create_ready_nuke':(0x493450,0x4934BD),'new_child_AI_assignment':(0x48D320,0x48D37A),'guard_home_or_unbound':(0x4B2720,0x4B2750),'reaction_retreat_point':(0x421180,0x4212A8)}
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
 command_case=pe.get_data(0x764A8+0x2E-5,1)[0];command_destination=struct.unpack('<I',pe.get_data(0x763C0+command_case*4,4))[0]
 assert command_case==38 and command_destination==0x476266
 r=dict(command_cancel_nuke=dict(command_byte='0x2e',index_bias=5,index_table='0x4764a8',index=command_case,destination_table='0x4763c0',destination=hex(command_destination),native_handler='0x4724f0',decoder_execution_not_claimed=True),target_sha256=TARGET_HASH,target_version='1.17.0.1',inspector_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),original_code_executed=False,primary_dispatch=dispatch,launch_state_table=dict(preferred_va='0x4366d4',states=[hex(x)for x in struct.unpack('<7I',pe.get_data(0x366D4,28))]),witnesses=witnesses)
 a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_text(json.dumps(r,indent=2)+'\n');print(json.dumps({'witnesses':len(witnesses),'instructions':sum(len(w['instructions'])for w in witnesses.values())}))
if __name__=='__main__':main()
