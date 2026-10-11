#!/usr/bin/env python3
"""Extract exact-image AIScript Town startup and connected construction witnesses.

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
 'aiscript_town_player_initialize':(0x482490,0x4824aa),
 'aiscript_post_town_regions':(0x494f80,0x494fb2),
 'regional_state_dispatch':(0x4b0ce0,0x4b0d3c),
 'player_spending_budget_refresh':(0x4823d0,0x48244a),
 'reserve_pending_worker_queue_costs':(0x487730,0x4877c8),
 'town_automatic_supply_request':(0x485970,0x485aab),
 'town_automatic_gas_request':(0x485ab0,0x485b70),
 'town_automatic_bunker_and_infantry_requests':(0x4ae220,0x4ae375),
 'town_automatic_turret_request':(0x4b1420,0x4b146f),
 'supply_refresh_airborne_depot_predicate':(0x484fc0,0x485014),
 'worker_build_queue_reset':(0x41b5c0,0x41b6e2),
 'computer_ai_special_type_service':(0x48e180,0x48e4a9),
 'worker_fallback_neutral_mineral_search':(0x431fe0,0x432058),
 'builder_failure_request_service':(0x485dc0,0x485de0),
 'builder_failure_error_get':(0x4b38a0,0x4b38a6),
 'builder_failure_heap_pop_and_add_budget':(0x482c70,0x482c82),
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
 result=dict(target_sha256=TARGET_HASH,target_version='1.17.0.1',original_code_executed=False,extractor_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),witnesses=witnesses,computer_type_dispatch=dict(first_type=2,last_type=172,destinations=[hex(x) for x in struct.unpack('<10I',pe.get_data(0x8e4ac,40))],indices=list(pe.get_data(0x8e4d4,171))),regional_state_dispatch=dict(destinations=[hex(x) for x in struct.unpack('<10I',pe.get_data(0xb0d3c,40))]),native_dat_descriptors={key:[dict(index=i,target_va=hex(struct.unpack('<3I',pe.get_data(base-0x400000+i*12,12))[0]),entry_width=struct.unpack('<3I',pe.get_data(base-0x400000+i*12,12))[1],count=struct.unpack('<3I',pe.get_data(base-0x400000+i*12,12))[2]) for i in range(count)] for key,base,count in (('units',0x5014f0,54),('flingy',0x501148,7),('sprites',0x501408,6),('images',0x5011a8,14),('weapons',0x501828,24),('orders',0x501278,19))})
 a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_text(json.dumps(result,indent=2)+'\n')
 print(json.dumps(dict(blocks=len(witnesses),instructions=sum(len(x['instructions']) for x in witnesses.values()),tails={k:v['undecoded_tail'] for k,v in witnesses.items() if v['undecoded_tail']})))
if __name__=='__main__':main()
