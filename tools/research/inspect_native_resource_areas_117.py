#!/usr/bin/env python3
"""Extract exact-image resource-area refresh, lookup and Town binding witnesses."""
import argparse
import hashlib
import json
from pathlib import Path
import capstone
import pefile
TARGET_HASH='94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0'
BLOCKS={
 'resource_area_update':(0x496c80,0x496e0e),
 'all_active_unit_head':(0x425600,0x425606),
 'resource_area_nearest_point':(0x496bf0,0x496c76),
 'resource_area_first_mineral':(0x4964b0,0x4964f2),
 'resource_area_gas_three_sort':(0x496510,0x496686),
 'resource_area_worker_limit':(0x496690,0x4966ee),
 'resource_area_mineral_worker_limit':(0x4966f0,0x49673e),
 'resource_area_free_first':(0x496500,0x49650b),
 'resource_area_free_next':(0x496740,0x496776),
 'player_town_resource_area_count':(0x486130,0x48615c),
 'resource_area_mineral_count':(0x496f20,0x496f3d),
 'resource_area_gas_count':(0x496f40,0x496f4e),
 'resource_death_cached_air_strength':(0x4a97e0,0x4a97e8),
 'resource_death_cached_ground_strength':(0x4a97f0,0x4a97f8),
 'town_bind_resources_and_assign_workers':(0x485020,0x4852ac),
 'town_gas_category_nth_selector':(0x485de0,0x485e74),
 'town_racial_gas_category':(0x485e80,0x485eeb),
 'player_mineral_gas_desire_percentages':(0x483030,0x4830cf),
}
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--exe',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
 raw=a.exe.read_bytes();assert hashlib.sha256(raw).hexdigest()==TARGET_HASH
 pe=pefile.PE(data=raw);assert pe.OPTIONAL_HEADER.ImageBase==0x400000 and pe.FILE_HEADER.Machine==0x14c
 md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);witnesses={}
 for name,(lo,hi) in BLOCKS.items():
  code=pe.get_data(lo-0x400000,hi-lo);ins=list(md.disasm(code,lo));n=sum(x.size for x in ins);assert n==len(code),(name,n,len(code))
  witnesses[name]=dict(preferred_va=hex(lo),rva=hex(lo-0x400000),end_exclusive=hex(hi),requested_bytes=len(code),decoded_bytes=n,undecoded_tail='',instructions=[dict(va=hex(x.address),rva=hex(x.address-0x400000),file_offset=hex(pe.get_offset_from_rva(x.address-0x400000)),bytes=x.bytes.hex(),mnemonic=x.mnemonic,operands=x.op_str) for x in ins])
 result=dict(target_sha256=TARGET_HASH,original_code_executed=False,extractor_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),witnesses=witnesses,resource_area_array=dict(base='0x6a2e88',stride=48,record_count=250,used_count='0x6a5d68',refresh_countdown='0x6a5d6c'))
 a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(dict(blocks=len(witnesses),instructions=sum(len(x['instructions']) for x in witnesses.values()))))
if __name__=='__main__':main()
