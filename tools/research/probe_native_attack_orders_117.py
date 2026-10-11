#!/usr/bin/env python3
"""Probe stock AttackUnit and timer consumers with populated original objects.

Without the explicit execution flag this prints a plan only. The guest executes
reviewed original instructions with actual callees, no stubs, Windows imports,
process startup, game loop, host files or network.
"""
import argparse
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
import inspect_native_attack_orders_117 as local
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
UNIT=0x5DA3A0;SPRITE=0x804000;IMAGE=0x805000;STACK=0x900000;STOP=0xA00000;SCRIPT=0xD00000;ASSETS=0xE00000
SELECT=('live_unit_add','live_unit_account','unit_group_upgrade_effects','speed_from_flingy','acceleration_from_flingy','turn_from_flingy','speed_status_modifier','enable_speed_upgrade','iscript_speed_control_zero_guard','upgrade_level_get')
DEATH=('drop_powerup_zero_carry','clear_unit_ai','remove_military_ai_null','remove_town_ai_null','clear_town_resource_refs','clear_guard_ai_null','clear_queen_selection')
ALLOW=[strength_source.WITNESSES['rebuild_all_computer_enemy_strength']]+[contour_source.BLOCKS[k] for k in ('unit_footprint','pixel_walkability','unit_extents','contour_fit')]+[collision_source.BLOCKS['collision_pair']]+[combat.BLOCKS[k] for k in ('has_weapon','target_rank','best_acquire','minimum_range','select_bucket_member','candidate_bucket_callback','radius_callback_iteration','cannot_chase_target','seek_range','rectangle_candidate_list')]+list(projectile_source.ALLOW)+list(local.BLOCKS.values())+[v for k,v in unitai.BLOCKS.items() if not any(t in k for t in ('branch','tail'))]
PLAN=dict(target_sha256=local.TARGET_HASH,helper_stubs=[],instruction_bound=100000,scope='Stock AttackUnit order, native live-DAT normalization, range/chase, cooldown writers and periodic spell timers',limits='Private populated pools and stock DAT/IScript/GRP/LO. Complete AttackUnit order and selected full object callbacks execute; victim status0x400, positive creep/slow-AI/FOW countdowns and empty script/resource/flingy lists are explicit fixtures. No full running world, rendering/resource loader, moving-ground chase integration or computer-AI-only order paths.')
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--exe',type=Path,required=True);p.add_argument('--data',type=Path,required=True);p.add_argument('--manifest',type=Path,required=True);p.add_argument('--assets',type=Path,required=True);p.add_argument('--asset-manifest',type=Path,required=True);p.add_argument('--pyms',type=Path,required=True);p.add_argument('--out',type=Path);p.add_argument('--case-group',choices=('all','attack','numeric','timers','chase','frame','targets'),default='all');p.add_argument('--execute-original-code',action='store_true');a=p.parse_args()
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
 from unicorn import Uc,UC_ARCH_X86,UC_MODE_32,UC_HOOK_CODE
 from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_EBX,UC_X86_REG_ECX,UC_X86_REG_EDX,UC_X86_REG_EDI,UC_X86_REG_ESP,UC_X86_REG_ESI
 pe=pefile.PE(data=raw);assert pe.OPTIONAL_HEADER.ImageBase==0x400000 and pe.FILE_HEADER.Machine==0x14C
 image=pe.get_memory_mapped_image();size=(pe.OPTIONAL_HEADER.SizeOfImage+4095)&~4095;cases=[];cache={};initial_seeds={}
 def w(u,address,value,width=4):u.mem_write(address,(value&((1<<(8*width))-1)).to_bytes(width,'little'))
 def r(u,address,width=4):return int.from_bytes(u.mem_read(address,width),'little')
 def new(unit_count=4,sprite_count=8,image_count=32,thingy_count=8,seed=12345,rng_enabled=True):
  u=Uc(UC_ARCH_X86,UC_MODE_32);u.mem_map(0x400000,size);u.mem_write(0x400000,image)
  for addr,length in ((0x800000,0x20000),(STACK,0x4000),(STOP,0x1000),(SCRIPT,0x10000),(ASSETS,0x600000),(0xB00000,0xA0000),(0xC00000,0x80000)):u.mem_map(addr,length)
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
  for base,count in ((0x665B08,12),(0x5C51D0,256),(0x5C55D0,256),(0x673940,50)):u.mem_write(base,b'\0'*(count*4))
  for base in (0x665AF8,0x665AFC,0x665AE0,0x665AE4,0x665B38):w(u,base,0)
  w(u,0x673930,int(rng_enabled));w(u,0x67393C,seed);w(u,0x543300,0);w(u,0x543304,0)
  for i in range(thingy_count):w(u,0x809000+i*16,0x809000+(i-1)*16 if i else 0);w(u,0x809000+i*16+4,0x809000+(i+1)*16 if i+1<thingy_count else 0)
  w(u,0x5C7A20,0x809000 if thingy_count else 0);w(u,0x5C7A24,0x809000+(thingy_count-1)*16 if thingy_count else 0);w(u,0x5C7A28,0);w(u,0x5C7A2C,0)
  w(u,0x6D3140,0)
  w(u,0x508D50,0);w(u,0x67CC90,0);w(u,0x6726D0+3*36,2,1)
  initial_seeds[id(u)]=seed
  return u
 def call(u,start,ecx,edx=0,args=()):
  visited=set();calls=[];script_ops=[];impacts=[];initializations=[];instructions=0;boundary=False;recent=[];entries={lo for lo,hi in ALLOW}
  def hook(m,addr,width,_):
   nonlocal instructions,boundary
   if addr==STOP:boundary=True;m.emu_stop();return
   key=(addr,width)
   if key not in cache:cache[key]=tuple(lo for lo,hi in ALLOW if lo<=addr and addr+width<=hi)
   if not cache[key]:raise RuntimeError(f'Outside reviewed original code {addr:#x}; recent={recent[-15:]}')
   visited.update(cache[key]);instructions+=1;recent.append(hex(addr))
   if addr==0x4104C4:
    pc=m.reg_read(UC_X86_REG_ESI);im=m.reg_read(UC_X86_REG_EBX);script_ops.append(dict(offset=pc-SCRIPT,opcode=r(m,pc,1),image_id=r(m,im+8,2),unit_context=r(m,0x543300),flingy_context=r(m,0x543304),bullet_context=r(m,0x543308)))
   if addr==0x4090E0:
    at=m.reg_read(UC_X86_REG_ECX);impacts.append(dict(bullet=at,weapon=r(m,at+0x60,1),flags=r(m,at+0x62,1),position=[r(m,at+0x28,2),r(m,at+0x2A,2)],source=r(m,at+0x64),target=r(m,at+0x5C),unit_context=r(m,0x543300),flingy_context=r(m,0x543304),bullet_context=r(m,0x543308)))
   if addr==0x409E15:
    at=m.reg_read(UC_X86_REG_EDI);initializations.append(dict(bullet=at,weapon=r(m,at+0x60,1),spawner=m.reg_read(UC_X86_REG_ESI),heading=r(m,at+0x21,1),unit_context=r(m,0x543300),flingy_context=r(m,0x543304),bullet_context=r(m,0x543308)))
   if addr in entries:
    sp=m.reg_read(UC_X86_REG_ESP);calls.append(dict(callee=hex(addr),ecx=m.reg_read(UC_X86_REG_ECX),edx=m.reg_read(UC_X86_REG_EDX),stack=[r(m,sp+4+i*4) for i in range(6)]))
  h=u.hook_add(UC_HOOK_CODE,hook);sp=STACK+0x3000;u.mem_write(sp,struct.pack('<'+'I'*(len(args)+1),STOP,*args));u.reg_write(UC_X86_REG_ESP,sp);u.reg_write(UC_X86_REG_ECX,ecx);u.reg_write(UC_X86_REG_EDX,edx)
  try:u.emu_start(start,STOP+1,count=100000)
  except Exception as e:raise RuntimeError(f'{e}; recent={recent[-30:]}; calls={calls[-10:]}') from e
  finally:u.hook_del(h)
  assert boundary and u.reg_read(UC_X86_REG_ESP)==sp+4*(len(args)+1)
  return dict(complete_return=True,return_eax=u.reg_read(UC_X86_REG_EAX),instruction_count=instructions,calls=calls,iscript_opcodes=script_ops,bullet_initializations=initializations,impact_events=impacts,visited_functions=[hex(x) for x in sorted(visited)])
 def snapshot(u):
  parent=r(u,0x665B08+3*4);result=dict(owner_head=parent,unit_count=r(u,0x665B38),free_unit_head=r(u,0x665AF0),free_sprite_head=r(u,0x5C51C8),free_image_head=r(u,0x544694),hidden_head=r(u,0x665AF8),rng_state=r(u,0x67393C),pending_delete_head=r(u,0x665AE8),error=r(u,0x5DA398,2),rng_counters={str(k):r(u,0x673940+k*4) for k in (3,6,7,14,15,37)},units=[])
  for j in range(4):
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
  counters={str(k):r(u,0x673940+k*4) for k in range(50)};seed=initial_seeds[id(u)];assert r(u,0x67393C)==advance(seed,sum(counters.values()))
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
  return dict(complete_return=all(x['complete_return'] for x in executions),complete_call_count=len(executions),return_eax=executions[-1]['return_eax'],instruction_count=sum(x['instruction_count'] for x in executions),calls=[c for x in executions for c in x['calls']],iscript_opcodes=[c for x in executions for c in x['iscript_opcodes']],bullet_initializations=[c for x in executions for c in x['bullet_initializations']],impact_events=[c for x in executions for c in x['impact_events']],visited_functions=sorted({c for x in executions for c in x['visited_functions']}))
 def environment(u,visibility=0):
  w(u,0x69B584,0xB00000);w(u,0xB00000,4,2);w(u,0xB44A0C,200*256);w(u,0xB44A10,200*256);w(u,0xB97A1C,0x80A000)
  for address,offset in ((0x666454,0),(0x666458,0x10000),(0x66696C,0x20000),(0x6CE650,0x40000),(0x66644C,0x50000)):w(u,address,0xC00000+offset)
  w(u,0x54323C,0xC10000);u.mem_write(0xC00000,struct.pack('<I',0x1FFFF if not visibility else 0x1FF00)*16384);w(u,0x67CF60,0xC60000)
  w(u,0x6D319C,2);w(u,0x5C99B0,0);w(u,0x5D6E40,256);w(u,0x5D6E44,160)
  w(u,0x5264CC+12,8);w(u,0x672668,1);w(u,0x67266C,1)
  w(u,0x5AEF00,0x80B000);w(u,0x5AEF04,0x80B000+31*20);w(u,0x5AEF08,0)
  for i in range(32):w(u,0x80B000+i*20,0x80B000+(i-1)*20 if i else 0);w(u,0x80B000+i*20+4,0x80B000+(i+1)*20 if i<31 else 0)
  w(u,0x5DA390,2);w(u,0x5DA394,2);w(u,0x6663EC,0,1);w(u,0x6751F4,0);w(u,0x6BE650,0,1);w(u,0x665D40,0);w(u,0x665B60,0)
  return call(u,0x425610,0)
 def setup(u,typ=0,victim_type=0,positions=((200,200),(280,200))):
  normalization=environment(u,255);executions=[normalization,call(u,0x40A0E0,0)]
  for at,t,(x,y) in ((UNIT,typ,positions[0]),(UNIT+0x150,victim_type,positions[1])):
   created=call(u,0x424900,t,x,(y,3));assert created['return_eax']==at
   executions.extend((created,call(u,0x424DA0,at)))
  w(u,UNIT+0x5C,UNIT+0x150);w(u,UNIT+0x150+0x7C,UNIT)
  w(u,0x543300,UNIT);w(u,0x543304,UNIT);w(u,0x543308,0)
  return executions
 def setup_enemy(u,typ=0,victim_type=0,positions=((200,200),(280,200))):
  normalization=environment(u,255);w(u,0x6726d0+4*36,2,1);executions=[normalization,call(u,0x40a0e0,0)]
  for at,t,(x,y),owner in ((UNIT,typ,positions[0],3),(UNIT+0x150,victim_type,positions[1],4)):
   created=call(u,0x424900,t,x,(y,owner));assert created['return_eax']==at
   executions.extend((created,call(u,0x424da0,at)))
  w(u,UNIT+0x5c,UNIT+0x150);w(u,UNIT+0x150+0x7c,UNIT)
  w(u,0x543300,UNIT);w(u,0x543304,UNIT);w(u,0x543308,0)
  return executions
 def bullets(u):
  out=[]
  for at in chain(u,0x540750):
   sp=r(u,at+0xC);out.append(dict(address=at,weapon=r(u,at+0x60,1),state=r(u,at+0x4D,1),remaining=r(u,at+0x61,1),flags=r(u,at+0x62,1),signal=r(u,at+0x4F,1),bounces=r(u,at+0x63,1),source=r(u,at+0x64),target=r(u,at+0x5C),position=[r(u,at+0x28,2),r(u,at+0x2A,2)],fixed_position=[r(u,at+0x2C),r(u,at+0x30)],speed=r(u,at+0x38),direction=r(u,at+0x21,1),velocity_direction=r(u,at+0x4A,1),sprite=sp,images=[] if not sp else [dict(address=im,id=r(u,im+8,2),flags=r(u,im+0xC,2),pc=r(u,im+0x12,2),wait=r(u,im+0x17,1),animation=r(u,im+0x16,1)) for im in chain(u,sp+0x1C)]))
  return out
 def image_and_bullet_step(u,executions,update_attacker=True):
  if update_attacker:
   w(u,0x543300,UNIT);w(u,0x543304,UNIT);w(u,0x543308,0);executions.append(call(u,0x418B20,UNIT))
  w(u,0x543300,0);executions.append(call(u,0x40A5F0,0));executions.append(call(u,0x418990,0))
  assert r(u,0x543300)==r(u,0x543304)==r(u,0x543308)==0
 def trace_row(u,tick):
  return dict(tick=tick,victim_hp=r(u,UNIT+0x150+8),bullet_count=r(u,0x53DB8C),bullets=bullets(u),things=chain(u,0x5C7A28),free_bullets=len(chain(u,0x540758)),free_images=len(chain(u,0x544694)),free_sprites=len(chain(u,0x5C51C8)))
 if a.case_group in ('all','attack'):
  for typ,vt in ((0,0),(8,0),(8,8)):
   u=new();executions=setup(u,typ,vt);victim=UNIT+0x150
   executions.append(call(u,0x42a380,UNIT,args=(10,victim)))
   w(u,UNIT+0x21,64,1);w(u,UNIT+0x23,64,1);executions.append(call(u,0x417190,r(u,UNIT+0xc),args=(64,)))
   before_rng=r(u,0x67393c);weapon_id=0 if typ==0 else (16 if vt==0 else 15)
   executions.append(call(u,0x41e5c0,UNIT))
   timeline=[]
   for tick in range(20):image_and_bullet_step(u,executions);timeline.append(trace_row(u,tick+1))
   cooldown=tables['weapons'][weapon_id].weapon_cooldown+(((advance(before_rng,1)>>16)&3)-1)
   record('attack_unit_'+str(typ)+'_'+str(vt),dict(attacker_type=typ,target_type=vt,heading=64,steps=20,native_issue=True,range_state=0),[r(u,UNIT+0x4e,1),r(u,UNIT+0x55,1),r(u,UNIT+0x56,1),r(u,victim+8)],[1,cooldown,cooldown,(tables['units'][vt].hit_points.whole-tables['weapons'][weapon_id].damage_amount)*256],joined(executions),timeline)
  for invalid in ('null','invulnerable','disabled_source'):
   u=new();executions=setup(u);victim=UNIT+0x150
   executions.append(call(u,0x42a380,UNIT,args=(10,0 if invalid=='null' else victim)))
   if invalid=='invulnerable':w(u,victim+0xdc,r(u,victim+0xdc)|0x4000000)
   if invalid=='disabled_source':w(u,UNIT+0xdc,r(u,UNIT+0xdc)|0x400)
   ex=call(u,0x41e5c0,UNIT)
   record('attack_invalid_target_'+invalid,dict(stock_units=[0,0],native_attack_issue=True,invalid_fixture=invalid,can_switch_target=False,unit_ai=None),[r(u,UNIT+0x4d,1),r(u,UNIT+0x55,1),r(u,UNIT+0x56,1)],[2 if invalid=='null' else 6,0,0],joined(executions+[ex]),snapshot(u))
 if a.case_group in ('all','numeric'):
  u=new();ex=call(u,0x425610,0);live=[x.target_acquisition_range for x in tables['units']]
  for i,row in enumerate(tables['units']):
   attacker=i if row.subunit1==228 else row.subunit1;attackrow=tables['units'][attacker];value=live[attacker]
   for weapon in (attackrow.ground_weapon,attackrow.air_weapon):
    if weapon<130:value=max(value,tables['weapons'][weapon].maximum_range>>5)
   live[i]=value&255
  record('native_stock_acquisition_normalization_228_rows',dict(stock_rows=228,subunit_sentinel=228,weapon_sentinel=130,ascending_in_place=True),list(u.mem_read(0x5219f0,228)),live,ex)
  for inputs in (([9,2],[1,0]),([9,2],[228,0]),([9,2],[1,228])):
   u=new();values,refs=inputs
   for i in range(2):
    w(u,0x5219f0+i,values[i],1);w(u,0x51fb90+i*2,refs[i],2);w(u,0x5211d8+i,130,1);w(u,0x5213a8+i,130,1)
   ex=call(u,0x425610,0);expected=values[:]
   for i in range(2):expected[i]=expected[i if refs[i]==228 else refs[i]]
   record('acquisition_in_place_refs_'+str(refs),dict(synthetic_rows=[0,1],initial_acquisition=values,subunit_refs=refs,weapons_none=130),list(u.mem_read(0x5219f0,2)),expected,ex)
  for weapon_id in (0,15,16):
   for spores in (0,1,3,9):
    for stim,ensnare,upgrade in ((0,0,False),(1,0,False),(0,1,False),(1,1,False),(0,0,True),(0,1,True),(1,1,True)):
     u=new();executions=setup(u,0 if weapon_id==0 else 8,8 if weapon_id==15 else 0)
     w(u,UNIT+0x126,spores,1);w(u,UNIT+0x115,stim,1);w(u,UNIT+0x116,ensnare,1)
     if upgrade:w(u,UNIT+0xdc,r(u,UNIT+0xdc)|0x20000000)
     base=tables['weapons'][weapon_id].weapon_cooldown
     expected=base+spores*max(3,base//8);modifier=int(bool(stim))+int(upgrade)-int(bool(ensnare))
     if modifier<0:expected+=expected//4
     elif modifier>0:expected//=2
     expected=max(5,min(250,expected));ex=call(u,0x41fb70,UNIT,args=(weapon_id,))
     record(f'cooldown_weapon_{weapon_id}_spores_{spores}_stim_{stim}_ensnare_{ensnare}_upgrade_{upgrade}',dict(stock_weapon=weapon_id,spores_fixture=spores,stim_timer_fixture=stim,ensnare_timer_fixture=ensnare,weapon_speed_status_bit20000000_fixture=upgrade),ex['return_eax'],expected,joined(executions+[ex]))
  for base in (0,1,4,5,6,249,250,251,255):
   for spores in (0,9,255):
    u=new();executions=setup(u);w(u,0x525e90,base,1);w(u,UNIT+0x126,spores,1)
    ex=call(u,0x41fb70,UNIT,args=(0,));record(f'cooldown_clamp_base_{base}_spores_{spores}',dict(stock_unit=0,synthetic_weapon0_base_cooldown=base,spores_fixture=spores),ex['return_eax'],max(5,min(250,base+spores*max(3,base//8))),joined(executions+[ex]))
  for selector in (1,2):
   for cooldown in (1,2,15,255):
    for wait in (0,1,8,255):
     u=new();executions=setup(u,8);w(u,UNIT+0x54+selector,cooldown,1);w(u,UNIT+0x85,wait,1)
     rng=r(u,0x67393c);ex=call(u,0x41f530,UNIT,args=(selector,16 if selector==1 else 15,5 if selector==1 else 6))
     record(f'blocked_cooldown_{selector}_{cooldown}_wait_{wait}',dict(selector=selector,cooldown_fixture=cooldown,order_wait_fixture=wait),[ex['return_eax'],r(u,UNIT+0x85,1),r(u,UNIT+0x54+selector,1),r(u,0x67393c)],[0,min(wait,cooldown-1),cooldown,rng],joined(executions+[ex]))
  print('cooldown numeric completed',flush=True)
  for typ,vt,creation_draws,weapon_id in ((0,0,8,0),(8,0,9,16),(8,8,10,15)):
   for residue in range(4):
    seed=next(x for x in range(1000) if ((advance(x,creation_draws+1)>>16)&3)==residue)
    u=new(seed=seed);executions=setup(u,typ,vt);victim=UNIT+0x150
    assert sum(r(u,0x673940+k*4) for k in range(50))==creation_draws
    executions.append(call(u,0x42a380,UNIT,args=(10,victim)));w(u,UNIT+0x21,64,1);w(u,UNIT+0x23,64,1);executions.append(call(u,0x417190,r(u,UNIT+0xc),args=(64,)))
    ex=call(u,0x41e5c0,UNIT);cooldown=tables['weapons'][weapon_id].weapon_cooldown+residue-1
    record(f'attack_cooldown_jitter_{typ}_{vt}_{residue}',dict(stock_unit=typ,stock_target=vt,initial_seed=seed,creation_random_calls=creation_draws,next_source12_draw_low2=residue),[r(u,UNIT+0x55,1),r(u,UNIT+0x56,1),r(u,0x673940+12*4)],[cooldown,cooldown,1],joined(executions+[ex]),snapshot(u))
  for heading in (0,47,48,64,80,81,128,200,255):
   u=new();executions=setup(u);w(u,UNIT+0x21,heading,1);executions.append(call(u,0x40cbd0,UNIT,args=(280,200)))
   ex=call(u,0x41ff50,UNIT,args=(0,));difference=min((heading-64)&255,(64-heading)&255)
   record(f'ready_weapon_angle_{heading}',dict(stock_marine=True,target_distance=80,heading_fixture=heading,waypoint_native_target=[280,200],weapon_attack_angle=16),ex['return_eax'],int(difference<=16),joined(executions+[ex]))
  for distance in (16,17,18,26,27,28,145,146):
   u=new();executions=setup(u,positions=((200,200),(200+distance,200)));w(u,0x5252b0,10)
   ex1=call(u,0x41ff50,UNIT,args=(0,));ex2=call(u,0x421060,UNIT,args=(UNIT+0x150,));gap=max(0,distance-17)
   record(f'weapon_minimum_range_boundary_{distance}',dict(stock_marine_extents=[8,9,8,10],distance=distance,synthetic_weapon0_minimum_range=10,stock_maximum_range=128,waypoint_equals_source_position=True),[ex1['return_eax'],ex2['return_eax']],[int(10<=gap<=128),int(10<gap<=128)],joined(executions+[ex1,ex2]))
 if a.case_group in ('all','timers'):
  for complete,hidden in ((True,False),(False,False),(False,True)):
   for phase in range(8):
    u=new();executions=setup(u);w(u,UNIT+0x122,phase,1);w(u,UNIT+0x11b,2,1);w(u,UNIT+0x115,2,1);w(u,UNIT+0x116,2,1);w(u,UNIT+0x117,2,1);w(u,UNIT+0x119,2,1);w(u,UNIT+0x124,2,1)
    w(u,UNIT+0xdc,(r(u,UNIT+0xdc)|1) if complete else (r(u,UNIT+0xdc)&~1))
    if hidden:w(u,r(u,UNIT+0xc)+0xe,r(u,r(u,UNIT+0xc)+0xe,1)|0x20,1)
    for x in (0x138,0x13a):w(u,UNIT+x,0xabcd,2)
    ex=call(u,0x43a320,UNIT);advance_phase=complete or not hidden;fires=advance_phase and phase==7
    actual=[r(u,UNIT+0x122,1)]+[r(u,UNIT+x,1) for x in (0x11b,0x115,0x116,0x117,0x119,0x124)]+[r(u,UNIT+0x138,2),r(u,UNIT+0x13a,2)]
    expected=[(phase+1)%8 if advance_phase else phase]+[1 if fires else 2]*6+[0xabcd]*2
    record(f'spell_timer_cadence_completed_{complete}_hidden_{hidden}_phase_{phase}',dict(stock_unit=0,completed_status_fixture=complete,sprite_hidden_status_fixture=hidden,phase_fixture=phase,positive_status_timers=2,strength_cache_sentinel=0xabcd),actual,expected,joined(executions+[ex]),dict(periodic_spell_calls=sum(c['callee']=='0x4351e0' for c in ex['calls'])))
  for typ in (0,8):
   for effect in ('stim','ensnare','both'):
    u=new();executions=setup(u,typ);w(u,UNIT+0x122,7,1);w(u,UNIT+0x115,int(effect in ('stim','both')),1);w(u,UNIT+0x116,int(effect in ('ensnare','both')),1)
    ex=call(u,0x43a320,UNIT)
    record(f'spell_expiry_{typ}_{effect}',dict(stock_unit=typ,phase_fixture=7,stim_timer_fixture=int(effect in ('stim','both')),ensnare_timer_fixture=int(effect in ('ensnare','both')),existing_ensnare_overlay=False),[r(u,UNIT+0x122,1),r(u,UNIT+0x115,1),r(u,UNIT+0x116,1)],[0,0,0],joined(executions+[ex]),snapshot(u))
  for phase in (0,7):
   u=new();executions=setup(u,65);w(u,UNIT+0x60,60*256-10);w(u,UNIT+0x122,phase,1)
   for offset in (0x138,0x13a):w(u,UNIT+offset,0xabcd,2)
   ex=call(u,0x43a320,UNIT)
   record(f'stock_shield_regeneration_does_not_refresh_cache_phase_{phase}',dict(stock_unit=65,current_shields_fp8=60*256-10,master_spell_phase_fixture=phase,strength_cache_sentinel=0xabcd),[r(u,UNIT+0x60),r(u,UNIT+0x138,2),r(u,UNIT+0x13a,2),sum(c['callee']=='0x4a8f30' for c in ex['calls'])],[60*256-3,0xabcd,0xabcd,0],joined(executions+[ex]))
  for typ in (0,8):
   u=new();executions=setup(u,typ)
   w(u,0x6ac530+typ*4,100);w(u,0x6ac530+(228+typ)*4,80)
   ex=call(u,0x430a00,UNIT)
   record(f'hallucination_conversion_strength_refresh_{typ}',dict(stock_unit=typ,synthetic_type_strength_air=100,synthetic_type_strength_ground=80,initial_hp_full=True,existing_timed_life=0),[r(u,UNIT+0xdc)&0x40000000,r(u,UNIT+0x110,2),r(u,UNIT+0xa6,1),r(u,UNIT+0x138,2),r(u,UNIT+0x13a,2)],[0x40000000,1350,149,100,80],joined(executions+[ex]),snapshot(u))
  print('positive periodic spell timers completed',flush=True)
 if a.case_group in ('all','chase'):
  for typ,vt in ((0,0),(8,0),(8,8)):
   for order_state in (0,1):
    for distance in (80,500):
     u=new();executions=setup(u,typ,vt,positions=((200,200),(200+distance,200)));victim=UNIT+0x150
     executions.append(call(u,0x42a380,UNIT,args=(10,victim)));w(u,UNIT+0x4e,order_state,1)
     w(u,UNIT+0x21,64,1);w(u,UNIT+0x23,64,1);executions.append(call(u,0x417190,r(u,UNIT+0xc),args=(64,)))
     ex=call(u,0x41e5c0,UNIT);executions.append(ex)
     moving=distance==500
     actual=[r(u,UNIT+0x4e,1),r(u,UNIT+0x10,2),r(u,UNIT+0x12,2),r(u,UNIT+0x14),r(u,UNIT+0xe3,1)]
     expected=[1,700 if moving else 200,200,victim if moving else 0,15 if moving or order_state else 0]
     record(f'attack_range_chase_{typ}_{vt}_{order_state}_{distance}',dict(stock_attacker=typ,stock_target=vt,initial_order_state_fixture=order_state,distance=distance,heading_fixture=64),actual,expected,joined(executions),snapshot(u))
 if a.case_group in ('all','targets'):
  for initial in ('none','armed','civilian'):
   u=new();executions=setup_enemy(u,0,15 if initial=='civilian' else 0);victim=UNIT+0x150
   third=UNIT+0x2a0
   created=call(u,0x424900,0,240,(200,4));assert created['return_eax']==third;executions.extend((created,call(u,0x424da0,third)))
   w(u,0x534914+3*12+3,1,1);w(u,0x534914+4*12+4,1,1)
   executions.append(call(u,0x42a380,UNIT,args=(10,victim)));w(u,UNIT+0xdc,r(u,UNIT+0xdc)|0x8000000);w(u,UNIT+0x5c,0 if initial=='none' else victim)
   w(u,UNIT+0x21,64,1);w(u,UNIT+0x23,64,1);executions.append(call(u,0x417190,r(u,UNIT+0xc),args=(64,)))
   before_seed=r(u,0x67393c);ex=call(u,0x41e5c0,UNIT);executions.append(ex)
   selected=r(u,UNIT+0x5c);expected_target=victim if initial=='armed' else third
   for tick in range(8):image_and_bullet_step(u,executions)
   initial_hp=tables['units'][0].hit_points.whole*256
   actual=[selected,r(u,expected_target+8),r(u,UNIT+0x4d,1),r(u,0x5c99b0)]
   record(f'autotarget_attack_stock_{initial}',dict(stock_units=[0,15 if initial=='civilian' else 0,0],owners=[3,4,4],self_alliance_fixture=1,cross_alliance_fixture=0,can_switch_target_status_fixture=True,initial_target=initial,positions=[[200,200],[280,200],[240,200]],steps=8),actual,[expected_target,initial_hp-6*256,10,6],joined(executions),snapshot(u))
 if a.case_group in ('all','frame'):
  for typ,vt in ((0,0),(8,0),(8,8)):
   u=new();executions=setup(u,typ,vt);victim=UNIT+0x150
   executions.append(call(u,0x42a380,UNIT,args=(10,victim)))
   w(u,UNIT+0x21,64,1);w(u,UNIT+0x23,64,1);executions.append(call(u,0x417190,r(u,UNIT+0xc),args=(64,)))
   w(u,victim+0xdc,r(u,victim+0xdc)|0x400);w(u,0x6c46d4,200);w(u,0x69b568,200);w(u,0x69b564,200);w(u,0x6663e4,200);w(u,0x6a2e70,0);w(u,0x6a5d68,0);w(u,0x6b1a94,0);w(u,0x6b1a98,1);w(u,0x5432f0,0)
   timeline=[]
   for tick in range(48):
    executions.append(call(u,0x445070,0));row=trace_row(u,tick+1);row.update(order_state=r(u,UNIT+0x4e,1),order_wait=r(u,UNIT+0x85,1),cooldowns=[r(u,UNIT+0x55,1),r(u,UNIT+0x56,1)],movement=r(u,UNIT+0x97,1));timeline.append(row)
   record(f'object_step_attack_{typ}_{vt}',dict(stock_units=[typ,vt],object_steps=48,victim_status_400_fixture=True,heading_fixture=64,creep_and_slow_ai_and_fow_countdowns=200,empty_ai_script_resource_flingy_lists=True),[r(u,0x6c46d4),r(u,0x673930),r(u,UNIT+0x4d,1),r(u,victim+8)],[152,0,10,(16 if typ==0 else 24 if vt==0 else 80)*256],joined(executions),timeline)
 modules={Path(module.__file__).name:hashlib.sha256(Path(module.__file__).read_bytes()).hexdigest() for name,module in sorted(sys.modules.items()) if getattr(module,'__file__',None) and Path(module.__file__).parent.resolve()==Path(__file__).parent.resolve() and name!='__main__'}
 result=dict(target_sha256=local.TARGET_HASH,probe_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),source_dependencies_sha256=modules,engine=dict(unicorn=unicorn.__version__,pefile=pefile.__version__,machine=platform.machine()),dat_sha256=dat_hashes,stock_weapon_rows=[dict(id=i,weapon=vars(tables['weapons'][i]),flingy=vars(tables['flingy'][tables['weapons'][i].graphics]),sprite=vars(tables['sprites'][tables['flingy'][tables['weapons'][i].graphics].sprite])) for i in (0,8,15,16)],asset_manifest_sha256=hashlib.sha256(a.asset_manifest.read_bytes()).hexdigest(),case_group=a.case_group,helper_stubs=[],instruction_bound=100000,reviewed_code_ranges=[dict(start=hex(lo),end_exclusive=hex(hi)) for lo,hi in sorted(set(ALLOW))],complete_cases=len(cases),prefix_cases=0,all_passed=all(x['passed'] for x in cases),cases=cases)
 a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(dict(cases=len(cases),all_passed=result['all_passed'])))
if __name__=='__main__':main()
