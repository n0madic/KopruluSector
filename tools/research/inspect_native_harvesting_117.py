#!/usr/bin/env python3
"""Extract exact-hash harvesting and cargo witnesses."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import capstone
import pefile
TARGET_HASH='94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0'
BLOCKS={
 'tile_included_excluded_flags':(0x413220,0x41326b),
 'hatchery_initial_primary_order':(0x43cd50,0x43cd8d),
 'hatchery_creep_and_larva_secondary':(0x43d1a0,0x43d243),
 'hatchery_larva_production':(0x43d0d0,0x43d19c),
 'hatchery_larva_count_callback':(0x43d910,0x43d93d),
 'hatchery_larva_spawn_position':(0x43d360,0x43d800),
 'larva_spawn_position_validity':(0x43c490,0x43c554),
 'larva_initial_side':(0x43d990,0x43d9f7),
 'larva_idle_primary_order':(0x43cd90,0x43cf09),
 'larva_side_constraint':(0x43c3b0,0x43c473),
 'larva_fallback_position':(0x43d800,0x43d8f1),
 'cargo_overlay_position':(0x410380,0x4103c0),
 'prepend_nontarget_order':(0x429b20,0x429b63),
 'prepend_target_order':(0x429bc0,0x429c40),
 'move_to_minerals_order':(0x4239a0,0x423c21),
 'return_resource_order':(0x423ce0,0x423e35),
 'harvest_dispatch_order':(0x423480,0x4235b7),
 'remove_specific_queued_order':(0x42a0e0,0x42a13d),
 'change_movement_target_to_unit':(0x43a0a0,0x43a0d0),
 'target_tracking_movement_request':(0x43a180,0x43a253),
 'find_resource_depot':(0x431f00,0x431f6e),
 'resource_depot_candidate':(0x4323d0,0x43240b),
 'append_nontarget_order':(0x4295a0,0x4295c3),
 'prepend_packed_target_order':(0x429c40,0x429c7b),
 'movement_target_path_modified':(0x42c790,0x42c79f),
 'movement_target_unit_fields':(0x40cce0,0x40cd29),
 'resource_depot_reachable_gate':(0x48e980,0x48e9a5),
 'terran_building_hp_threshold_33':(0x438dd0,0x438e15),
 'unit_target_rectangle_boundary':(0x42e520,0x42e5f8),
 'path_target_prediction_and_commit':(0x420db0,0x420e33),
 'iscript_explicit_weapon':(0x41f4d0,0x41f52f),
 'reset_harvest_collision_order':(0x42e0d0,0x42e12b),
 'depot_same_ground_group':(0x481d00,0x481d50),
 'depot_spatial_nearest_wrapper':(0x442c20,0x442cbc),
 'resource_spatial_nearest_search':(0x442d90,0x44310c),
 'next_order_harvesting_predicate':(0x424550,0x424588),
 'next_queued_order_id':(0x429d10,0x429d17),
 'hatchery_family_type':(0x41bfb0,0x41bfdb),
 'hatchery_deposit_side_weights':(0x43da30,0x43db47),
 'clear_active_flag_and_order_done':(0x429d00,0x429d0f),
 'reset_collision_overlap_query':(0x42e600,0x42e667),
 'reset_collision_overlap_callback':(0x42e670,0x42e6b7),
 'resource_depot_available':(0x424460,0x4244be),
 'cargo_overlay_remove':(0x427410,0x427476),
 'cargo_deposit':(0x427480,0x427513),
 'resource_warning_owner_gate':(0x4461f0,0x446278),
 'resource_wait_list_pop':(0x423ed0,0x423f86),
 'resource_wait_list_request':(0x424230,0x4242e3),
 'resource_wait_list_cancel':(0x424170,0x42422b),
 'wait_for_minerals_order':(0x423c30,0x423c67),
 'mining_facing_predicate':(0x424810,0x42485f),
 'player_race_worker_id':(0x486070,0x486095),
 'race_worker_count_consumer':(0x4939e0,0x493a5e),
 'player_resource_ai_accounting':(0x482f00,0x482f1c),
 'sprite_replace_images':(0x415590,0x4156f8),
 'reapply_unit_status_overlays':(0x434d10,0x434d94),
 'mineral_amount_animation':(0x4247b0,0x424805),
 'resource_creation_sprite_change':(0x423330,0x4233f4),
 'mining_minerals_order':(0x423810,0x423995),
 'extract_resource_amount':(0x423e40,0x423ec5),
 'resource_to_worker_cargo':(0x423fd0,0x424071),
 'worker_cargo_setter':(0x427090,0x4270ef),
 'computer_harvest_bonus_predicate':(0x482f40,0x482f7f),
 'resource_start_service':(0x4240d0,0x42412d),
 'resource_finish_service':(0x424130,0x42416a),
}




def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--exe',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
 raw=a.exe.read_bytes();assert hashlib.sha256(raw).hexdigest()==TARGET_HASH
 pe=pefile.PE(data=raw);assert pe.OPTIONAL_HEADER.ImageBase==0x400000 and pe.FILE_HEADER.Machine==0x14C
 md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);witnesses={}
 for name,(lo,hi) in BLOCKS.items():
  code=pe.get_data(lo-0x400000,hi-lo);ins=list(md.disasm(code,lo));length=sum(x.size for x in ins)
  assert length==len(code),(name,length,len(code))
  witnesses[name]=dict(preferred_va=hex(lo),rva=hex(lo-0x400000),end_exclusive=hex(hi),requested_bytes=len(code),decoded_bytes=length,undecoded_tail=code[length:].hex(),instructions=[dict(va=hex(x.address),rva=hex(x.address-0x400000),file_offset=hex(pe.get_offset_from_rva(x.address-0x400000)),bytes=x.bytes.hex(),mnemonic=x.mnemonic,operands=x.op_str) for x in ins])
 result=dict(target_sha256=TARGET_HASH,target_version='1.17.0.1',original_code_executed=False,extractor_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),witnesses=witnesses,iscript_dispatch=dict(preferred_va='0x4111e4',destinations=[hex(x) for x in struct.unpack('<69I',pe.get_data(0x111E4,276))]),native_dat_descriptors={key:[dict(index=i,target_va=hex(struct.unpack('<3I',pe.get_data(base-0x400000+i*12,12))[0]),entry_width=struct.unpack('<3I',pe.get_data(base-0x400000+i*12,12))[1],count=struct.unpack('<3I',pe.get_data(base-0x400000+i*12,12))[2]) for i in range(count)] for key,base,count in (('units',0x5014F0,54),('flingy',0x501148,7),('sprites',0x501408,6),('images',0x5011A8,14),('weapons',0x501828,24),('orders',0x501278,19))})
 a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_text(json.dumps(result,indent=2)+'\n')
 print(json.dumps(dict(blocks=len(witnesses),instructions=sum(len(x['instructions']) for x in witnesses.values()),tails={k:v['undecoded_tail'] for k,v in witnesses.items() if v['undecoded_tail']})))
if __name__=='__main__':main()
