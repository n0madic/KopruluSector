#!/usr/bin/env python3
"""Extract exact-hash paid Zerg construction and Larva morph witnesses."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import capstone
import pefile
TARGET_HASH='94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0'
BLOCKS={
 'drone_start_build25':(0x43ba90,0x43babb),
 'drone_build26':(0x43b900,0x43ba8d),
 'drone_land70':(0x43c970,0x43ccba),
 'drone_to_zerg_building':(0x43c000,0x43c031),
 'initialize_zerg_construction':(0x43bf10,0x43bf74),
 'worker_ai_to_building_ai':(0x486ba0,0x486be5),
 'cancel_zerg_construction':(0x43b380,0x43b4fe),
 'unit_morph42':(0x43bac0,0x43bc4c),
 'unit_birth41':(0x43bd50,0x43be7d),
 'spawn_larva_secondary78':(0x43d0d0,0x43d19c),
 'spawning_creep_and_larva':(0x43d1a0,0x43d243),
 'larva_spawn_location':(0x43d360,0x43d800),
 'larva_spawn_candidate':(0x43c490,0x43c554),
 'larva_count_callback':(0x43d910,0x43d93d),
 'larva_parent_side_state':(0x43d990,0x43d9f7),
 'change_unit_type':(0x424ad0,0x424bcc),
 'change_type_preserve_sprite_state':(0x424bd0,0x424d29),
 'restore_status_overlays_after_type':(0x424a00,0x424ac5),
 'portrait_change_disabled_backend_guard':(0x4268d0,0x4269a5),
 'clear_attack_animation_after_type':(0x41d4c0,0x41d4e1),
 'hatchery_idle101':(0x43cd50,0x43cd8d),
 'larva_main77':(0x43cd90,0x43cf09),
 'larva_stray_predicate':(0x43c3b0,0x43c473),
 'larva_choose_point':(0x43d800,0x43d8f1),
 'reinitialize_type_and_rescale_hp':(0x426750,0x4268c9),
 'movement_wait_and_retry15':(0x42bfd0,0x42c022),
 'building_ai_back_to_worker_ai':(0x487900,0x487942),
 'landing_shadow_follow_image_flag':(0x416970,0x41697f),
 'landing_shadow_vertical_offset':(0x417260,0x417276),
 'landing_shadow_clear_follow_flag':(0x417840,0x417853),
 'set_image_vertical_offset':(0x411800,0x411817),
 'refresh_sprite_image_dirty_rects':(0x416520,0x4165c6),
 'carried_powerup_reference':(0x421600,0x421618),
 'deferred_fog_tile_cache_scan':(0x40af30,0x40b05c),
 'fog_visibility_clear_and_dirty':(0x46c180,0x46c1df),
 'fog_dirty_window5':(0x445880,0x445891),
 'dirty_window_rectangle':(0x4cadf0,0x4cae2e),
 'dirty_grid_rectangle':(0x4cac50,0x4cad09),
 'creep_recession_one_record_consumer':(0x4b4a40,0x4b4b79),
 'strategic_update_elapsed_time_guard':(0x4acdf0,0x4ace1e),
 'strategic_update_guard_return':(0x4ad219,0x4ad228),
}
BLOCKS['building_current_stock_check']=(0x4086b0,0x408706)
STATIC_ONLY={'network_tbl_binding':(0x43f0d0,0x43f0f6)}

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--exe',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
 raw=a.exe.read_bytes();assert hashlib.sha256(raw).hexdigest()==TARGET_HASH
 pe=pefile.PE(data=raw);assert pe.OPTIONAL_HEADER.ImageBase==0x400000 and pe.FILE_HEADER.Machine==0x14C
 md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);witnesses={}
 for name,(lo,hi) in {**BLOCKS,**STATIC_ONLY}.items():
  code=pe.get_data(lo-0x400000,hi-lo);ins=list(md.disasm(code,lo));length=sum(x.size for x in ins)
  assert length==len(code),(name,length,len(code))
  witnesses[name]=dict(preferred_va=hex(lo),rva=hex(lo-0x400000),end_exclusive=hex(hi),requested_bytes=len(code),decoded_bytes=length,undecoded_tail=code[length:].hex(),instructions=[dict(va=hex(x.address),rva=hex(x.address-0x400000),file_offset=hex(pe.get_offset_from_rva(x.address-0x400000)),bytes=x.bytes.hex(),mnemonic=x.mnemonic,operands=x.op_str) for x in ins])
 result=dict(target_sha256=TARGET_HASH,target_version='1.17.0.1',original_code_executed=False,extractor_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),witnesses=witnesses,static_only_witnesses=list(STATIC_ONLY),zerg_build_self_switch=dict(table='0x43b8e4',destinations=[hex(x) for x in struct.unpack('<7I',pe.get_data(0x3b8e4,28))]),main_dispatch={str(order):dict(immediate_adapter=hex(struct.unpack('<I',pe.get_data(0x39a84+4*pe.get_data(0x39b18+order-4,1)[0],4))[0]),throttled_adapter=hex(struct.unpack('<I',pe.get_data(0x39bcc+4*pe.get_data(0x39d5c+order-1,1)[0],4))[0])) for order in (25,26,41,42,45,70,77,101,150)},network_tbl_binding=dict(path=pe.get_data(0xec2d0,32).split(b'\0',1)[0].decode('ascii'),pointer_global='0x665de0',loader='0x4c13e0',writer='0x43f0f0',native_loader_executed=False),iscript_dispatch=dict(preferred_va='0x4111e4',destinations=[hex(x) for x in struct.unpack('<69I',pe.get_data(0x111E4,276))]),native_dat_descriptors={key:[dict(index=i,target_va=hex(struct.unpack('<3I',pe.get_data(base-0x400000+i*12,12))[0]),entry_width=struct.unpack('<3I',pe.get_data(base-0x400000+i*12,12))[1],count=struct.unpack('<3I',pe.get_data(base-0x400000+i*12,12))[2]) for i in range(count)] for key,base,count in (('units',0x5014F0,54),('flingy',0x501148,7),('sprites',0x501408,6),('images',0x5011A8,14),('weapons',0x501828,24),('orders',0x501278,19))})
 a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_text(json.dumps(result,indent=2)+'\n')
 print(json.dumps(dict(blocks=len(witnesses),instructions=sum(len(x['instructions']) for x in witnesses.values()),tails={k:v['undecoded_tail'] for k,v in witnesses.items() if v['undecoded_tail']})))
if __name__=='__main__':main()
