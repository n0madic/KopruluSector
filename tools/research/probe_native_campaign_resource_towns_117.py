#!/usr/bin/env python3
"""Compose original campaign resource producers, start extraction and Town binding.

Without the explicit execution flag this prints a plan only. The guest executes
reviewed original instructions with actual callees, no stubs, Windows imports,
process startup, outer accepted-frame orchestration, host files or network.
"""
import argparse
from collections import deque, Counter
import hashlib
import json
from pathlib import Path
import platform
import struct
import subprocess
import sys
import types
import pefile
import inspect_native_image_initialization_117 as image_source
import inspect_native_populated_lifecycle_117 as pending_source
import inspect_native_unit_death_117 as death_blocks
import probe_native_unit_death_117 as death_source
import inspect_native_active_iscript_117 as active_blocks
import probe_native_active_iscript_117 as active_source
import inspect_native_projectiles_117 as projectile_blocks
import probe_native_projectiles_117 as projectile_source
import probe_native_attack_orders_117 as attack_source
import probe_native_path_progress_117 as paths
import probe_native_blocked_movement_117 as blocked_source
import inspect_native_resource_generation_117 as local
import probe_native_campaign_resource_areas_117 as authentic_source
import probe_native_resource_generation_117 as frozen_generation
import probe_native_mission_units_117 as mission_source
import probe_native_terrain_import_117 as terrain_source
import probe_native_campaign_regions_117 as campaign_source
import inspect_terrain_assets as terrain_assets
import probe_native_resource_areas_117 as resource_source
import probe_native_automatic_requests_117 as automatic_source
import probe_native_towns_117 as towns_source
import inspect_native_production_117 as production_source
import inspect_native_training_117 as training_source
import inspect_native_transport_117 as transport_blocks
import inspect_native_computer_combat_117 as computer_blocks
import probe_native_ai_placement_117 as ai_placement_source
import inspect_native_region_states_117 as state_source
import probe_native_construction_117 as construction_source
import inspect_native_workers_117 as worker_blocks
import inspect_native_ai_removal_117 as removal_blocks
import inspect_native_guard_lifecycle_117 as guard_blocks
import probe_native_gas_harvesting_117 as gas_source
import probe_native_placement_117 as placement_source
import inspect_native_creep_117 as creep_source
import inspect_native_unit_ai_117 as unitai
import inspect_native_combat_ai_117 as combat
import probe_native_contour_fit_117 as contour_source
import probe_native_movement_collision_117 as collision_source
import inspect_native_enemy_strength_117 as strength_source
import inspect_economy_frame_117 as frame_source
import inspect_ai_opcodes_117 as script_source
import inspect_native_radius_recruitment_117 as region_source
import inspect_native_damage_117 as damage_source
import probe_native_trigger_move_images_117 as move_images_source
import probe_native_trigger_grounded_move_117 as move_source
import probe_native_populated_lifecycle_117 as populated_source
import probe_native_unit_creation_117 as hidden_source
import probe_native_image_initialization_117 as initialized_source
import inspect_native_training_lifecycle_117 as lifecycle
import inspect_native_unit_creation_117 as creation
import inspect_native_primary_orders_117 as orders
import inspect_native_training_117 as admission
import probe_native_trigger_ownership_117 as ownership
import probe_native_trigger_unit_actions_117 as actions
UNIT=0x5DA3A0;SPRITE=0x2000000;IMAGE=0x2100000;STACK=0x900000;STOP=0xA00000;SCRIPT=0xD00000;ASSETS=0xE00000
SELECT=('live_unit_add','live_unit_account','unit_group_upgrade_effects','speed_from_flingy','acceleration_from_flingy','turn_from_flingy','speed_status_modifier','enable_speed_upgrade','iscript_speed_control_zero_guard','upgrade_level_get')
DEATH=('drop_powerup_zero_carry','clear_unit_ai','remove_military_ai_null','remove_town_ai_null','clear_town_resource_refs','clear_guard_ai_null','clear_queen_selection')
ALLOW=sorted(set(list(combat.BLOCKS.values())+list(mission_source.ALLOW)+list(terrain_source.BLOCKS.values())+list(resource_source.ALLOW)+list(automatic_source.ALLOW)+[combat.BLOCKS["computer_ai"]]+list(guard_blocks.default.WITNESSES.values())+list(guard_blocks.default.dispatch.WITNESSES.values())+list(computer_blocks.BLOCKS.values())+list(transport_blocks.BLOCKS.values())+list(production_source.WITNESSES.values())+list(training_source.WITNESSES.values())+list(training_source.unit_reqs.WITNESSES.values())+list(ai_placement_source.ALLOW)+list(frame_source.BLOCKS.values())+list(towns_source.ALLOW)+list(state_source.WITNESSES.values())+list(state_source.targets.WITNESSES.values())+list(state_source.wave.WITNESSES.values())+list(state_source.wave.attack.WITNESSES.values())+list(state_source.donor.WITNESSES.values())+list(state_source.consumers.WITNESSES.values())+list(worker_blocks.BLOCKS.values())+list(removal_blocks.WITNESSES.values())+list(guard_blocks.WITNESSES.values())+list(script_source.WITNESSES.values())+list(local.BLOCKS.values())+[combat.BLOCKS['nearest_rectangle_search_setup'],combat.BLOCKS['change_region_state'],frame_source.BLOCKS['build_request_wrapper'],frame_source.BLOCKS['town_request_insert']]))
PLAN=dict(target_sha256=local.TARGET_HASH,helper_stubs=[],instruction_bound=8000000,scope='Original resource-area generation and clustering',limits='Explicit clear world and controller/campaign inputs; no outer accepted frame, original loader or renderer.')
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--exe',type=Path,required=True);p.add_argument('--data',type=Path,required=True);p.add_argument('--manifest',type=Path,required=True);p.add_argument('--assets',type=Path,required=True);p.add_argument('--asset-manifest',type=Path,required=True);p.add_argument('--pyms',type=Path,required=True);p.add_argument('--out',type=Path);p.add_argument('--case-group',choices=('all','campaign'),default='all');p.add_argument('--terrain-report',type=Path,required=True);p.add_argument('--terrain-data',type=Path,required=True);p.add_argument('--regions-report',type=Path,required=True);p.add_argument('--snapshots',type=Path,required=True);p.add_argument('--map-indices',default='56,58');p.add_argument('--execute-original-code',action='store_true');a=p.parse_args()
 raw=a.exe.read_bytes();assert hashlib.sha256(raw).hexdigest()==local.TARGET_HASH
 if not a.execute_original_code:print(json.dumps(PLAN,indent=2));return
 assert a.out and platform.machine().lower() in ('x86_64','amd64')
 assert subprocess.check_output(['git','-C',str(a.pyms),'rev-parse','HEAD'],text=True).strip()=='bfc5d3aad0b5614a5aff72c223f8efa00afddfa4'
 manifest=json.loads(a.manifest.read_text());assetmanifest=json.loads(a.asset_manifest.read_text());dat_hashes={};assets={};lo_assets=[]
 def stock(path):
  v=(a.data/path.replace('\\','/')).read_bytes();h=hashlib.sha256(v).hexdigest();assert h==manifest['effective'][path]['sha256'];dat_hashes[path]=h;return v
 sys.path.insert(0,str(a.pyms))
 from PyMS.FileFormats.DAT.UnitsDAT import UnitsDAT
 from PyMS.FileFormats.DAT.FlingyDAT import FlingyDAT
 from PyMS.FileFormats.DAT.SpritesDAT import SpritesDAT
 from PyMS.FileFormats.DAT.ImagesDAT import ImagesDAT
 from PyMS.FileFormats.DAT.WeaponsDAT import WeaponsDAT
 tables={};table_raw={}
 for key,cls in (('units',UnitsDAT),('flingy',FlingyDAT),('sprites',SpritesDAT),('images',ImagesDAT),('weapons',WeaponsDAT)):
  v=stock('arr\\'+key+'.dat');table_raw[key]=v;table=cls();table.load_data(v);tables[key]=table.entries
 script=stock('scripts\\iscript.bin');table_raw['orders']=stock('arr\\orders.dat');orderrows=orders.stock_orders(table_raw['orders'])
 for row in assetmanifest['images']:
  v=(a.assets/row['path'].replace('\\','/')).read_bytes();assert hashlib.sha256(v).hexdigest()==row['sha256'];assets[row['image_id']]=(row,v)
 for row in assetmanifest['lo_files']:
  v=(a.assets/row['path'].replace('\\','/')).read_bytes();assert hashlib.sha256(v).hexdigest()==row['sha256'];lo_assets.append((row,v))
 import unicorn
 from unicorn import Uc,UC_ARCH_X86,UC_MODE_32,UC_HOOK_CODE,UC_HOOK_MEM_WRITE
 from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_EBX,UC_X86_REG_ECX,UC_X86_REG_EDX,UC_X86_REG_EDI,UC_X86_REG_ESP,UC_X86_REG_ESI,UC_X86_REG_EIP,UC_X86_REG_FPCW,UC_X86_REG_MXCSR
 pe=pefile.PE(data=raw);assert pe.OPTIONAL_HEADER.ImageBase==0x400000 and pe.FILE_HEADER.Machine==0x14C
 image=pe.get_memory_mapped_image();size=(pe.OPTIONAL_HEADER.SizeOfImage+4095)&~4095;cases=[];cache={};initial_seeds={};executed_stats=[]
 code_sections=[(0x400000+s.VirtualAddress,0x400000+s.VirtualAddress+max(s.Misc_VirtualSize,s.SizeOfRawData)) for s in pe.sections if s.Characteristics&0x20000000]
 writable=[(0x400000,size),(0x800000,0x20000),(STACK,0x20000),(STOP,0x1000),(SCRIPT,0x10000),(ASSETS,0x600000),(0xB00000,0xA0000),(0xC00000,0x80000),(0x2000000,0x400000),(0x2400000,0xC00000),(0x3000000,0x100000)]
 def w(u,address,value,width=4):u.mem_write(address,(value&((1<<(8*width))-1)).to_bytes(width,'little'))
 def r(u,address,width=4):return int.from_bytes(u.mem_read(address,width),'little')
 def new(unit_count=4,sprite_count=8,image_count=32,thingy_count=8,seed=12345,rng_enabled=True):
  u=Uc(UC_ARCH_X86,UC_MODE_32);u.reg_write(UC_X86_REG_FPCW,0x37f);u.reg_write(UC_X86_REG_MXCSR,0x1f80);u.mem_map(0x400000,size);u.mem_write(0x400000,image)
  for addr,length in ((0x800000,0x20000),(STACK,0x20000),(STOP,0x1000),(SCRIPT,0x10000),(ASSETS,0x600000),(0xB00000,0xA0000),(0xC00000,0x80000),(0x2000000,0x400000),(0x2400000,0xC00000),(0x3000000,0x100000)):u.mem_map(addr,length)
  u.mem_write(SCRIPT,script);w(u,0x54330C,SCRIPT)
  at=ASSETS;allocated={}
  for ident,(row,v) in assets.items():
   if row['path'] not in allocated:u.mem_write(at,v);allocated[row['path']]=at;at+=(len(v)+4095)&~4095
   w(u,0x5436F8+ident*4,allocated[row['path']])
  for row,v in lo_assets:
   if row['path'] not in allocated:u.mem_write(at,v);allocated[row['path']]=at;at+=(len(v)+4095)&~4095
   if row['kind']<5:w(u,0x596400+(row['kind']*999+row['image_id'])*4,allocated[row['path']])
  for key,base in (('units',0x5014F0),('flingy',0x501148),('sprites',0x501408),('images',0x5011A8),('weapons',0x501828),('orders',0x501278)):
   v=table_raw[key];offset=0
   for prop_index in range({'units':54,'flingy':7,'sprites':6,'images':14,'weapons':24,'orders':19}[key]):
    destination,width,count=struct.unpack('<3I',pe.get_data(base-0x400000+prop_index*12,12));n=width*count;u.mem_write(destination,v[offset:offset+n]);offset+=n
   assert offset==len(v)
  for row in orderrows:
   w(u,0x51B200+row['id'],row['interruptible'],1);w(u,0x51B980+row['id']*2,row['highlight'],2);w(u,0x51B080+row['id'],row['subunit_inheritance'],1)
  for pool,count,head,tail,stride in ((UNIT,unit_count,0x665AF0,0x665AF4,0x150),(SPRITE,sprite_count,0x5C51C8,0x5C51CC,0x24),(IMAGE,image_count,0x544694,0x592898,0x40)):
   u.mem_write(pool,b'\0'*(max(count,1)*stride));w(u,head,pool if count else 0);w(u,tail,pool+(count-1)*stride if count else 0)
   for i in range(count):w(u,pool+i*stride,pool+(i-1)*stride if i else 0);w(u,pool+i*stride+4,pool+(i+1)*stride if i+1<count else 0)
  w(u,0x543230,4096,2);w(u,0x543234,4096,2);w(u,0x5264B4,128,2);w(u,0x5264B6,128,2)
  u.mem_write(0x543228,struct.pack('<4H',0,0,4096,4096))
  for base,count in ((0x665B08,12),(0x5C51D0,256),(0x5C55D0,256),(0x673940,256)):u.mem_write(base,b'\0'*(count*4))
  for base in (0x665AF8,0x665AFC,0x665AE0,0x665AE4,0x665B38):w(u,base,0)
  w(u,0x673930,int(rng_enabled));w(u,0x67393C,seed);w(u,0x543300,0);w(u,0x543304,0)
  for i in range(thingy_count):w(u,0x809000+i*16,0x809000+(i-1)*16 if i else 0);w(u,0x809000+i*16+4,0x809000+(i+1)*16 if i+1<thingy_count else 0)
  w(u,0x5C7A20,0x809000 if thingy_count else 0);w(u,0x5C7A24,0x809000+(thingy_count-1)*16 if thingy_count else 0);w(u,0x5C7A28,0);w(u,0x5C7A2C,0)
  w(u,0x6D3140,0)
  w(u,0x508D50,0);w(u,0x67CC90,0);w(u,0x6726D0+3*36,2,1)
  initial_seeds[id(u)]=seed
  return u
 def call(u,start,ecx,edx=0,args=(),prepared_sp=None,expected_sp=None):
  visited=set();calls=[];script_ops=[];impacts=[];initializations=[];movement_states=[];resource_branches=Counter();instructions=0;writes=0;boundary=False;recent=deque(maxlen=30);entries={lo for lo,hi in ALLOW}
  def write_hook(m,access,addr,width,value,_):
   nonlocal writes
   if any(addr<hi and addr+width>lo for lo,hi in code_sections):raise RuntimeError(f'Original code write rejected {addr:#x}/{width}')
   if not any(lo<=addr and addr+width<=lo+n for lo,n in writable):raise RuntimeError(f'Guest write outside declared memory {addr:#x}/{width}')
   writes+=1
  def hook(m,addr,width,_):
   nonlocal instructions,boundary
   if addr==STOP:boundary=True;m.emu_stop();return
   key=(addr,width)
   if key not in cache:cache[key]=tuple(lo for lo,hi in ALLOW if lo<=addr and addr+width<=hi)
   if not cache[key]:raise RuntimeError(f'Outside reviewed original code {addr:#x}; recent={list(recent)[-15:]}')
   visited.update(cache[key]);instructions+=1;recent.append(hex(addr))
   if addr in (0x49562c,0x4957c7):resource_branches[('start' if addr==0x49562c else 'candidate')+'_group_'+('accepted' if m.reg_read(UC_X86_REG_EAX) else 'rejected')]+=1
   if addr==0x49565f:resource_branches['start_height_'+('accepted' if m.reg_read(UC_X86_REG_EAX)==m.reg_read(UC_X86_REG_EDI) else 'rejected')]+=1
   if addr==0x4957ef:resource_branches['candidate_height_'+('accepted' if m.reg_read(UC_X86_REG_EAX)==m.reg_read(UC_X86_REG_ESI) else 'rejected')]+=1
   if addr==0x495695:resource_branches['start_path_'+('accepted' if m.reg_read(UC_X86_REG_EDI)<=m.reg_read(UC_X86_REG_EAX) else 'rejected')]+=1
   if addr==0x495865:resource_branches['candidate_path_'+('accepted' if m.reg_read(UC_X86_REG_ESI)<=m.reg_read(UC_X86_REG_EAX) else 'rejected')]+=1
   if addr==0x42ad50:movement_states.append(r(m,m.reg_read(UC_X86_REG_ESI)+0x97,1))
   if addr==0x4104C4:
    pc=m.reg_read(UC_X86_REG_ESI);im=m.reg_read(UC_X86_REG_EBX);script_ops.append(dict(offset=pc-SCRIPT,opcode=r(m,pc,1),image_id=r(m,im+8,2),unit_context=r(m,0x543300),flingy_context=r(m,0x543304),bullet_context=r(m,0x543308)))
   if addr==0x4090E0:
    at=m.reg_read(UC_X86_REG_ECX);impacts.append(dict(bullet=at,weapon=r(m,at+0x60,1),flags=r(m,at+0x62,1),position=[r(m,at+0x28,2),r(m,at+0x2A,2)],source=r(m,at+0x64),target=r(m,at+0x5C),unit_context=r(m,0x543300),flingy_context=r(m,0x543304),bullet_context=r(m,0x543308)))
   if addr==0x409E15:
    at=m.reg_read(UC_X86_REG_EDI);initializations.append(dict(bullet=at,weapon=r(m,at+0x60,1),spawner=m.reg_read(UC_X86_REG_ESI),heading=r(m,at+0x21,1),unit_context=r(m,0x543300),flingy_context=r(m,0x543304),bullet_context=r(m,0x543308)))
   if addr in entries:
    sp=m.reg_read(UC_X86_REG_ESP);calls.append(dict(callee=hex(addr),ecx=m.reg_read(UC_X86_REG_ECX),edx=m.reg_read(UC_X86_REG_EDX),stack=[r(m,sp+4+i*4) for i in range(6)]))
  h=u.hook_add(UC_HOOK_CODE,hook);wh=u.hook_add(UC_HOOK_MEM_WRITE,write_hook);sp=STACK+0x10000 if prepared_sp is None else prepared_sp
  if prepared_sp is None:u.mem_write(sp,struct.pack('<'+'I'*(len(args)+1),STOP,*args))
  u.reg_write(UC_X86_REG_ESP,sp);u.reg_write(UC_X86_REG_ECX,ecx);u.reg_write(UC_X86_REG_EDX,edx)
  limit=80000000 if start==0x497a20 else 8000000
  try:u.emu_start(start,STOP+1,count=limit)
  except Exception as e:raise RuntimeError(f'{e}; recent={list(recent)}; calls={calls[-10:]}') from e
  finally:u.hook_del(h);u.hook_del(wh)
  expected_sp=sp+4*(len(args)+1) if expected_sp is None else expected_sp
  if not boundary or u.reg_read(UC_X86_REG_ESP)!=expected_sp:
   raise RuntimeError(f'Incomplete original return entry={start:#x}; instructions={instructions}; EIP={u.reg_read(UC_X86_REG_EIP):#x}; boundary={boundary}; ESP={u.reg_read(UC_X86_REG_ESP):#x}; expected_ESP={expected_sp:#x}; recent={list(recent)}; calls={calls[-10:]}')
  executed_stats.append(dict(entry=hex(start),instructions=instructions,fragment=prepared_sp is not None))
  return dict(complete_return=True,return_eax=u.reg_read(UC_X86_REG_EAX),instruction_count=instructions,guarded_memory_writes=writes,calls=calls,iscript_opcodes=script_ops,bullet_initializations=initializations,impact_events=impacts,movement_state_dispatches=movement_states,resource_branches=dict(resource_branches),visited_functions=[hex(x) for x in sorted(visited)])
 def snapshot(u):
  parent=r(u,0x665B08+3*4);result=dict(owner_head=parent,unit_count=r(u,0x665B38),free_unit_head=r(u,0x665AF0),free_sprite_head=r(u,0x5C51C8),free_image_head=r(u,0x544694),hidden_head=r(u,0x665AF8),rng_state=r(u,0x67393C),pending_delete_head=r(u,0x665AE8),error=r(u,0x5DA398,2),rng_counters={str(k):r(u,0x673940+k*4) for k in (3,6,7,14,15,37)},units=[])
  for j in range(32):
   unit=UNIT+j*0x150
   if r(u,unit+0xC)==0:continue
   sp=r(u,unit+0xC);images=[];at=r(u,sp+0x1C)
   while at:
    images.append(dict(address=at,id=r(u,at+8,2),flags=r(u,at+0xC,2),direction=r(u,at+0xB,1),offset=list(struct.unpack('<2b',u.mem_read(at+0xE,2))),iscript=list(struct.unpack('<3H2B',u.mem_read(at+0x10,8))),base_frame=r(u,at+0x18,2),frame=r(u,at+0x1A,2),bounds=list(struct.unpack('<8h',u.mem_read(at+0x1C,16))),sprite=r(u,at+0x3C),grp=r(u,at+0x2C)))
    at=r(u,at+4);assert len(images)<32
   result['units'].append(dict(address=unit,type=r(u,unit+0x64,2),owner=r(u,unit+0x4C,1),status=r(u,unit+0xDC),hp=r(u,unit+8),shield=r(u,unit+0x60),energy=r(u,unit+0xA2,2),direction=r(u,unit+0x21,1),cooldowns=[r(u,unit+x,1) for x in (0x54,0x55,0x56,0x57)],movement_state=r(u,unit+0x97,1),live_counter=r(u,0x529604+(r(u,unit+0x64,2)*12+r(u,unit+0x4c,1))*4),completed_counter=r(u,0x52C0C4+(r(u,unit+0x64,2)*12+r(u,unit+0x4c,1))*4),wireframe_randomizer=r(u,unit+0xE1,1),generation=r(u,unit+0xA5,1),primary_order=r(u,unit+0x4D,1),secondary_order=r(u,unit+0xA6,1),build_time=r(u,unit+0xAC,2),queue=list(struct.unpack('<5H',u.mem_read(unit+0x98,10))),speed=r(u,unit+0x34),subunit=r(u,unit+0x70),sprite=dict(address=sp,id=r(u,sp+8,2),flags=r(u,sp+0xE,1),owner=r(u,sp+0xA,1),visibility=r(u,sp+0xC,1),main=r(u,sp+0x18),images=images)))
  return result
 def record(name,inputs,actual,expected,ex,state=None):
  assert actual==expected,(name,inputs,actual,expected)
  counters={str(k):r(u,0x673940+k*4) for k in range(256)};seed=initial_seeds[id(u)];assert r(u,0x673938)==sum(counters.values());assert r(u,0x67393C)==advance(seed,sum(counters.values()))
  ex['rng_validation']=dict(initial_seed=seed,total_calls=sum(counters.values()),source_counters=counters,final_state=r(u,0x67393C),independent_integer_LCG_matches=True)
  cases.append(dict(case=name,inputs=inputs,actual=actual,expected=expected,passed=True,execution=ex,native_state=state))
 def chain(u,head,next_offset=4):
  result=[];at=r(u,head)
  while at:result.append(at);at=r(u,at+next_offset);assert len(result)<=128
  return result
 def advance(seed,n):
  for i in range(n):seed=(seed*22695477+1)&0xFFFFFFFF
  return seed
 def joined(executions):
  return dict(complete_return=all(x['complete_return'] for x in executions),complete_call_count=len(executions),return_eax=executions[-1]['return_eax'],instruction_count=sum(x['instruction_count'] for x in executions),guarded_memory_writes=sum(x['guarded_memory_writes'] for x in executions),calls=[c for x in executions for c in x['calls']],iscript_opcodes=[c for x in executions for c in x['iscript_opcodes']],bullet_initializations=[c for x in executions for c in x['bullet_initializations']],impact_events=[c for x in executions for c in x['impact_events']],movement_state_dispatches=[c for x in executions for c in x['movement_state_dispatches']],visited_functions=sorted({c for x in executions for c in x['visited_functions']}))
 def environment(u,visibility=0):
  w(u,0x69B584,0xB00000);w(u,0xB00000,4,2);w(u,0xB44A0C,200*256);w(u,0xB44A10,200*256);w(u,0xB97A1C,0x80A000)
  for address,offset in ((0x666454,0),(0x666458,0x10000),(0x66696C,0x20000),(0x6CE650,0x40000),(0x66644C,0x50000)):w(u,address,0xC00000+offset)
  w(u,0x54323C,0xC10000);u.mem_write(0xC00000,struct.pack('<I',0x1FFFF if not visibility else 0x1FF00)*16384);w(u,0x67CF60,0xC60000)
  w(u,0x6ca7a0,0xC30000);w(u,0x6ca7ac,128);w(u,0x6ca7b0,128)
  w(u,0x6D319C,2);w(u,0x5C99B0,0);w(u,0x5D6E40,256);w(u,0x5D6E44,160)
  w(u,0x5264CC+12,8);w(u,0x672668,1);w(u,0x67266C,1)
  w(u,0x5AEF00,0x80B000);w(u,0x5AEF04,0x80B000+31*20);w(u,0x5AEF08,0)
  for i in range(32):w(u,0x80B000+i*20,0x80B000+(i-1)*20 if i else 0);w(u,0x80B000+i*20+4,0x80B000+(i+1)*20 if i<31 else 0)
  w(u,0x5DA390,2);w(u,0x5DA394,2);w(u,0x6663EC,0,1);w(u,0x6751F4,0);w(u,0x6BE650,0,1);w(u,0x665D40,0);w(u,0x665B60,0)
  return call(u,0x425610,0)
 def clear_world(u):
  normalize=environment(u,255)
  w(u,0xB00000,2,2);u.mem_write(0xB0000c,struct.pack('<H',1)*65536)
  region=0xB00000+0x449fc+64
  w(u,region,0,2);w(u,region+2,1,2);w(u,region+0xc,region+0x2c)
  pool=0x80c000;w(u,0x69b574,pool);w(u,0x69b570,pool);w(u,pool,0)
  return normalize
 def movement_snapshot(u,at):
  sprite=r(u,at+0xc);position=list(struct.unpack('<2H',u.mem_read(at+0x28,4)))
  fixed=list(struct.unpack('<2I',u.mem_read(at+0x2c,8)))
  visible=list(struct.unpack('<2H',u.mem_read(sprite+0x14,4)))
  indices=[r(u,at+k) for k in (0x13c,0x140,0x144,0x148)]
  borders=[r(u,base+8*index+4) for base,index in zip((0x5c99b8,0x5c99b8,0x5d03f8,0x5d03f8),indices)]
  assert visible==position and position==[x>>8 for x in fixed]
  typ=r(u,at+0x64,2);e=tables['units'][typ].unit_extents
  expected=[position[0]-e.left,position[0]+e.right,position[1]-e.up,position[1]+e.down]
  assert borders==expected,(typ,position,indices,borders,expected)
  for base in (0x5c99b8,0x5d03f8):
   endpoints=[r(u,base+8*i+4) for i in range(r(u,0x5c99b0))]
   assert endpoints==sorted(endpoints)
  path=r(u,at+0x100)
  return dict(position=position,fixed=fixed,sprite=visible,state=r(u,at+0x97,1),speed=r(u,at+0x38),heading=r(u,at+0x21,1),velocity_heading=r(u,at+0x23,1),flingy_flags=r(u,at+0x20,1),path=hex(path),path_timer=r(u,path+0x19,1) if path else None,index_slots=indices,borders=borders,free_path_head=hex(r(u,0x69b570)))
 def create(u,typ,x,y,owner):
  c=call(u,0x424900,typ,x,(y,owner));at=c['return_eax'];assert at
  return at,[c,call(u,0x424da0,at)]
 def set_context(u,at):
  w(u,0x543300,at);w(u,0x543304,at);w(u,0x543308,0)
 TOWNPOOL=0x690160;WORKERPOOL=0x67f7b0;BUILDINGPOOL=0x685578
 def initialized_world():
  u=new(unit_count=32,sprite_count=32,image_count=128)
  ex=[clear_world(u),call(u,0x40a0e0,0),call(u,0x486a20,0),call(u,0x4b2640,0),call(u,0x482ee0,3)]
  for player in range(8):
   regional=0x80d000+player*0x400;u.mem_write(regional,bytes(0x400));w(u,0x6b1a74+player*4,regional)
   w(u,regional+0x34,1,2);w(u,regional+0x38,player,1)
  w(u,0x6726d1+3*36,1,1)
  for player in range(12):w(u,0x534914+player*12+player,1,1)
  return u,ex
 def town_snapshot(u,town):
  def ai_list(head):
   out=[];at=r(u,head)
   while at:
    out.append(dict(address=at,next=r(u,at),prev=r(u,at+4),kind=r(u,at+8,1),unit=r(u,at+0x10),town=r(u,at+0x14)))
    assert len(out)<1024;at=r(u,at)
   return out
  return dict(address=town,next=r(u,town),prev=r(u,town+4),worker_pool=r(u,town+8),workers=ai_list(town+0xc),building_pool=r(u,town+0x10),buildings=ai_list(town+0x14),owner=r(u,town+0x18,1),initialized=r(u,town+0x19,1),worker_count=r(u,town+0x1a,1),worker_limit=r(u,town+0x1b,1),area=r(u,town+0x1c,1),needs_resources=r(u,town+0x1d,1),center=[r(u,town+0x20,2),r(u,town+0x22,2)],main=r(u,town+0x24),builder=r(u,town+0x28),resources=[r(u,town+k) for k in (0x2c,0x30,0x34,0x38)])
 def script_start(u,executions,extra=(),expected_request=None):
  executions.append(call(u,0x493330,0x6a1a18));w(u,0x6a2e6c,0x6a1a18);w(u,0x6a2e70,0)
  program=0x80f000;rectangle=0x80f100;code=[56,3,*extra,2,1,0,36];u.mem_write(program,bytes(code));w(u,0x6a2e74,program);w(u,0x6a2e78,program);u.mem_write(rectangle,struct.pack('<4I',400,400,900,800))
  executions.append(call(u,0x494d90,3,0,(rectangle,0)));ai_script=executions[-1]['return_eax'];assert ai_script
  executions.append(call(u,0x493a60,ai_script));town=r(u,ai_script+0x2c);assert town
  assert list(u.mem_read(town+0x3c,4))==([0,0,0,0] if expected_request is None else expected_request)
  return town,code
 def heap_snapshot(u,player):
  result=[]
  for i in range(r(u,player+0x210,1)):
   at=player+0x18+i*8;result.append([r(u,at,1),r(u,at+1,1),r(u,at+2,2),r(u,at+4)])
  return result
 AREA=0x6a2e88
 def area_snapshot(u,index):
  at=AREA+index*48
  return [r(u,at,2),r(u,at+2,2),r(u,at+4,1),r(u,at+5,1),r(u,at+6,1),r(u,at+7,1),r(u,at+8),r(u,at+12)]
 def resource_world():
  u,executions=initialized_world();w(u,0x6726d0+3*36,1,1)
  main,ex=create(u,106,768,512,3);executions+=ex
  resources=[]
  for typ,x,y,owner,tag,amount in ((176,896,768,11,1,750),(177,944,768,11,1,250),(188,1152,768,11,1,5000),(110,1152,1024,3,2,2000)):
   at,ex=create(u,typ,x,y,owner);executions+=ex;w(u,at+0xd0,amount,2);w(u,at+0xd8,tag,1);resources.append(at)
  w(u,0x6a5d68,2);w(u,0x6a5d6c,0)
  for i,(x,y) in enumerate(((896,512),(3072,3072)),1):
   at=AREA+i*48;u.mem_write(at,bytes(48));w(u,at,x,2);w(u,at+2,y,2);w(u,at+4,7,1);w(u,at+5,9,1);w(u,at+6,7,1);w(u,at+7,0x83,1);w(u,at+8,123);w(u,at+12,456)
  return u,executions,main,resources
 terrain_provenance=json.loads(a.terrain_report.read_text())
 region_provenance=json.loads(a.regions_report.read_text())
 assert terrain_provenance['script_sha256']==hashlib.sha256(Path(terrain_assets.__file__).read_bytes()).hexdigest()
 assert region_provenance['probe_script_sha256']==hashlib.sha256(Path(campaign_source.__file__).read_bytes()).hexdigest()
 assert region_provenance['all_expected_results_matched'] and region_provenance['all_audited_maps_included']
 graph_cases=[c for c in region_provenance['cases'] if c['name'].startswith('campaign_graph_')]
 assert len(graph_cases)==len(terrain_provenance['campaigns'])==68
 selected=[int(v) for v in a.map_indices.split(',')]
 assert selected and len(selected)==len(set(selected)) and all(0<=v<68 for v in selected)
 TERRAIN=0x2400000
 FLAGS=TERRAIN;MEGA_IDS=TERRAIN+0x40000;VF4=TERRAIN+0x80000
 MEGA_FLAGS=TERRAIN+0x180000;CV5=TERRAIN+0x1C0000
 MTXM=TERRAIN+0x200000;ALTERNATE=TERRAIN+0x240000
 def sections(blob):
  at=0
  while at<len(blob):
   assert at+8<=len(blob)
   name,length=struct.unpack_from('<4sI',blob,at);at+=8;assert at+length<=len(blob)
   yield name,blob[at:at+length];at+=length
 def load_graph(u,snapshot):
  base=0xB00000;u.mem_write(base,bytes(0xA0000));w(u,0x69b584,base)
  w(u,base,len(snapshot['regions']),2);w(u,base+2,len(snapshot['splits']),2)
  w(u,base+8,base+0x2000c+len(snapshot['splits'])*6)
  cursor=base+0x70000
  for region in snapshot['regions']:
   at=base+0x449fc+region['id']*64
   assert at+64<=base+0x70000
   for offset,value,width in ((0,region['flags'],2),(2,region['group'],2),(4,region['tiles'],2),(6,region['ground_neighbors'],1),(7,len(region['neighbors']),1),(12,cursor,4),(32,region['cut_rank'],1)):w(u,at+offset,value,width)
   u.mem_write(at+16,struct.pack('<2i',*region['center_fixed8']))
   u.mem_write(at+24,struct.pack('<4h',*region['bounds_pixels']))
   neighbors=region['neighbors']
   if neighbors:u.mem_write(cursor,struct.pack('<'+'H'*len(neighbors),*neighbors))
   cursor+=2*len(neighbors)
  assert cursor<base+0x90000
  for y,row in enumerate(snapshot['raster']):u.mem_write(base+12+y*512,struct.pack('<'+'H'*len(row),*row))
  for index,split in enumerate(snapshot['splits']):
   assert len(split)==3
   u.mem_write(base+0x2000c+index*6,struct.pack('<3H',*split))
  w(u,0xB97A1C,0x80A000)
 def load_terrain(u,width,height,tileset,mtxm):
  payloads={}
  for extension in ('cv5','vf4'):
   key='tileset\\'+tileset+'.'+extension;v=(a.terrain_data/key.replace('\\','/')).read_bytes()
   assert hashlib.sha256(v).hexdigest()==terrain_provenance['effective'][key]['sha256'];payloads[extension]=v
  assert len(payloads['vf4'])<=0x100000 and len(payloads['cv5'])<=0x40000
  for address,value,width_bytes in ((0x543230,width*32,2),(0x543234,height*32,2),(0x5264B4,width,2),(0x5264B6,height,2),(0x666458,MTXM,4),(0x666454,FLAGS,4),(0x54323C,MEGA_IDS,4),(0x66644C,VF4,4),(0x666448,len(payloads['vf4'])//32,2),(0x6CE650,CV5,4),(0x666968,len(payloads['cv5'])//52,4),(0x666450,MEGA_FLAGS,4),(0x66696C,ALTERNATE,4)):w(u,address,value,width_bytes)
  u.mem_write(0x543228,struct.pack('<4H',0,0,width*32,height*32));u.mem_write(CV5,payloads['cv5']);u.mem_write(VF4,payloads['vf4']);u.mem_write(MTXM,mtxm)
  # The original post-allocation body keeps its own stack frame and original
  # epilogue. Private memory supplies only the preceding allocation result.
  bp=STACK+0xE000;u.mem_write(bp-0x100,bytes(0x110));w(u,bp+4,STOP)
  from unicorn.x86_const import UC_X86_REG_EBP
  u.reg_write(UC_X86_REG_EBP,bp);u.reg_write(UC_X86_REG_EAX,MEGA_FLAGS)
  fragment=call(u,0x445562,0,prepared_sp=bp-0x14,expected_sp=bp+8)
  fragment['scope']='post-allocation megatile flag fragment';fragment['complete_return']=False
  vf4=list(struct.iter_unpack('<16H',payloads['vf4']))
  expected_flags=[terrain_source.generation.megatile_flags_reference(record) for record in vf4]
  assert list(struct.unpack('<'+'I'*len(vf4),u.mem_read(MEGA_FLAGS,len(vf4)*4)))==expected_flags
  terrain_import=call(u,0x445720,0)
  cv5=list(struct.iter_unpack('<26H',payloads['cv5']));flags=[];ids=[]
  values=list(struct.unpack('<'+'H'*(width*height),mtxm))
  for value in values:
   group=value>>4&0x7ff;cell=value&15;assert group<len(cv5)
   mega=cv5[group][10+cell]
   flags.append(((cv5[group][1]&0xd8fa)<<16)|expected_flags[mega&0x7fff]|(0x400000 if value&0x8000 else 0))
   ids.append(mega|(0x8000 if value&0x8000 else 0))
  assert list(struct.unpack('<'+'I'*len(flags),u.mem_read(FLAGS,len(flags)*4)))==flags
  assert list(struct.unpack('<'+'H'*len(ids),u.mem_read(MEGA_IDS,len(ids)*2)))==ids
  # Vision availability is a declared caller input, independent of the
  # authenticated original terrain bits above the low visibility bytes.
  for index,value in enumerate(flags):w(u,FLAGS+index*4,value|0xffff)
  return fragment,terrain_import,{key:hashlib.sha256(value).hexdigest() for key,value in payloads.items()}
 def original_height_reference(x,y,width,mtxm,cv5,vf4):
  tile=struct.unpack_from('<H',mtxm,((y//32)*width+x//32)*2)[0]
  mega=struct.unpack_from('<H',cv5,(tile>>4&0x7ff)*52+20+(tile&15)*2)[0]
  value=struct.unpack_from('<H',vf4,mega*32+((y//8&3)*4+(x//8&3))*2)[0]
  return 2 if value&4 else (value&255)>>1
 for ordinal in selected:
  row=terrain_provenance['campaigns'][ordinal];path=row['path'];blob=(a.data/path.replace('\\','/')).read_bytes()
  assert hashlib.sha256(blob).hexdigest()==next(x['sha256'] for x in manifest['campaign_found'] if x['path']==path)
  assert row['chk_sha256']==hashlib.sha256(blob).hexdigest()
  graph_case=graph_cases[ordinal];assert graph_case['name']=='campaign_graph_'+path
  snapshot_bytes=(a.snapshots/(str(ordinal).zfill(2)+'.json')).read_bytes()
  assert hashlib.sha256(snapshot_bytes).hexdigest()==graph_case['observed']['snapshot_sha256']
  graph=json.loads(snapshot_bytes);width,height=row['width'],row['height'];assert [graph['width'],graph['height']]==[width,height]
  blocks=list(sections(blob));version=struct.unpack('<H',next(v for k,v in blocks if k==b'VER '))[0]
  assert version in (59,205),'Unsupported source map requires a separate explicit boundary'
  mtxm=next(v for k,v in blocks if k==b'MTXM');assert len(mtxm)==width*height*2
  assert hashlib.sha256(mtxm).hexdigest()==row['mtxm_sha256']
  records=[];start_records=[]
  for section_index,(kind,payload) in enumerate(blocks):
   if kind!=b'UNIT':continue
   assert len(payload)%36==0
   for index in range(len(payload)//36):
    value=payload[index*36:(index+1)*36];typ=struct.unpack_from('<H',value,8)[0]
    if typ not in (110,149,157,176,177,178,188,214):continue
    entry=dict(type=typ,x=struct.unpack_from('<H',value,4)[0],y=struct.unpack_from('<H',value,6)[0],owner=value[16],section_index=section_index,record_index=index,sha256=hashlib.sha256(value).hexdigest(),raw=value)
    (start_records if typ==214 else records).append(entry)
  assert len(records)<1000
  u=new(unit_count=len(records)+8,sprite_count=len(records)+8,image_count=4*len(records)+32)
  executions=[clear_world(u),call(u,0x40a0e0,0),call(u,0x4a98e0,0)]
  load_graph(u,graph);fragment,terrain_import,terrain_hashes=load_terrain(u,width,height,row['tileset'],mtxm);executions.append(terrain_import)
  # Runtime controllers deliberately use human2; raw editor OWNR bytes are
  # recorded but their lobby normalization is outside this composition.
  for owner in range(12):w(u,0x6726d0+owner*36,2,1)
  u.mem_write(0x534a00,bytes(32));starts={}
  for entry in start_records:
   u.mem_write(0x800100,entry['raw']);executions.append(call(u,0x46a280,0x800100,0x534a00));assert executions[-1]['return_eax']==1
   if entry['owner']<8:starts[entry['owner']]=[entry['x'],entry['y']]
  assert all(list(struct.unpack('<2H',u.mem_read(0x534a00+owner*4,4)))==position for owner,position in starts.items())
  objects=[];properties=[];height_observations=[]
  cv5=(a.terrain_data/'tileset'/(row['tileset']+'.cv5')).read_bytes();vf4=(a.terrain_data/'tileset'/(row['tileset']+'.vf4')).read_bytes()
  for entry in records:
   ex=call(u,0x469460,entry['type'],entry['x'],(entry['y'],entry['owner']));executions.append(ex);at=ex['return_eax'];assert at
   u.mem_write(0x800100,entry['raw']);executions.append(call(u,0x46a420,at,0x80010c));objects.append(at)
   properties.append(dict(address=at,type=r(u,at+0x64,2),owner=r(u,at+0x4c,1),amount=r(u,at+0xd0,2),position=list(struct.unpack('<2H',u.mem_read(at+0x28,4))),record_sha256=entry['sha256']))
   assert properties[-1]['type']==entry['type'] and properties[-1]['owner']==entry['owner']
   assert properties[-1]['position']==[entry['x'],entry['y']]
   assert properties[-1]['amount']==struct.unpack_from('<I',entry['raw'],20)[0]&0xffff
   ex=call(u,0x445690,entry['x'],entry['y']);executions.append(ex)
   expected_height=original_height_reference(entry['x'],entry['y'],width,mtxm,cv5,vf4)
   assert ex['return_eax']==expected_height;height_observations.append(expected_height)
  candidates=[]
  for section_index,(kind,payload) in enumerate(blocks):
   if kind!=b'UNIT':continue
   for index in range(len(payload)//36):
    raw_record=payload[index*36:(index+1)*36];typ=struct.unpack_from('<H',raw_record,8)[0]
    if typ in (106,7):candidates.append(dict(type=typ,owner=raw_record[16],x=struct.unpack_from('<H',raw_record,4)[0],y=struct.unpack_from('<H',raw_record,6)[0],raw=raw_record,sha256=hashlib.sha256(raw_record).hexdigest()))
  depot=next(entry for entry in candidates if entry['type']==106)
  owner=depot['owner'];workers=[entry for entry in candidates if entry['type']==7 and entry['owner']==owner][:4]
  assert workers and owner<8
  chosen=[depot,*workers];town_objects=[]
  for entry in chosen:
   executions.append(call(u,0x469460,entry['type'],entry['x'],(entry['y'],owner)));at=executions[-1]['return_eax'];assert at;town_objects.append(at)
   u.mem_write(0x800100,entry['raw']);executions.append(call(u,0x46a420,at,0x80010c))
  main=town_objects[0]
  before=list(u.mem_read(0x6a2e88,250*48));executions.append(call(u,0x497a20,0))
  count=r(u,0x6a5d68);areas=[area_snapshot(u,i) for i in range(1,count+1)];tags=[r(u,at+0xd8,1) for at in objects]
  assert 0<=count<=249 and all(1<=tag<=count for tag in tags)
  expected_by_area={}
  for index,tag in enumerate(tags):
   values=expected_by_area.setdefault(tag,[0,0,0,0]);gas=records[index]['type'] in (110,149,157,188)
   values[int(gas)]+=1;values[2+int(gas)]+=properties[index]['amount']
  for tag,area in enumerate(areas,1):
   counts=expected_by_area.get(tag,[0,0,0,0]);assert [area[2],area[3],area[6],area[7]]==counts
  nonzero_starts=[position for owner,position in sorted(starts.items()) if any(position)]
  assert len(areas)>=len(nonzero_starts)
  assert [area[:2] for area in areas[:len(nonzero_starts)]]==nonzero_starts
  assert all(area[4:6]==[1,1] for area in areas[:len(nonzero_starts)])
  executions.extend([call(u,0x486a20,0),call(u,0x4b2640,0),call(u,0x482ee0,owner)])
  for player in range(8):
   regional=0x3000000+player*0x20000;u.mem_write(regional,bytes(len(graph['regions'])*0x34));w(u,0x6b1a74+player*4,regional)
   for region in graph['regions']:
    w(u,regional+region['id']*0x34,region['id'],2);w(u,regional+region['id']*0x34+4,player,1)
  w(u,0x6726d0+owner*36,1,1);w(u,0x6726d1+owner*36,1,1)
  executions.append(call(u,0x493330,0x6a1a18));w(u,0x6a2e6c,0x6a1a18);w(u,0x6a2e70,0)
  program=0x80f000;rectangle=0x80f100;code=[56,3,2,1,0,36];u.mem_write(program,bytes(code));w(u,0x6a2e74,program);w(u,0x6a2e78,program)
  rect=[max(0,depot['x']-200),max(0,depot['y']-200),min(width*32-1,depot['x']+200),min(height*32-1,depot['y']+400)]
  u.mem_write(rectangle,struct.pack('<4I',*rect));executions.append(call(u,0x494d90,owner,0,(rectangle,0)));script_at=executions[-1]['return_eax'];assert script_at
  executions.append(call(u,0x493a60,script_at));town=r(u,script_at+0x2c);assert town
  executions.append(call(u,0x485020,town));native_town=town_snapshot(u,town)
  assert native_town['owner']==owner and native_town['main']==main
  assert native_town['worker_count']==len(workers) and len(native_town['workers'])==len(workers)
  assert native_town['area'] in tags and native_town['needs_resources']==0
  assert native_town['resources'][0] in objects
  for ptr in native_town['resources']:
   if ptr:assert ptr in objects and r(u,ptr+0xd8,1)==native_town['area']
  assert all(r(u,entry['address']+9,1)==1 for entry in native_town['workers'])
  chosen_area=areas[native_town['area']-1];executions.append(call(u,0x486130,owner));town_mineral_sum=executions[-1]['return_eax'];assert town_mineral_sum==chosen_area[2]
  quotient=chosen_area[6]//500;expected_limit=(chosen_area[2] if quotient<town_mineral_sum else min(quotient,2*chosen_area[2]))+3*chosen_area[3]
  executions.append(call(u,0x496690,native_town['area'],owner));assert executions[-1]['return_eax']==expected_limit==native_town['worker_limit']
  town_state=dict(town=native_town,script=hex(script_at),script_bytes_fixture=code,script_rectangle_fixture=rect,actual_depot_worker_records=[{key:value for key,value in entry.items() if key!='raw'} for entry in chosen],genuine_unit_addresses=town_objects,controller_changed_as_declared_caller_input=True,runtime_race_fixture=1,x87_control_word_fixture=0x37f,mxcsr_fixture=0x1f80,unit_type_strength_initializer_executed=True,region_ai_records_fixture=True,no_host_Town_area_resource_worker_label_writes=True)
  observed=dict(area_count=count,countdown=r(u,0x6a5d6c),areas=areas,resource_tags=tags,resource_properties=properties,resource_pixel_heights=height_observations,original_generator_filter_branches=next(ex['resource_branches'] for ex in executions if any(c['callee']=='0x497a20' for c in ex['calls'])),town_binding=town_state)
  assert observed['countdown']==0 and r(u,0x5c99b0)==2*(len(objects)+len(town_objects))
  inputs=dict(map_index=ordinal,map=path,version=version,chk_sha256=hashlib.sha256(blob).hexdigest(),mtxm_sha256=hashlib.sha256(mtxm).hexdigest(),tileset=row['tileset'],terrain_sha256=terrain_hashes,graph_snapshot_sha256=hashlib.sha256(snapshot_bytes).hexdigest(),dimensions=[width,height],resource_records=[{key:value for key,value in entry.items() if key!='raw'} for entry in records],start_records=[{key:value for key,value in entry.items() if key!='raw'} for entry in start_records],runtime_controllers_fixture=2,start_positions_produced_by_complete_native_helper=True,graph_read_fields_reconstructed_from_original_snapshot=True,graph_creation_rerun=False,mission_nonresource_units_loaded=False,terrain_visibility_low_word_fixture=65535)
  record('authentic_campaign_resource_generation_and_Town_binding',inputs,observed,observed,joined(executions),dict(original_terrain_fragment=fragment,spatial_depth=r(u,0x6d3140),conservation_assertions=True,original_graph_region_count=len(graph['regions']),original_graph_split_count=len(graph['splits'])))
  cases[-1]['oracle']='independent retail amounts/positions/pixel heights and per-area count/sum conservation; native partition is an observation, not independently predicted'
  print(json.dumps(dict(map_index=ordinal,map=path,resources=len(objects),areas=count,cases=len(cases))),flush=True)
 modules={Path(module.__file__).name:hashlib.sha256(Path(module.__file__).read_bytes()).hexdigest() for name,module in sorted(sys.modules.items()) if getattr(module,'__file__',None) and Path(module.__file__).parent.resolve()==Path(__file__).parent.resolve() and name!='__main__'}
 result=dict(target_sha256=local.TARGET_HASH,probe_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),source_dependencies_sha256=modules,engine=dict(unicorn=unicorn.__version__,pefile=pefile.__version__,python=platform.python_version(),machine=platform.machine()),dat_sha256=dat_hashes,asset_manifest_sha256=hashlib.sha256(a.asset_manifest.read_bytes()).hexdigest(),terrain_report_sha256=hashlib.sha256(a.terrain_report.read_bytes()).hexdigest(),campaign_regions_report_sha256=hashlib.sha256(a.regions_report.read_bytes()).hexdigest(),case_group=a.case_group,helper_stubs=[],instruction_bound=8000000,resource_generator_instruction_bound=80000000,reviewed_code_ranges=[dict(start=hex(lo),end_exclusive=hex(hi)) for lo,hi in sorted(set(ALLOW))],complete_original_function_calls=sum(not x['fragment'] for x in executed_stats),fragment_preparation_calls=sum(x['fragment'] for x in executed_stats),total_original_instructions_executed=sum(x['instructions'] for x in executed_stats),maximum_single_call_instructions=max(x['instructions'] for x in executed_stats),original_code_executed=True,complete_game_executed=False,complete_cases=len(cases),prefix_cases=0,all_passed=all(x['passed'] for x in cases),original_graph_creation_rerun=False,whole_mission_executed=False,cases=cases)
 a.out.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({'complete_cases':len(cases),'all_passed':True}))
if __name__=='__main__':main()
