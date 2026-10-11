#!/usr/bin/env python3
"""Read exact-hash connected campaign-wave timing and scenario witnesses."""
import argparse,hashlib,json
from pathlib import Path
import pefile,capstone
TARGET_HASH='94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0'
BLOCKS={
 'military_computer_idle_consumer':(0x4b1010,0x4b10fd),
 'military_nearby_bunker_entry':(0x4ab630,0x4ab6c3),
 'movement_iscript_animation_entry':(0x42e9b0,0x42ea06),
 'combat_target_region_gate':(0x421320,0x421362),
 'combat_point_region_gate':(0x421370,0x4213b1),
 'acquired_target_issue_attack':(0x4202c0,0x4203c8),
 'movement_wait_retry_state15':(0x42bfd0,0x42c022),
 'strategic_update_elapsed_gate':(0x4acdf0,0x4ace1e),
 'strategic_update_return':(0x4ad219,0x4ad222),
 'military_assigned_region_ground_target':(0x4ae870,0x4ae89d),
 'military_assigned_region_air_target':(0x4ae840,0x4ae86d),
 'original_elapsed_and_wait_timer':(0x4be410,0x4be4fd),
 'frame_progress_timer_gate':(0x47cb60,0x47cb66),
 'objective_update_default_guard':(0x4ba910,0x4ba9d1),
 'objective_update_default_return':(0x4bab31,0x4bab32),
 'countdown_hide_widget':(0x4a79a0,0x4a79b7),
 'widget_lookup_by_identifier':(0x4c2200,0x4c2220),
 'widget_hide':(0x4c22f0,0x4c2381),
}
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--exe',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
 raw=a.exe.read_bytes();assert hashlib.sha256(raw).hexdigest()==TARGET_HASH;pe=pefile.PE(data=raw);assert pe.OPTIONAL_HEADER.ImageBase==0x400000 and pe.FILE_HEADER.Machine==0x14c
 md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);witnesses={}
 for name,(lo,hi)in BLOCKS.items():
  code=pe.get_data(lo-0x400000,hi-lo);ins=list(md.disasm(code,lo));assert sum(i.size for i in ins)==len(code)
  witnesses[name]=dict(start_preferred_va=hex(lo),end_exclusive_preferred_va=hex(hi),bytes_sha256=hashlib.sha256(code).hexdigest(),instructions=[dict(address=hex(i.address),bytes=i.bytes.hex(),mnemonic=i.mnemonic,operands=i.op_str)for i in ins])
 r=dict(target_sha256=TARGET_HASH,target_version='1.17.0.1',inspector_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),original_code_executed=False,witnesses=witnesses)
 a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_text(json.dumps(r,indent=2)+'\n');print(json.dumps(dict(witnesses=len(witnesses),instructions=sum(len(w['instructions'])for w in witnesses.values()))))
if __name__=='__main__':main()
