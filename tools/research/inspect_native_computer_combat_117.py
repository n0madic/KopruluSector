#!/usr/bin/env python3
"""Extract exact-hash computer combat-order and guard/Stim witnesses."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import capstone
import pefile
TARGET_HASH='94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0'
BLOCKS={
 'ground_splash_callback':(0x409590,0x4096b9),
 'bullet_point_to_target_bounds_within_radius':(0x40a9f0,0x40aa89),
 'sap_location':(0x41eeb0,0x41ef6f),
 'sap_unit':(0x41ef70,0x41f082),
 'set_move_target_and_waypoint':(0x43a070,0x43a09e),
 'hold_initialize_stop':(0x43a4e0,0x43a535),
 'hold_position':(0x41e9b0,0x41ea3f),
 'ai_transport_destination':(0x437540,0x4376c7),
 'region_marker':(0x481bb0,0x481bd1),
 'military_state_for_transport':(0x4b0580,0x4b05b3),
 'ai_region_destination':(0x4ab450,0x4ab482),
 'computer_attack_support_stim':(0x4b1100,0x4b11ab),
 'guard_return_home':(0x4b2110,0x4b21f0),
 'guard_home_set':(0x4b2270,0x4b2293),
 'stim_apply':(0x438af0,0x438b44),
 'movement_speed_status':(0x428af0,0x428b18),
 'hold_attack_attempt':(0x420bd0,0x420da7),
 'hold_target_acquisition':(0x432060,0x432070),
 'can_attack_now':(0x422d10,0x422d8a),
 'hold_acquisition':(0x431b70,0x431c18),
 'melee_approach':(0x41d8f0,0x41d9b4),
 'unlock_primary_then_done':(0x429d00,0x429d10),
 'prepend_target_order':(0x429da0,0x429dd8),
 'prepend_order_packed_target':(0x429c40,0x429c7b),
 'enable_target_switching':(0x41cf90,0x41cfac),
}



def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--exe',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
 raw=a.exe.read_bytes();assert hashlib.sha256(raw).hexdigest()==TARGET_HASH
 pe=pefile.PE(data=raw);assert pe.OPTIONAL_HEADER.ImageBase==0x400000 and pe.FILE_HEADER.Machine==0x14C
 md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);witnesses={}
 for name,(lo,hi) in BLOCKS.items():
  code=pe.get_data(lo-0x400000,hi-lo);ins=list(md.disasm(code,lo));length=sum(x.size for x in ins)
  witnesses[name]=dict(preferred_va=hex(lo),rva=hex(lo-0x400000),end_exclusive=hex(hi),requested_bytes=len(code),decoded_bytes=length,undecoded_tail=code[length:].hex(),instructions=[dict(va=hex(x.address),rva=hex(x.address-0x400000),file_offset=hex(pe.get_offset_from_rva(x.address-0x400000)),bytes=x.bytes.hex(),mnemonic=x.mnemonic,operands=x.op_str) for x in ins])
 result=dict(target_sha256=TARGET_HASH,target_version='1.17.0.1',original_code_executed=False,extractor_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),witnesses=witnesses,iscript_dispatch=dict(preferred_va='0x4111e4',destinations=[hex(x) for x in struct.unpack('<69I',pe.get_data(0x111E4,276))]),native_dat_descriptors={key:[dict(index=i,target_va=hex(struct.unpack('<3I',pe.get_data(base-0x400000+i*12,12))[0]),entry_width=struct.unpack('<3I',pe.get_data(base-0x400000+i*12,12))[1],count=struct.unpack('<3I',pe.get_data(base-0x400000+i*12,12))[2]) for i in range(count)] for key,base,count in (('units',0x5014F0,54),('flingy',0x501148,7),('sprites',0x501408,6),('images',0x5011A8,14),('weapons',0x501828,24),('orders',0x501278,19))})
 a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_text(json.dumps(result,indent=2)+'\n')
 print(json.dumps(dict(blocks=len(witnesses),instructions=sum(len(x['instructions']) for x in witnesses.values()),tails={k:v['undecoded_tail'] for k,v in witnesses.items() if v['undecoded_tail']})))
if __name__=='__main__':main()
