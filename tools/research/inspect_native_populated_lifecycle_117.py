#!/usr/bin/env python3
"""Extract exact-hash unit sprite/image/IScript initialization witnesses."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import capstone
import pefile
TARGET_HASH='94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0'
BLOCKS={
 'recycle_pending_unit':(0x4256B0,0x4256E9),
 'object_order_step':(0x438EF0,0x439308),
 'unit_step_prep':(0x438CD0,0x438DC6),
 'pending_unit_head':(0x4255B0,0x4255B6),
 'hidden_unit_head':(0x4255E0,0x4255E6),
 'active_unit_head':(0x425600,0x425606),
 'owner_unit_head':(0x4256F0,0x4256F8),
 'ai_step_cache_clear':(0x4B0BD0,0x4B0BDB),
 'selection_inactive_gate':(0x49C0F0,0x49C136),
 'placement_ui_refresh_gate':(0x423120,0x423142),
 'placement_ui_active':(0x47D070,0x47D076),
 'target_ui_active':(0x4B2840,0x4B2846),
 'cloak_list_step':(0x434310,0x4343AD),
 'psi_step':(0x430670,0x430772),
 'bullet_cache_step':(0x40A880,0x40A8B5),
 'stock_completed_counter_get':(0x4143E0,0x4143F4),
 'flingy_relative_turn':(0x40BFD0,0x40C043),
 'flingy_relative_turn_left':(0x40BF50,0x40BFC5),
 'pending_creep_timer_gate':(0x4B4A10,0x4B4A31),
}
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--exe',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
 raw=a.exe.read_bytes();assert hashlib.sha256(raw).hexdigest()==TARGET_HASH
 pe=pefile.PE(data=raw);assert pe.OPTIONAL_HEADER.ImageBase==0x400000 and pe.FILE_HEADER.Machine==0x14C
 md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);witnesses={}
 for name,(lo,hi) in BLOCKS.items():
  code=pe.get_data(lo-0x400000,hi-lo);ins=list(md.disasm(code,lo));length=sum(x.size for x in ins)
  witnesses[name]=dict(preferred_va=hex(lo),rva=hex(lo-0x400000),end_exclusive=hex(hi),requested_bytes=len(code),decoded_bytes=length,undecoded_tail=code[length:].hex(),instructions=[dict(va=hex(x.address),rva=hex(x.address-0x400000),file_offset=hex(pe.get_offset_from_rva(x.address-0x400000)),bytes=x.bytes.hex(),mnemonic=x.mnemonic,operands=x.op_str) for x in ins])
 result=dict(target_sha256=TARGET_HASH,target_version='1.17.0.1',original_code_executed=False,extractor_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),witnesses=witnesses,iscript_dispatch=dict(preferred_va='0x4111e4',destinations=[hex(x) for x in struct.unpack('<69I',pe.get_data(0x111E4,276))]),native_dat_descriptors={key:[dict(index=i,target_va=hex(struct.unpack('<3I',pe.get_data(base-0x400000+i*12,12))[0]),entry_width=struct.unpack('<3I',pe.get_data(base-0x400000+i*12,12))[1],count=struct.unpack('<3I',pe.get_data(base-0x400000+i*12,12))[2]) for i in range(count)] for key,base,count in (('units',0x5014F0,54),('flingy',0x501148,7),('sprites',0x501408,6),('images',0x5011A8,14))},unit_remove_switch=dict(destinations=[hex(x) for x in struct.unpack('<6I',pe.get_data(0x25500,24))],mapping=list(pe.get_data(0x25518,143))),draw_update_callbacks=[hex(x) for x in struct.unpack('<36I',pe.get_data(0x103044,144))[::2]])
 a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_text(json.dumps(result,indent=2)+'\n')
 print(json.dumps(dict(blocks=len(witnesses),instructions=sum(len(x['instructions']) for x in witnesses.values()),tails={k:v['undecoded_tail'] for k,v in witnesses.items() if v['undecoded_tail']})))
if __name__=='__main__':main()
