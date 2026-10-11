#!/usr/bin/env python3
"""Extract exact-hash multi-target weapon/explosion witnesses."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import capstone
import pefile
TARGET_HASH='94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0'
BLOCKS={
 'bullet_bounce':(0x409830,0x40994d),
 'bounce_candidate':(0x40a820,0x40a87e),
 'siege_body_watch_target':(0x428a30,0x428a71),
 'turret_attack':(0x41f170,0x41f2f9),
 'lurker_splash_candidate':(0x409770,0x409822),
 'secondary_train_fighter':(0x41c630,0x41c77e),
 'hangar_add':(0x41cc80,0x41ccd0),
 'hangar_launch':(0x41ccd0,0x41ce35),
 'reaver_launch_order':(0x41c790,0x41c85e),
 'reaver_attack_order':(0x41c860,0x41c90a),
 'scarab_order':(0x41c950,0x41ca6a),
 'reaver_carrier_range':(0x41bfe0,0x41c256),
 'same_ground_group':(0x481cb0,0x481cfe),
 'prepend_target_order':(0x429bc0,0x429c40),
 'reaver_distance_limit':(0x41d0b0,0x41d0cf),
 'reaver_attack_state_id':(0x41d0d0,0x41d0f8),
 'ground_route_distance':(0x48e9b0,0x48eb86),
 'iscript_use_weapon':(0x41f4d0,0x41f52f),
 'hangar_detach':(0x41cfe0,0x41d05c),
 'iscript_attkshiftproj_handler_fragment':(0x411023,0x41105a),
}



def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--exe',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
 raw=a.exe.read_bytes();assert hashlib.sha256(raw).hexdigest()==TARGET_HASH
 pe=pefile.PE(data=raw);assert pe.OPTIONAL_HEADER.ImageBase==0x400000 and pe.FILE_HEADER.Machine==0x14C
 md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);witnesses={}
 for name,(lo,hi) in BLOCKS.items():
  code=pe.get_data(lo-0x400000,hi-lo);ins=list(md.disasm(code,lo));length=sum(x.size for x in ins)
  witnesses[name]=dict(preferred_va=hex(lo),rva=hex(lo-0x400000),end_exclusive=hex(hi),requested_bytes=len(code),decoded_bytes=length,undecoded_tail=code[length:].hex(),instructions=[dict(va=hex(x.address),rva=hex(x.address-0x400000),file_offset=hex(pe.get_offset_from_rva(x.address-0x400000)),bytes=x.bytes.hex(),mnemonic=x.mnemonic,operands=x.op_str) for x in ins])
 result=dict(target_sha256=TARGET_HASH,target_version='1.17.0.1',original_code_executed=False,extractor_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),witnesses=witnesses,iscript_dispatch=dict(preferred_va='0x4111e4',destinations=[hex(x) for x in struct.unpack('<69I',pe.get_data(0x111E4,276))]),native_dat_descriptors={key:[dict(index=i,target_va=hex(struct.unpack('<3I',pe.get_data(base-0x400000+i*12,12))[0]),entry_width=struct.unpack('<3I',pe.get_data(base-0x400000+i*12,12))[1],count=struct.unpack('<3I',pe.get_data(base-0x400000+i*12,12))[2]) for i in range(count)] for key,base,count in (('units',0x5014F0,54),('flingy',0x501148,7),('sprites',0x501408,6),('images',0x5011A8,14),('weapons',0x501828,24),('orders',0x501278,19))})
 a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_text(json.dumps(result,indent=2)+'\n')
 print(json.dumps(dict(blocks=len(witnesses),instructions=sum(len(x['instructions']) for x in witnesses.values()),tails={k:v['undecoded_tail'] for k,v in witnesses.items() if v['undecoded_tail']})))
if __name__=='__main__':main()
