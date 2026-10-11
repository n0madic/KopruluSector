#!/usr/bin/env python3
"""Extract exact-hash active-unit and gameplay IScript ordering witnesses."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import capstone
import pefile
TARGET_HASH='94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0'
BLOCKS={
 'movement_state_dispatcher':(0x42AD30,0x42AF44),
 'set_flingy_current_speed':(0x40CB90,0x40CBC8),
 'clamp_turret_heading_32':(0x40BA90,0x40BAE0),
 'set_flingy_fixed_position':(0x40CC00,0x40CC33),
 'prepare_flingy_movement':(0x40BCD0,0x40BD0B),
 'commit_flingy_movement':(0x40BDB0,0x40BE7F),
 'flingy_target_direction':(0x40CF50,0x40D014),
 'flingy_prepared_position':(0x40D020,0x40D10D),
 'flingy_walking_animation_gate':(0x40D110,0x40D192),
 'flingy_heading_step':(0x40D1A0,0x40D252),
 'flingy_speed_step':(0x40D260,0x40D446),
 'flingy_velocity_heading_step':(0x40D450,0x40D4EA),
 'air_waypoint_map_clamp':(0x42AA60,0x42AB06),
 'movement_commit_prepared_position':(0x42AB10,0x42ABA8),
 'energy_capacity_with_upgrades':(0x434940,0x4349D6),
 'air_density_cell_update':(0x4815D0,0x4816B7),
 'air_density_displacement':(0x481880,0x4819F5),
 'movement_air_stationary_init':(0x42B7F0,0x42B8C3),
 'unit_max_energy':(0x434A50,0x434A72),
 'movement_stationary_init':(0x42B030,0x42B1A8),
 'movement_initialize':(0x42BCC0,0x42BE0D),
 'active_unit_movement_prep':(0x43A5F0,0x43A715),
 'goliath_turret_heading_clamp':(0x435A30,0x435A40),
 'subunit_turn_sync':(0x43A800,0x43A876),
 'movement_subunit_stationary':(0x42C610,0x42C664),
 'set_move_target_position':(0x43A0D0,0x43A180),
 'clear_movement_turn_when_within_10':(0x40C6F0,0x40C71F),
 'flingy_heading_and_image_step':(0x40BEE0,0x40BEFD),
 'unit_status_cooldown_step':(0x43A320,0x43A48F),
 'unit_energy_step':(0x438E20,0x438EDB),
 'sprite_selection_timer_step':(0x43A580,0x43A5EB),
 'creep_schedule_countdown':(0x4B4010,0x4B403B),
 'resource_area_update':(0x496C80,0x496E0E),
 'bullet_step':(0x40A5F0,0x40A6B2),
 'flingy_step':(0x40C910,0x40C9E1),
 'iscript_nobrk_end':(0x429CC0,0x429CF2),
}

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--exe',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
 raw=a.exe.read_bytes();assert hashlib.sha256(raw).hexdigest()==TARGET_HASH
 pe=pefile.PE(data=raw);assert pe.OPTIONAL_HEADER.ImageBase==0x400000 and pe.FILE_HEADER.Machine==0x14C
 md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);witnesses={}
 for name,(lo,hi) in BLOCKS.items():
  code=pe.get_data(lo-0x400000,hi-lo);ins=list(md.disasm(code,lo));length=sum(x.size for x in ins)
  witnesses[name]=dict(preferred_va=hex(lo),rva=hex(lo-0x400000),end_exclusive=hex(hi),requested_bytes=len(code),decoded_bytes=length,undecoded_tail=code[length:].hex(),instructions=[dict(va=hex(x.address),rva=hex(x.address-0x400000),file_offset=hex(pe.get_offset_from_rva(x.address-0x400000)),bytes=x.bytes.hex(),mnemonic=x.mnemonic,operands=x.op_str) for x in ins])
 result=dict(target_sha256=TARGET_HASH,target_version='1.17.0.1',original_code_executed=False,extractor_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),witnesses=witnesses,primary_death_dispatch=dict(preferred_va='0x4399c0',destinations=[hex(x) for x in struct.unpack('<5I',pe.get_data(0x399C0,20))],mapping=list(pe.get_data(0x399D4,175)),death_order=0,death_case_va='0x439448',tail_target_va='0x41e630'),iscript_dispatch=dict(preferred_va='0x4111e4',destinations=[hex(x) for x in struct.unpack('<69I',pe.get_data(0x111E4,276))]),native_dat_descriptors={key:[dict(index=i,target_va=hex(struct.unpack('<3I',pe.get_data(base-0x400000+i*12,12))[0]),entry_width=struct.unpack('<3I',pe.get_data(base-0x400000+i*12,12))[1],count=struct.unpack('<3I',pe.get_data(base-0x400000+i*12,12))[2]) for i in range(count)] for key,base,count in (('units',0x5014F0,54),('flingy',0x501148,7),('sprites',0x501408,6),('images',0x5011A8,14),('weapons',0x501828,24))},unit_remove_switch=dict(destinations=[hex(x) for x in struct.unpack('<6I',pe.get_data(0x25500,24))],mapping=list(pe.get_data(0x25518,143))),draw_update_callbacks=[hex(x) for x in struct.unpack('<36I',pe.get_data(0x103044,144))[::2]])
 result['movement_dispatch']=dict(preferred_va='0x42af44',destinations=[hex(x) for x in struct.unpack('<36I',pe.get_data(0x2AF44,144))],state_field='CUnit+0x97',repeat_when_return_eax_nonzero=True)
 result['energy_capacity_dispatch']=dict(preferred_va='0x4349d8',destinations=[hex(x) for x in struct.unpack('<12I',pe.get_data(0x349D8,48))],mapping_va='0x434a08',mapping=list(pe.get_data(0x34A08,71)),mapping_unit_start=1)
 a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_text(json.dumps(result,indent=2)+'\n')
 print(json.dumps(dict(blocks=len(witnesses),instructions=sum(len(x['instructions']) for x in witnesses.values()),tails={k:v['undecoded_tail'] for k,v in witnesses.items() if v['undecoded_tail']})))
if __name__=='__main__':main()
