#!/usr/bin/env python3
"""Extract exact-image resource-area generation and resource clustering witnesses."""
import argparse
import hashlib
import json
from pathlib import Path
import capstone
import pefile
TARGET_HASH='94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0'
BLOCKS={
 'resource_area_generation':(0x497a20,0x497d8f),
 'resource_area_existing_start_match':(0x4955b0,0x4956c0),
 'resource_area_unassigned_clusters':(0x495890,0x495ba3),
 'resource_area_cluster_candidate':(0x495750,0x495883),
 'resource_area_cluster_reference_mineral':(0x495e70,0x4960aa),
 'resource_area_cluster_weight':(0x497480,0x497622),
 'crt_stack_page_probe':(0x4d2200,0x4d222b),
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
