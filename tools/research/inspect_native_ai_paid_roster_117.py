#!/usr/bin/env python3
"""Read the exact-image Firebat recipe and new paid-roster caller witnesses."""
import argparse
import hashlib
import json
from pathlib import Path
import capstone
import pefile
import inspect_native_requirements_117 as requirements
TARGET_HASH=requirements.TARGET_HASH
BLOCKS={}
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--exe',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
 raw=a.exe.read_bytes();assert hashlib.sha256(raw).hexdigest()==TARGET_HASH;pe=pefile.PE(data=raw);assert pe.OPTIONAL_HEADER.ImageBase==0x400000 and pe.FILE_HEADER.Machine==0x14c
 entries,end=requirements.parse_unit_table(pe);recipe=next(x for x in entries if x['unit_id']==32)
 md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);witnesses={}
 for name,(lo,hi)in BLOCKS.items():
  code=pe.get_data(lo-0x400000,hi-lo);ins=list(md.disasm(code,lo));assert sum(i.size for i in ins)==len(code)
  witnesses[name]=dict(start_preferred_va=hex(lo),end_exclusive_preferred_va=hex(hi),bytes_sha256=hashlib.sha256(code).hexdigest(),instructions=[dict(address=hex(i.address),bytes=i.bytes.hex(),mnemonic=i.mnemonic,operands=i.op_str)for i in ins])
 result=dict(target_sha256=TARGET_HASH,target_version='1.17.0.1',inspector_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),dependency_sha256={Path(requirements.__file__).name:hashlib.sha256(Path(requirements.__file__).read_bytes()).hexdigest()},original_code_executed=False,original_Firebat_recipe=recipe,witnesses=witnesses)
 a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(dict(witnesses=len(witnesses),instructions=sum(len(w['instructions'])for w in witnesses.values()),original_recipe=recipe)))
if __name__=='__main__':main()
