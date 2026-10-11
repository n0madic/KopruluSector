#!/usr/bin/env python3
"""Read exact-hash populated spell-field and relocation witnesses as data."""
import argparse,hashlib,json
from pathlib import Path
import pefile,capstone
TARGET_HASH='94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0'
BLOCKS={
 'dark_swarm_creator':(0x43DC10,0x43DC90),
 'recall_relocate':(0x4304D0,0x43060D),
 'recall_target_callback':(0x4302C0,0x430317),
}
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--exe',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
 raw=a.exe.read_bytes();assert hashlib.sha256(raw).hexdigest()==TARGET_HASH;pe=pefile.PE(data=raw)
 assert pe.OPTIONAL_HEADER.ImageBase==0x400000 and pe.FILE_HEADER.Machine==0x14c
 md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);witnesses={}
 for name,(lo,hi)in BLOCKS.items():
  code=pe.get_data(lo-0x400000,hi-lo);ins=list(md.disasm(code,lo));assert sum(i.size for i in ins)==len(code)
  witnesses[name]={'start_preferred_va':hex(lo),'end_exclusive_preferred_va':hex(hi),'bytes_sha256':hashlib.sha256(code).hexdigest(),'instructions':[{'address':hex(i.address),'bytes':i.bytes.hex(),'mnemonic':i.mnemonic,'operands':i.op_str}for i in ins]}
 r={'target_sha256':TARGET_HASH,'target_version':'1.17.0.1','inspector_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),'original_code_executed':False,'witnesses':witnesses}
 a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_text(json.dumps(r,indent=2)+'\n');print(json.dumps({'witnesses':len(witnesses),'instructions':sum(len(w['instructions'])for w in witnesses.values())}))
if __name__=='__main__':main()
