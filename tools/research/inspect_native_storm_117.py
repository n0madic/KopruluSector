#!/usr/bin/env python3
"""Read exact-hash Psi Storm ground-over-time witnesses as data."""
import argparse,hashlib,json,struct
from pathlib import Path
import pefile,capstone
TARGET_HASH='94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0'
BLOCKS={'ground_damage_over_time':(0x409A30,0x409A95)}
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--exe',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
 raw=a.exe.read_bytes();assert hashlib.sha256(raw).hexdigest()==TARGET_HASH;pe=pefile.PE(data=raw)
 assert pe.OPTIONAL_HEADER.ImageBase==0x400000 and pe.FILE_HEADER.Machine==0x14c
 md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);witnesses={}
 for name,(lo,hi)in BLOCKS.items():
  code=pe.get_data(lo-0x400000,hi-lo);ins=list(md.disasm(code,lo));assert sum(i.size for i in ins)==len(code)
  witnesses[name]={'start_preferred_va':hex(lo),'end_exclusive_preferred_va':hex(hi),'bytes_sha256':hashlib.sha256(code).hexdigest(),'instructions':[{'address':hex(i.address),'bytes':i.bytes.hex(),'mnemonic':i.mnemonic,'operands':i.op_str}for i in ins]}
 states=struct.unpack('<7I',pe.get_data(0x40A704-0x400000,28));assert states[4]==0x40A6F4
 wrapper=list(md.disasm(pe.get_data(states[4]-0x400000,5),states[4]));assert len(wrapper)==1 and wrapper[0].mnemonic=='jmp' and wrapper[0].op_str=='0x409a30'
 r={'target_sha256':TARGET_HASH,'target_version':'1.17.0.1','inspector_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),'original_code_executed':False,'Bullet_state4_tail_dispatch':{'table_preferred_va':'0x40a704','wrapper_destinations':[hex(x)for x in states],'state4_wrapper':'0x40a6f4','state4_wrapper_bytes':wrapper[0].bytes.hex(),'state4_handler':'0x409a30'},'witnesses':witnesses}
 a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_text(json.dumps(r,indent=2)+'\n');print(json.dumps({'witnesses':len(witnesses),'instructions':sum(len(w['instructions'])for w in witnesses.values())}))
if __name__=='__main__':main()
