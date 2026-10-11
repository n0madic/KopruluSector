#!/usr/bin/env python3
"""Extract exact-hash transport boarding, cargo and unloading witnesses."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import capstone
import pefile
TARGET_HASH='94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0'
BLOCKS={
 'can_enter_transport':(0x437440,0x437532),
 'cargo_detach':(0x437320,0x437436),
 'enter_transport_order':(0x4376d0,0x437796),
 'pickup_transport_order':(0x4377a0,0x437843),
 'pickup_bunker_order':(0x437850,0x43788a),
 'move_unload_order':(0x437890,0x4379fa),
 'transport_idle_order':(0x437b50,0x437b92),
 'unload_order':(0x437ba0,0x437c66),
 'load_passenger':(0x437c70,0x437d9a),
 'iterate_cargo_callback':(0x437da0,0x437df5),
 'unload_passenger':(0x437e00,0x437f74),
 'get_unload_position':(0x438180,0x4381cb),
 'cargo_has_space':(0x4382f0,0x438341),
 'cargo_space_used':(0x438350,0x438385),
 'release_worker_construction_reservation':(0x484f10,0x484f31),
 'generation_encode':(0x428bd0,0x428c0b),
 'move_towards_target_and_waypoint':(0x43a0a0,0x43a0d0),
 'transported_unit_movement_reset':(0x42c7a0,0x42c7d8),
 'restore_status_bit20000_from_dat_bit08000000':(0x4249e0,0x424a00),
 'give_unit_and_cargo_frontend':(0x4b81a0,0x4b82bb),
 'give_cargo_callback':(0x4b82c0,0x4b82c9),
 'prepend_order_without_target':(0x429b20,0x429b63),
 'insert_order_after_or_at_tail':(0x4295d0,0x42964f),
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
