#!/usr/bin/env python3
"""Read exact-hash populated caster readiness and generic spell-order witnesses as data."""
import argparse,hashlib,json,struct
from pathlib import Path
import pefile,capstone
TARGET_HASH='94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0'
BLOCKS={
 'InitArbiter':(0x42F510,0x42F54C),
 'InitArbiter_primary_dispatch_wrapper':(0x4394BF,0x4394CB),
 'generic_spell_invalid_target':(0x434E70,0x434FE0),
 'generic_spell_movement':(0x435020,0x4350A7),
 'generic_spell_range':(0x435530,0x4355B5),
 'spell_iscript_weapon_launch':(0x41F4A0,0x41F4CA),
 'weapon_point_launch':(0x420030,0x4200DF),
}
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--exe',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
 raw=a.exe.read_bytes();assert hashlib.sha256(raw).hexdigest()==TARGET_HASH;pe=pefile.PE(data=raw)
 assert pe.OPTIONAL_HEADER.ImageBase==0x400000 and pe.FILE_HEADER.Machine==0x14c
 md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);witnesses={}
 for name,(lo,hi)in BLOCKS.items():
  code=pe.get_data(lo-0x400000,hi-lo);ins=list(md.disasm(code,lo));assert sum(i.size for i in ins)==len(code)
  witnesses[name]={'start_preferred_va':hex(lo),'end_exclusive_preferred_va':hex(hi),'bytes_sha256':hashlib.sha256(code).hexdigest(),'instructions':[{'address':hex(i.address),'bytes':i.bytes.hex(),'mnemonic':i.mnemonic,'operands':i.op_str}for i in ins]}
 index=pe.get_data(0x439B18-0x400000+130-4,1)[0];target=struct.unpack('<I',pe.get_data(0x439A84-0x400000+index*4,4))[0]
 assert index==25 and target==0x4394BF
 r={'target_sha256':TARGET_HASH,'target_version':'1.17.0.1','inspector_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),'original_code_executed':False,'InitArbiter_dispatch':{'order_id':130,'dispatcher_stage':2,'index_table_preferred_va':'0x439b18','index_bias':4,'case_index':index,'target_table_preferred_va':'0x439a84','wrapper_preferred_va':hex(target),'handler_preferred_va':'0x42f510'},'witnesses':witnesses}
 a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_text(json.dumps(r,indent=2)+'\n');print(json.dumps({'witnesses':len(witnesses),'instructions':sum(len(w['instructions'])for w in witnesses.values())}))
if __name__=='__main__':main()
