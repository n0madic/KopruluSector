#!/usr/bin/env python3
"""Extract exact-hash paid Protoss construction and warp witnesses."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import capstone
import pefile
import inspect_native_pixel_effects_117 as pixel_source
import inspect_native_special_initialization_117 as special_source
import probe_native_placement_117 as placement_source
TARGET_HASH='94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0'
BLOCKS={
 'probe_build31':(0x42ee00,0x42f008),
 'probe_build32_adapter':(0x42f230,0x42f23b),
 'protoss_warp48':(0x42f020,0x42f196),
 'protoss_warp_image_modifier':(0x411840,0x4118c6),
 'pylon_field_sprite':(0x42f270,0x42f30b),
 'pylon_init_psi164':(0x42f5b0,0x42f631),
 'pylon_remove_psi_links':(0x430610,0x430664),
 'stock_warp_image_update':pixel_source.BLOCKS['warp_in_update'],
 'psi_image_explicit_flip':special_source.BLOCKS['image_explicit_flip'],
 'stock_power_opacity':placement_source.BLOCKS['grp_opaque_sample'],
}
STATIC_ONLY={'network_tbl_binding':(0x43f0d0,0x43f0f6),'stock_power_mask_bootstrap':(0x42fe60,0x42ffb4)}

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--exe',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
 raw=a.exe.read_bytes();assert hashlib.sha256(raw).hexdigest()==TARGET_HASH
 pe=pefile.PE(data=raw);assert pe.OPTIONAL_HEADER.ImageBase==0x400000 and pe.FILE_HEADER.Machine==0x14C
 md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);witnesses={}
 for name,(lo,hi) in {**BLOCKS,**STATIC_ONLY}.items():
  code=pe.get_data(lo-0x400000,hi-lo);ins=list(md.disasm(code,lo));length=sum(x.size for x in ins)
  assert length==len(code),(name,length,len(code))
  witnesses[name]=dict(preferred_va=hex(lo),rva=hex(lo-0x400000),end_exclusive=hex(hi),requested_bytes=len(code),decoded_bytes=length,undecoded_tail=code[length:].hex(),instructions=[dict(va=hex(x.address),rva=hex(x.address-0x400000),file_offset=hex(pe.get_offset_from_rva(x.address-0x400000)),bytes=x.bytes.hex(),mnemonic=x.mnemonic,operands=x.op_str) for x in ins])
 result=dict(target_sha256=TARGET_HASH,target_version='1.17.0.1',original_code_executed=False,extractor_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),witnesses=witnesses,static_only_witnesses=list(STATIC_ONLY),probe_build_switch=dict(table='0x42f008',destinations=[hex(x)for x in struct.unpack('<4I',pe.get_data(0x2f008,16))]),protoss_warp_switch=dict(table='0x42f198',destinations=[hex(x)for x in struct.unpack('<4I',pe.get_data(0x2f198,16))]),main_dispatch={str(order):dict(immediate_adapter=hex(struct.unpack('<I',pe.get_data(0x39a84+4*pe.get_data(0x39b18+order-4,1)[0],4))[0]),throttled_adapter=hex(struct.unpack('<I',pe.get_data(0x39bcc+4*pe.get_data(0x39d5c+order-1,1)[0],4))[0])) for order in (31,32,48)},early_order_dispatch=dict(table='0x4399c0',indices='0x4399d4',order48_adapter=hex(struct.unpack('<I',pe.get_data(0x399c0+4*pe.get_data(0x399d4+48,1)[0],4))[0])),network_tbl_binding=dict(path=pe.get_data(0xec2d0,32).split(b'\0',1)[0].decode('ascii'),pointer_global='0x665de0',loader='0x4c13e0',writer='0x43f0f0',native_loader_executed=False),iscript_dispatch=dict(preferred_va='0x4111e4',destinations=[hex(x) for x in struct.unpack('<69I',pe.get_data(0x111E4,276))]),native_dat_descriptors={key:[dict(index=i,target_va=hex(struct.unpack('<3I',pe.get_data(base-0x400000+i*12,12))[0]),entry_width=struct.unpack('<3I',pe.get_data(base-0x400000+i*12,12))[1],count=struct.unpack('<3I',pe.get_data(base-0x400000+i*12,12))[2]) for i in range(count)] for key,base,count in (('units',0x5014F0,54),('flingy',0x501148,7),('sprites',0x501408,6),('images',0x5011A8,14),('weapons',0x501828,24),('orders',0x501278,19))})
 a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_text(json.dumps(result,indent=2)+'\n')
 print(json.dumps(dict(blocks=len(witnesses),instructions=sum(len(x['instructions']) for x in witnesses.values()),tails={k:v['undecoded_tail'] for k,v in witnesses.items() if v['undecoded_tail']})))
if __name__=='__main__':main()
