#!/usr/bin/env python3
"""Extract exact-image Town pools, creation and owner/area admission witnesses.

Analytical labels are not original symbols. Reading PE data executes no game.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import capstone
import pefile
TARGET_HASH='94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0'
BLOCKS={
 'building_ai_pool_initialize':(0x483bd0,0x483c95),
 'town_pool_initialize':(0x483ca0,0x483d94),
 'worker_ai_pool_initialize':(0x483da0,0x483e32),
 'all_town_worker_building_pools_initialize':(0x486a20,0x486ae9),
 'town_allocate':(0x485770,0x485836),
 'area_owner_filter':(0x485760,0x48576c),
 'start_town':(0x487030,0x48710a),
 'start_area_town':(0x486e80,0x486f90),
 'owner_unit_head':(0x4256f0,0x4256f8),
 'unit_incomplete_train_or_resetcollision':(0x4b14d0,0x4b14f2),
 'campaign_flag_get':(0x4831e0,0x4831f1),
 'building_region_assignment':(0x4b00a0,0x4b0206),
 'town_index_decode':(0x4877d0,0x4877e4),
 'campaign_default_minimum':(0x4837d0,0x4837de),
 'racial_main_building_type':(0x485ef0,0x485f15),
 'native_memset':(0x4d0c60,0x4d0cf0),
 'area_filtered_spatial_query':(0x422460,0x4224c0),
}

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--exe',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
 raw=a.exe.read_bytes();assert hashlib.sha256(raw).hexdigest()==TARGET_HASH
 pe=pefile.PE(data=raw);assert pe.OPTIONAL_HEADER.ImageBase==0x400000 and pe.FILE_HEADER.Machine==0x14c
 md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);witnesses={}
 for name,(lo,hi) in BLOCKS.items():
  code=pe.get_data(lo-0x400000,hi-lo);ins=list(md.disasm(code,lo));length=sum(x.size for x in ins)
  assert length==len(code),(name,length,len(code))
  witnesses[name]=dict(preferred_va=hex(lo),rva=hex(lo-0x400000),end_exclusive=hex(hi),requested_bytes=len(code),decoded_bytes=length,undecoded_tail=code[length:].hex(),instructions=[dict(va=hex(x.address),rva=hex(x.address-0x400000),file_offset=hex(pe.get_offset_from_rva(x.address-0x400000)),bytes=x.bytes.hex(),mnemonic=x.mnemonic,operands=x.op_str) for x in ins])
 result=dict(target_sha256=TARGET_HASH,target_version='1.17.0.1',original_code_executed=False,extractor_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),witnesses=witnesses,area_center_switch=dict(first_type=7,last_type=154,destinations=[hex(x) for x in struct.unpack('<3I',pe.get_data(0x86f90,12))],indices=list(pe.get_data(0x86f9c,148))),native_dat_descriptors={key:[dict(index=i,target_va=hex(struct.unpack('<3I',pe.get_data(base-0x400000+i*12,12))[0]),entry_width=struct.unpack('<3I',pe.get_data(base-0x400000+i*12,12))[1],count=struct.unpack('<3I',pe.get_data(base-0x400000+i*12,12))[2]) for i in range(count)] for key,base,count in (('units',0x5014f0,54),('flingy',0x501148,7),('sprites',0x501408,6),('images',0x5011a8,14),('weapons',0x501828,24),('orders',0x501278,19))})
 a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_text(json.dumps(result,indent=2)+'\n')
 print(json.dumps(dict(blocks=len(witnesses),instructions=sum(len(x['instructions']) for x in witnesses.values()),tails={k:v['undecoded_tail'] for k,v in witnesses.items() if v['undecoded_tail']})))
if __name__=='__main__':main()
