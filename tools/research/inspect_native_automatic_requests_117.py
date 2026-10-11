#!/usr/bin/env python3
"""Extract selected original automatic Town request producers and helpers."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import capstone
import pefile
import inspect_native_ai_construction_117 as connected
TARGET_HASH=connected.TARGET_HASH
BLOCKS=dict(connected.BLOCKS,
 automatic_supply_timing_flag=(0x483330,0x483344),
 automatic_unit_request_cost_and_heap=(0x482ae0,0x482b60),
 automatic_supply_actual_and_queued_count=(0x487ba0,0x487d0a))
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--exe',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
 raw=a.exe.read_bytes();assert hashlib.sha256(raw).hexdigest()==TARGET_HASH
 pe=pefile.PE(data=raw);assert pe.OPTIONAL_HEADER.ImageBase==0x400000 and pe.FILE_HEADER.Machine==0x14c
 md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);witnesses={}
 for name,(lo,hi) in BLOCKS.items():
  code=pe.get_data(lo-0x400000,hi-lo);ins=list(md.disasm(code,lo));length=sum(x.size for x in ins);assert length==len(code),(name,length,len(code))
  witnesses[name]=dict(preferred_va=hex(lo),rva=hex(lo-0x400000),end_exclusive=hex(hi),requested_bytes=len(code),decoded_bytes=length,undecoded_tail='',instructions=[dict(va=hex(x.address),rva=hex(x.address-0x400000),file_offset=hex(pe.get_offset_from_rva(x.address-0x400000)),bytes=x.bytes.hex(),mnemonic=x.mnemonic,operands=x.op_str) for x in ins])
 result=dict(target_sha256=TARGET_HASH,original_code_executed=False,extractor_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),source_dependencies_sha256={Path(connected.__file__).name:hashlib.sha256(Path(connected.__file__).read_bytes()).hexdigest()},witnesses=witnesses,count_morph_dispatch=dict(first_type=131,last_type=143,destinations=[hex(x) for x in struct.unpack('<5I',pe.get_data(0x87d0c,20))],indices=list(pe.get_data(0x87d20,13))))
 a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(dict(blocks=len(witnesses),instructions=sum(len(x['instructions']) for x in witnesses.values()))))
if __name__=='__main__':main()
