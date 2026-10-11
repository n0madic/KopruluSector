#!/usr/bin/env python3
"""Extract exact-hash attack order and periodic spell-timer witnesses."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import capstone
import pefile
TARGET_HASH='94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0'
BLOCKS={
 'normalize_acquisition_ranges':(0x425610,0x425682),
 'nearby_ally_attack_reaction':(0x420120,0x42018f),
 'human_attack_or_escape_reaction':(0x420190,0x4202ba),
 'prepend_move_for_unattackable_target':(0x429b70,0x429bbe),
 'attack_unit':(0x41e5c0,0x41e621),
 'pick_attack_target':(0x41de10,0x41df81),
 'attack_unit_range_and_chase':(0x420770,0x420908),
 'attack_animation_selector':(0x41f630,0x41f6b2),
 'attack_cooldown_animation':(0x41f530,0x41f5d0),
 'weapon_cooldown':(0x41fb70,0x41fbd1),
 'weapon_speed_status':(0x428ac0,0x428ae8),
 'ready_to_attack':(0x41ff50,0x41fff3),
 'weapon_facing':(0x41fe60,0x41fece),
 'rectangle_distance':(0x422850,0x422999),
 'point_rectangle_distance':(0x422760,0x422845),
 'subunit_predicate':(0x420000,0x420027),
 'clear_last_bullet_spawner':(0x40a9e0,0x40a9eb),
 'stop_for_attack':(0x43a490,0x43a4d4),
 'ground_weapon_with_subunit':(0x41fbe0,0x41fc11),
 'air_weapon_with_subunit':(0x41fb40,0x41fb63),
 'unit_bounds_at_position':(0x409d20,0x409d6f),
 'unit_position_blocker':(0x42eab0,0x42ebe9),
 'unit_blocker_predicate':(0x42e130,0x42e1b5),
 'movement_stationary_end':(0x42b1b0,0x42b239),
 'movement_position_blocked':(0x42ca80,0x42cb88),
 'movement_stationary_hold':(0x42b240,0x42b2ad),
 'change_move_target_unit':(0x40cce0,0x40cd29),
 'flag_old_path_changed':(0x42c790,0x42c79f),
 'chase_target':(0x43a180,0x43a253),
 'ranged_chase':(0x438ba0,0x438c11),
 'progress_spell_timers':(0x4351e0,0x4352f7),
 'acid_spore_timers':(0x43e280,0x43e40a),
 'stim_expiry_speed':(0x43aea0,0x43aee1),
 'ensnare_expiry':(0x43dee0,0x43df01),
 'iscript_speed_recalculate':(0x42dff0,0x42e0cb),
 'unit_timed_life_duration':(0x434a90,0x434ab2),
 'hallucination_conversion':(0x430a00,0x430afb),
 'hallucinated_initialization':(0x430960,0x4309fe),
 'hallucination_secondary':(0x430c70,0x430cda),
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
