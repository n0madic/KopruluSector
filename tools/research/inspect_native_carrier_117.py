#!/usr/bin/env python3
"""Extract exact-hash Carrier/Interceptor hangar, combat and return witnesses."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import capstone
import pefile
TARGET_HASH='94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0'
BLOCKS={
 'carrier_main':(0x41c290,0x41c30c),
 'carrier_attack':(0x41c310,0x41c3bf),
 'carrier_move':(0x41c3c0,0x41c409),
 'carrier_stop':(0x41c410,0x41c445),
 'interceptor_return':(0x41c450,0x41c4dc),
 'interceptor_attack_order':(0x41c4e0,0x41c615),
 'interceptor_target_validate':(0x41cb20,0x41cb9d),
 'hangar_outside_to_inside':(0x41ce40,0x41cec6),
 'fighter_parent_death':(0x41ced0,0x41cf8a),
 'interceptors_attack_parent_target':(0x41d060,0x41d0a1),
 'carrier_reaver_fight_order_choose':(0x41d0d0,0x41d0f8),
 'interceptor_orbit_destination':(0x41d100,0x41d1bd),
 'carrier_reaver_idle_order_choose':(0x41d1c0,0x41d1fc),
 'add_hitpoints':(0x41b900,0x41b90f),
 'interceptor_parent_alert_queries':(0x4af220,0x4af26f),
 'interceptor_parent_alert_candidate':(0x4aaaa0,0x4aab15),
}
STATIC_ONLY={'network_tbl_binding':(0x43f0d0,0x43f0f6)}

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--exe',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
 raw=a.exe.read_bytes();assert hashlib.sha256(raw).hexdigest()==TARGET_HASH
 pe=pefile.PE(data=raw);assert pe.OPTIONAL_HEADER.ImageBase==0x400000 and pe.FILE_HEADER.Machine==0x14C
 md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);witnesses={}
 for name,(lo,hi) in {**BLOCKS,**STATIC_ONLY}.items():
  code=pe.get_data(lo-0x400000,hi-lo);ins=list(md.disasm(code,lo));length=sum(x.size for x in ins)
  witnesses[name]=dict(preferred_va=hex(lo),rva=hex(lo-0x400000),end_exclusive=hex(hi),requested_bytes=len(code),decoded_bytes=length,undecoded_tail=code[length:].hex(),instructions=[dict(va=hex(x.address),rva=hex(x.address-0x400000),file_offset=hex(pe.get_offset_from_rva(x.address-0x400000)),bytes=x.bytes.hex(),mnemonic=x.mnemonic,operands=x.op_str) for x in ins])
 result=dict(target_sha256=TARGET_HASH,target_version='1.17.0.1',original_code_executed=False,extractor_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),witnesses=witnesses,static_only_witnesses=list(STATIC_ONLY),network_tbl_binding=dict(path=pe.get_data(0xec2d0,32).split(b'\0',1)[0].decode('ascii'),pointer_global='0x665de0',loader='0x4c13e0',writer='0x43f0f0',native_loader_executed=False),iscript_dispatch=dict(preferred_va='0x4111e4',destinations=[hex(x) for x in struct.unpack('<69I',pe.get_data(0x111E4,276))]),native_dat_descriptors={key:[dict(index=i,target_va=hex(struct.unpack('<3I',pe.get_data(base-0x400000+i*12,12))[0]),entry_width=struct.unpack('<3I',pe.get_data(base-0x400000+i*12,12))[1],count=struct.unpack('<3I',pe.get_data(base-0x400000+i*12,12))[2]) for i in range(count)] for key,base,count in (('units',0x5014F0,54),('flingy',0x501148,7),('sprites',0x501408,6),('images',0x5011A8,14),('weapons',0x501828,24),('orders',0x501278,19))})
 a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_text(json.dumps(result,indent=2)+'\n')
 print(json.dumps(dict(blocks=len(witnesses),instructions=sum(len(x['instructions']) for x in witnesses.values()),tails={k:v['undecoded_tail'] for k,v in witnesses.items() if v['undecoded_tail']})))
if __name__=='__main__':main()
