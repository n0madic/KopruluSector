#!/usr/bin/env python3
"""Extract exact-hash physical blocked-movement witnesses."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import capstone
import pefile
TARGET_HASH='94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0'
BLOCKS={
 'follow_path_collision_responses':(0x42b8d0,0x42bafe),
 'dodge_generation_encode':(0x428bd0,0x428c0b),
 'dodge_generation_resolve':(0x428b50,0x428b9c),
 'movement_resolve_unit_collision_28':(0x42b3b0,0x42b6cc),
 'blocker_contains_mover_goal':(0x42e360,0x42e3f7),
 'blocker_future_contains_mover_goal':(0x42e400,0x42e48d),
 'movement_blocked_29':(0x42c670,0x42c785),
 'movement_prepare_dodge_stop':(0x42c810,0x42c83e),
 'movement_stop_at_current':(0x42c7a0,0x42c7d8),
 'movement_prepare_slide_31':(0x42c390,0x42c3f5),
 'movement_slide_32':(0x42c1e0,0x42c38c),
 'flingy_prepare_direction_speed_override':(0x40bc30,0x40bcc7),
 'flingy_velocity_from_heading_speed':(0x40cb60,0x40cb89),
 'movement_forced_blocker_repath_23':(0x42bf10,0x42bf5f),
 'movement_all_blockers_repath_24':(0x42bf60,0x42bfc1),
 'movement_turn_dodge_path_20':(0x42b2b0,0x42b36f),
 'movement_dodge_path_rebuild_19':(0x42c570,0x42c60e),
 'dodge_side_direction':(0x42dc70,0x42deee),
 'blocker_relative_side':(0x42cc70,0x42cdd7),
 'blocker_moving_away':(0x42cde0,0x42ce54),
 'movement_retry_33':(0x42bb00,0x42bb79),
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
