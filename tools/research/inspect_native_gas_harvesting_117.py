#!/usr/bin/env python3
"""Extract exact-hash gas-enter/hide/exit witnesses."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import capstone
import pefile
TARGET_HASH='94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0'
BLOCKS={
 'completed_owned_refinery_predicate':(0x424330,0x424373),
 'hidden_unit_active_update':(0x43a770,0x43a7f2),
 'hidden_unit_main_dispatcher':(0x439e20,0x439edb),
 'placement_postcheck_native_false_leaf':(0x4928f0,0x4928f5),
 'append_simple_primary_order':(0x429eb0,0x429ed3),
 'harvest_gas_order':(0x4235c0,0x42379c),
 'wait_for_gas_order':(0x4237a0,0x423805),
 'gas_exit_placement_and_angle':(0x424590,0x42474c),
 'insert_nontarget_before_order':(0x429de0,0x429e03),
 'resource_wait_list_unlink':(0x424750,0x4247ae),
}

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--exe',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
 raw=a.exe.read_bytes();assert hashlib.sha256(raw).hexdigest()==TARGET_HASH
 pe=pefile.PE(data=raw);assert pe.OPTIONAL_HEADER.ImageBase==0x400000 and pe.FILE_HEADER.Machine==0x14C
 md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);witnesses={}
 for name,(lo,hi) in BLOCKS.items():
  code=pe.get_data(lo-0x400000,hi-lo);ins=list(md.disasm(code,lo));length=sum(x.size for x in ins)
  assert length==len(code),(name,length,len(code))
  witnesses[name]=dict(preferred_va=hex(lo),rva=hex(lo-0x400000),end_exclusive=hex(hi),requested_bytes=len(code),decoded_bytes=length,undecoded_tail=code[length:].hex(),instructions=[dict(va=hex(x.address),rva=hex(x.address-0x400000),file_offset=hex(pe.get_offset_from_rva(x.address-0x400000)),bytes=x.bytes.hex(),mnemonic=x.mnemonic,operands=x.op_str) for x in ins])
 result=dict(target_sha256=TARGET_HASH,target_version='1.17.0.1',original_code_executed=False,extractor_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),witnesses=witnesses,hidden_primary_priority_switch=dict(destinations=[hex(x) for x in struct.unpack('<11I',pe.get_data(0x39edc,44))],mapping=list(pe.get_data(0x39f08,177)),order83_destination=hex(struct.unpack('<11I',pe.get_data(0x39edc,44))[pe.get_data(0x39f08,177)[83]])),hidden_primary_throttled_switch=dict(first_order=5,destinations=[hex(x) for x in struct.unpack('<5I',pe.get_data(0x39fbc,20))],mapping=list(pe.get_data(0x39fd0,157))),iscript_dispatch=dict(preferred_va='0x4111e4',destinations=[hex(x) for x in struct.unpack('<69I',pe.get_data(0x111E4,276))]),native_dat_descriptors={key:[dict(index=i,target_va=hex(struct.unpack('<3I',pe.get_data(base-0x400000+i*12,12))[0]),entry_width=struct.unpack('<3I',pe.get_data(base-0x400000+i*12,12))[1],count=struct.unpack('<3I',pe.get_data(base-0x400000+i*12,12))[2]) for i in range(count)] for key,base,count in (('units',0x5014F0,54),('flingy',0x501148,7),('sprites',0x501408,6),('images',0x5011A8,14),('weapons',0x501828,24),('orders',0x501278,19))})
 a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_text(json.dumps(result,indent=2)+'\n')
 print(json.dumps(dict(blocks=len(witnesses),instructions=sum(len(x['instructions']) for x in witnesses.values()),tails={k:v['undecoded_tail'] for k,v in witnesses.items() if v['undecoded_tail']})))
if __name__=='__main__':main()
