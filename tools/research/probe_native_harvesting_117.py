#!/usr/bin/env python3
"""Probe original harvesting and populated cargo services.

Without the explicit execution flag this prints a plan only. The guest executes
reviewed original instructions with actual callees, no stubs, Windows imports,
process startup, outer accepted-frame orchestration, host files or network.
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
import probe_native_attack_orders_117 as attack_source
import probe_native_path_progress_117 as paths
import probe_native_blocked_movement_117 as blocked_source
import inspect_native_harvesting_117 as local
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
UNIT=0x5DA3A0;SPRITE=0x804000;IMAGE=0x805000;STACK=0x900000;STOP=0xA00000;SCRIPT=0xD00000;ASSETS=0xE00000
SELECT=('live_unit_add','live_unit_account','unit_group_upgrade_effects','speed_from_flingy','acceleration_from_flingy','turn_from_flingy','speed_status_modifier','enable_speed_upgrade','iscript_speed_control_zero_guard','upgrade_level_get')
DEATH=('drop_powerup_zero_carry','clear_unit_ai','remove_military_ai_null','remove_town_ai_null','clear_town_resource_refs','clear_guard_ai_null','clear_queen_selection')
ALLOW=list(blocked_source.ALLOW)+list(local.BLOCKS.values())+[creep_source.BLOCKS[k] for k in ('creep_spread_one','creep_bounds','creep_tile_physical_blocked','creep_tile_occupied','creep_neighbor_count','creep_tile_flags_mask')]
PLAN=dict(target_sha256=local.TARGET_HASH,helper_stubs=[],instruction_bound=100000,scope='Original populated cargo, resource queues, computer worker bonus, Hatchery side weights and three whole-object mineral harvest/return chains',limits='Private explicit world/resource amount/pool inputs. No full accepted game frame or original loader.')
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--exe',type=Path,required=True);p.add_argument('--data',type=Path,required=True);p.add_argument('--manifest',type=Path,required=True);p.add_argument('--assets',type=Path,required=True);p.add_argument('--asset-manifest',type=Path,required=True);p.add_argument('--pyms',type=Path,required=True);p.add_argument('--out',type=Path);p.add_argument('--case-group',choices=('all','cargo','queue','bonus','cycle','hatchery'),default='all');p.add_argument('--cycle-worker',type=int,choices=(7,41,64));p.add_argument('--execute-original-code',action='store_true');a=p.parse_args()
 raw=a.exe.read_bytes();assert hashlib.sha256(raw).hexdigest()==local.TARGET_HASH
 if not a.execute_original_code:print(json.dumps(PLAN,indent=2));return
 assert a.out and platform.machine().lower() in ('x86_64','amd64')
 assert a.cycle_worker is None or a.case_group=='cycle'
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
 image=pe.get_memory_mapped_image();size=(pe.OPTIONAL_HEADER.SizeOfImage+4095)&~4095;cases=[];cache={};initial_seeds={};executed_stats=[]
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
  u.mem_write(0x543228,struct.pack('<4H',0,0,4096,4096))
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
  visited=set();calls=[];script_ops=[];impacts=[];initializations=[];movement_states=[];instructions=0;boundary=False;recent=[];entries={lo for lo,hi in ALLOW}
  def hook(m,addr,width,_):
   nonlocal instructions,boundary
   if addr==STOP:boundary=True;m.emu_stop();return
   key=(addr,width)
   if key not in cache:cache[key]=tuple(lo for lo,hi in ALLOW if lo<=addr and addr+width<=hi)
   if not cache[key]:raise RuntimeError(f'Outside reviewed original code {addr:#x}; recent={recent[-15:]}')
   visited.update(cache[key]);instructions+=1;recent.append(hex(addr))
   if addr==0x42ad50:movement_states.append(r(m,m.reg_read(UC_X86_REG_ESI)+0x97,1))
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
  executed_stats.append(dict(entry=hex(start),instructions=instructions))
  return dict(complete_return=True,return_eax=u.reg_read(UC_X86_REG_EAX),instruction_count=instructions,calls=calls,iscript_opcodes=script_ops,bullet_initializations=initializations,impact_events=impacts,movement_state_dispatches=movement_states,visited_functions=[hex(x) for x in sorted(visited)])
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
  counters={str(k):r(u,0x673940+k*4) for k in range(52)};seed=initial_seeds[id(u)];assert r(u,0x673938)==sum(counters.values());assert r(u,0x67393C)==advance(seed,sum(counters.values()))
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
  return dict(complete_return=all(x['complete_return'] for x in executions),complete_call_count=len(executions),return_eax=executions[-1]['return_eax'],instruction_count=sum(x['instruction_count'] for x in executions),calls=[c for x in executions for c in x['calls']],iscript_opcodes=[c for x in executions for c in x['iscript_opcodes']],bullet_initializations=[c for x in executions for c in x['bullet_initializations']],impact_events=[c for x in executions for c in x['impact_events']],movement_state_dispatches=[c for x in executions for c in x['movement_state_dispatches']],visited_functions=sorted({c for x in executions for c in x['visited_functions']}))
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
 def clear_world(u):
  normalize=environment(u,255)
  w(u,0xB00000,2,2);u.mem_write(0xB0000c,struct.pack('<H',1)*65536)
  region=0xB00000+0x449fc+64
  w(u,region,0,2);w(u,region+2,1,2);w(u,region+0xc,region+0x2c)
  pool=0x80c000;w(u,0x69b574,pool);w(u,0x69b570,pool);w(u,pool,0)
  return normalize
 def movement_snapshot(u):
  sprite=r(u,UNIT+0xc);position=list(struct.unpack('<2H',u.mem_read(UNIT+0x28,4)))
  fixed=list(struct.unpack('<2I',u.mem_read(UNIT+0x2c,8)))
  visible=list(struct.unpack('<2H',u.mem_read(sprite+0x14,4)))
  indices=[r(u,UNIT+k) for k in (0x13c,0x140,0x144,0x148)]
  borders=[r(u,base+8*index+4) for base,index in zip((0x5c99b8,0x5c99b8,0x5d03f8,0x5d03f8),indices)]
  assert visible==position and position==[x>>8 for x in fixed]
  typ=r(u,UNIT+0x64,2);e=tables['units'][typ].unit_extents
  expected=[position[0]-e.left,position[0]+e.right,position[1]-e.up,position[1]+e.down]
  assert borders==expected,(typ,position,indices,borders,expected)
  for base in (0x5c99b8,0x5d03f8):
   endpoints=[r(u,base+8*i+4) for i in range(r(u,0x5c99b0))]
   assert endpoints==sorted(endpoints)
  path=r(u,UNIT+0x100)
  return dict(position=position,fixed=fixed,sprite=visible,state=r(u,UNIT+0x97,1),speed=r(u,UNIT+0x38),heading=r(u,UNIT+0x21,1),velocity_heading=r(u,UNIT+0x23,1),flingy_flags=r(u,UNIT+0x20,1),path=hex(path),path_timer=r(u,path+0x19,1) if path else None,index_slots=indices,borders=borders,free_path_head=hex(r(u,0x69b570)))
 def create(u,typ,x,y,owner):
  c=call(u,0x424900,typ,x,(y,owner));at=c['return_eax'];assert at
  return at,[c,call(u,0x424da0,at)]
 def set_context(u,at):
  w(u,0x543300,at);w(u,0x543304,at);w(u,0x543308,0)
 def cargo(u,worker):
  sp=r(u,worker+0xc);at=r(u,sp+0x1c);imgs=[]
  while at:
   ident=r(u,at+8,2)
   if 397<=ident<=404:imgs.append(dict(address=at,id=ident,flags=r(u,at+0xc,2),offset=list(struct.unpack('<2b',u.mem_read(at+0xe,2)))))
   at=r(u,at+4);assert len(imgs)<8
  return dict(amount=r(u,worker+0xcf,1),type=r(u,worker+0xe0,1),images=imgs)
 if a.case_group in ('all','cargo'):
  for worker_type in (7,41,64):
   for resource_type,large_image in ((176,397),(177,397),(178,397),(110,403),(149,401),(157,399)):
    for amount in (0,1,2,7,8,9,15,16,257,258,507,508,757,758,1500):
     u=new();executions=[clear_world(u)]
     worker,ex=create(u,worker_type,512,512,3);executions+=ex
     resource,ex=create(u,resource_type,640,512,11 if resource_type>=176 else 3);executions+=ex
     w(u,resource+0xd0,amount,2);set_context(u,worker)
     before=snapshot(u);initial_animation=r(u,resource+0xd2,1);executions.append(call(u,0x423fd0,resource,args=(worker,)));after=snapshot(u)
     mineral=resource_type>=176;quantity=min(amount,8) if mineral else (8 if amount>=8 else 2)
     remaining=amount-8 if amount>=8 else (amount if mineral else 0)
     if mineral:
      expected_animation=initial_animation if amount<=8 else (20 if remaining>=750 else 15 if remaining>=500 else 14 if remaining>=250 else 13)
      assert r(u,resource+0xd2,1)==expected_animation,(amount,remaining,r(u,resource+0xd2,1),expected_animation)
     c=cargo(u,worker);ids=[x['id'] for x in c['images']]
     actual=[r(u,resource+0xd0,2),c['amount'],c['type'],ids,r(u,resource+0x4d,1)]
     record('stock_resource_cargo',dict(worker_type=worker_type,resource_type=resource_type,authored_initial_resource_amount=amount,native_creation=True,native_cargo_service=True),actual,[remaining,quantity,(2 if mineral else 1) if quantity else 0,[large_image+(quantity<8)] if quantity else [],0 if mineral and amount<=8 else 23],joined(executions),dict(before=before,after=after,cargo=c))
     if quantity:
      before_cargo=cargo(u,worker);beforefree=r(u,0x544694);deposited=call(u,0x427480,worker,args=(resource,));c=cargo(u,worker)
      base=0x67d074+3*0x4e8
      actual=[c['amount'],c['type'],[x['id'] for x in c['images']],r(u,0x5263d0+12),r(u,0x526400+12),r(u,0x526460+12),r(u,0x526430+12),r(u,base),r(u,base+4)]
      record('stock_cargo_deposit',dict(worker_type=worker_type,resource_type=resource_type,amount=quantity,native_cargo_before=before_cargo,free_image_before=beforefree),actual,[0,0,[],quantity if mineral else 0,0 if mineral else quantity,quantity if mineral else 0,0 if mineral else quantity,quantity if mineral else 0,0 if mineral else quantity],deposited,dict(cargo=c,free_image_after=r(u,0x544694)))
    print(json.dumps({'phase':'original populated resource cargo','worker':worker_type,'resource':resource_type,'cases':len(cases)}),flush=True)
 if a.case_group in ('all','cargo'):
  for typ in (7,41,64):
   for first,second in ((176,176),(176,110),(110,176),(110,110)):
    u=new();executions=[clear_world(u)];worker,ex=create(u,typ,512,512,3);executions+=ex;resources={}
    for ident,x in ((176,640),(110,896)):
     at,ex=create(u,ident,x,512,11 if ident==176 else 3);executions+=ex;resources[ident]=at;w(u,at+0xd0,1500,2)
    set_context(u,worker);executions.append(call(u,0x423fd0,resources[first],args=(worker,)));before=cargo(u,worker);free_count=len(chain(u,0x544694))
    executions.append(call(u,0x423fd0,resources[second],args=(worker,)));after=cargo(u,worker)
    actual=[after['amount'],after['type'],[x['id'] for x in after['images']],len(chain(u,0x544694)),r(u,resources[second]+0xd0,2)]
    record('native_cargo_replacement',dict(worker_type=typ,first_resource=first,second_resource=second,first_native_cargo=before,previous_free_images=free_count),actual,[8,2 if second==176 else 1,[397 if second==176 else 403],free_count,1484 if first==second else 1492],joined(executions),after)
    free_before=r(u,0x544694);guard=call(u,0x427090,worker,args=(402,17,1));guardafter=cargo(u,worker)
    record('native_cargo_setter_existing_guard',dict(worker_type=typ,native_existing_cargo=after,requested_image=402,requested_amount=17,requested_mineral=1),[guardafter,r(u,0x544694)],[after,free_before],guard)
   for resource_type in (176,110):
    u=new(image_count=4);executions=[clear_world(u)];worker,ex=create(u,typ,512,512,3);executions+=ex
    resource,ex=create(u,resource_type,640,512,11 if resource_type==176 else 3);executions+=ex;w(u,resource+0xd0,1500,2)
    assert r(u,0x544694)==0;set_context(u,worker);executions.append(call(u,0x423fd0,resource,args=(worker,)));after=cargo(u,worker)
    record('native_cargo_image_pool_exhausted',dict(worker_type=typ,resource_type=resource_type,native_image_pool_records=4,no_free_images_before_service=True),[r(u,resource+0xd0,2),after['amount'],after['type'],after['images'],r(u,0x544694)],[1492,8,2 if resource_type==176 else 1,[],0],joined(executions),after)
 if a.case_group in ('all','bonus'):
  profiles=[(11,2,True,1500),(12,2,True,1500),(13,2,True,1500),(12,0,True,1500),(12,1,True,1500),(12,3,True,1500),(12,2,False,1500)]+[(12,2,True,amount) for amount in (0,7,8,9)]
  for race,worker_type in ((0,41),(1,7),(2,64)):
   for count,mode,has_ai,amount in profiles:
    u=new(unit_count=16,sprite_count=32,image_count=64);executions=[clear_world(u)]
    w(u,0x6726d1+3*36,race,1);w(u,0x67d297+3*0x4e8,mode,1)
    for i in range(count):
     worker,ex=create(u,worker_type,512 if i==0 else 256+i*32,512 if i==0 else 1024,3);executions+=ex
    resource,ex=create(u,176,640,512,11);executions+=ex;w(u,resource+0xd0,amount,2)
    worker=UNIT;w(u,worker+0x134,0x818000 if has_ai else 0);set_context(u,worker)
    native_count=r(u,0x52c0c4+(worker_type*12+3)*4);assert native_count==count
    bonus=int(has_ai and mode==2 and count>=12)
    pred=call(u,0x482f40,worker);executions.append(pred);executions.append(call(u,0x423fd0,resource,args=(worker,)))
    c=cargo(u,worker);actual=[pred['return_eax'],r(u,resource+0xd0,2),c['amount'],c['type'],[x['id'] for x in c['images']]]
    quantity=min(amount,8);remaining=amount-8 if amount>=8 else amount
    record('native_completed_workers_bonus',dict(race=race,worker_type=worker_type,native_created_completed_workers=count,ai_pointer_presence=has_ai,authored_ai_mode_byte=mode,resource_amount=amount),actual,[bonus,remaining,quantity+bonus if quantity else 0,2 if quantity else 0,[397+int(quantity<8)] if quantity else []],joined(executions),dict(cargo=c,native_completed_count=native_count))
   print(json.dumps({'phase':'original populated bonus','race':race,'cases':len(cases)}),flush=True)
 if a.case_group in ('all','hatchery'):
  for depot_type in (131,132,133):
   ext=tables['units'][depot_type].unit_extents
   positions=((512-ext.left-1,512),(512-ext.left,512),(512+ext.right,512),(512,512-ext.up-1),(512-ext.left-1,512-ext.up),(512,512+ext.down),(512,512+ext.down+1),(512,512))
   for point in positions:
    for value in (0,1,24,99,100,101,124,125,32767,32768,65535):
     u=new();executions=[clear_world(u)];worker,ex=create(u,41,*point,3);executions+=ex;depot,ex=create(u,depot_type,512,512,3);executions+=ex
     before=[value]*4;u.mem_write(depot+0xd0,struct.pack('<4H',*before));executions.append(call(u,0x43da30,depot,args=(worker,)))
     expected=[max(x-1,0) for x in before]
     side=1 if point[1]<512-ext.up else 3 if point[1]>512+ext.down else 0 if point[0]<512-ext.left else 2
     signed=expected[side] if expected[side]<32768 else expected[side]-65536
     if signed<100:expected[side]=(expected[side]+25)&65535
     actual=list(struct.unpack('<4H',u.mem_read(depot+0xd0,8)))
     record('original_hatchery_deposit_side_weights',dict(depot_type=depot_type,worker_point=point,depot_point=[512,512],stock_extents=vars(ext),authored_initial_words=before,selected_side=side),actual,expected,joined(executions))
   print(json.dumps({'phase':'original hatchery deposit weights','depot':depot_type,'cases':len(cases)}),flush=True)
 if a.case_group in ('all','queue'):
  for resource_type,wait_order,mine_order in ((176,86,87),(110,82,83)):
   for profile in ('fifo','status400','irradiate','stasis','lockdown','both_disabled','newest_bad_order','cancel_waiting','cancel_active'):
    u=new();executions=[clear_world(u)];workers=[]
    for i in range(3):
     worker,ex=create(u,7,512,512+i*32,3);workers.append(worker);executions+=ex
    resource,ex=create(u,resource_type,640,512,11 if resource_type==176 else 3);executions+=ex;w(u,resource+0xd0,1500,2)
    for worker in workers:
     set_context(u,worker);executions.append(call(u,0x42a380,worker,args=(wait_order,resource)))
     executions.append(call(u,0x424230,worker,args=(resource,)))
    def queue_state():
     waiting=[];at=r(u,resource+0xd4);prev=0
     while at:
      assert r(u,at+0xd4)==prev;waiting.append(at);prev=at;at=r(u,at+0xd8);assert len(waiting)<4
     return dict(waiting=waiting,busy=r(u,resource+0xd3,1),workers=[dict(address=x,order=r(u,x+0x4d,1),order_state=r(u,x+0x4e,1),active=r(u,x+0xce,1),resource=r(u,x+0xd0)) for x in workers])
    established=queue_state();assert established['waiting']==workers[:0:-1] and established['busy']==1
    if profile=='status400':w(u,workers[1]+0xdc,r(u,workers[1]+0xdc)|0x400)
    if profile in ('irradiate','stasis','lockdown'):w(u,workers[1]+{'irradiate':0x117,'stasis':0x119,'lockdown':0x124}[profile],1,1)
    if profile=='both_disabled':
     for worker in workers[1:]:w(u,worker+0xdc,r(u,worker+0xdc)|0x400)
    if profile=='newest_bad_order':w(u,workers[2]+0x4d,2,1)
    if profile=='cancel_waiting':executions.append(call(u,0x424170,resource,args=(workers[1],)))
    if profile=='cancel_active':executions.append(call(u,0x424170,resource,args=(workers[0],)))
    else:executions.append(call(u,0x424130,workers[0],args=(resource,)))
    after=queue_state()
    chosen=workers[1] if profile in ('fifo','newest_bad_order','cancel_active') else (0 if profile=='both_disabled' else workers[2])
    expectedwait=[x for x in workers[:0:-1] if x!=chosen and not(profile=='cancel_waiting' and x==workers[1])]
    actual=[after['waiting'],after['busy'],[x['address'] for x in after['workers'] if x['active']],[x['order'] for x in after['workers'] if x['address']==chosen]]
    record('native_harvest_queue',dict(profile=profile,native_created_workers=3,resource_type=resource_type,authored_eligibility_fields=profile not in ('fifo','cancel_waiting','cancel_active'),initial_native_queue=established),actual,[expectedwait,int(bool(chosen)),[chosen] if chosen else [],[mine_order] if chosen else []],joined(executions),after)
  print(json.dumps({'phase':'original populated queue','cases':len(cases)}),flush=True)
 if a.case_group in ('all','cycle'):
  for typ,depot_type in ((7,106),(41,131),(64,154)):
   if a.cycle_worker is not None and typ!=a.cycle_worker:continue
   u=new(unit_count=16,sprite_count=32,image_count=128);executions=[clear_world(u),call(u,0x40a0e0,0)]
   worker,ex=create(u,typ,512,512,3);executions+=ex
   resource,ex=create(u,176,768,512,11);executions+=ex;w(u,resource+0xd0,1500,2)
   depot,ex=create(u,depot_type,256,512,3);executions+=ex
   set_context(u,worker);executions.append(call(u,0x42a380,worker,args=(79,resource)))
   for address in (0x6c46d4,0x69b568,0x69b564,0x6663e4):w(u,address,10000)
   for player in range(8):
    regional=0x80d000+player*0x400;u.mem_write(regional,bytes(0x400));w(u,0x6b1a74+player*4,regional)
   w(u,0x6a2e70,0);w(u,0x6a5d68,0);w(u,0x6b1a94,0);w(u,0x6b1a98,1);w(u,0x5432f0,0)
   timeline=[]
   for tick in range(500):
    w(u,0x52651c,tick);executions.append(call(u,0x445070,0));row=movement_snapshot(u)
    row.update(tick=tick+1,order=r(u,worker+0x4d,1),order_state=r(u,worker+0x4e,1),wait=r(u,worker+0x85,1),timer=r(u,worker+0x54,1),cargo=cargo(u,worker),resource_amount=r(u,resource+0xd0,2),minerals=r(u,0x5263d0+12),harvest_active=r(u,worker+0xce,1));timeline.append(row)
    assert r(u,0x543300)==r(u,0x543304)==r(u,0x543308)==0
    if row['minerals']>=8:break
   actual=[r(u,0x5263d0+12),r(u,0x526460+12),r(u,resource+0xd0,2),cargo(u,worker)['amount'],cargo(u,worker)['type']]
   if actual[0]!=8:
    print(json.dumps(dict(diagnostic_last=timeline[-3:],depot=snapshot(u)['units'][-1],transitions=[x for i,x in enumerate(timeline) if not i or (x['order'],x['order_state'])!=(timeline[i-1]['order'],timeline[i-1]['order_state'])])),flush=True)
   record('original_object_harvest_return',dict(worker_type=typ,resource_type=176,depot_type=depot_type,source=[512,512],resource_point=[768,512],depot_point=[256,512],issued_target_order=79,actual_whole_object_callbacks=True,authored_tick_clock=True,manual_position_order_stage_velocity_cargo_updates=False),actual,[8,8,1492,0,0],joined(executions),dict(timeline=timeline,depot_harvest_words=list(struct.unpack('<4H',u.mem_read(depot+0xd0,8)))))
   print(json.dumps({'phase':'original object harvest','worker':typ,'ticks':len(timeline),'cases':len(cases)}),flush=True)
 modules={Path(module.__file__).name:hashlib.sha256(Path(module.__file__).read_bytes()).hexdigest() for name,module in sorted(sys.modules.items()) if getattr(module,'__file__',None) and Path(module.__file__).parent.resolve()==Path(__file__).parent.resolve() and name!='__main__'}
 result=dict(target_sha256=local.TARGET_HASH,probe_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),source_dependencies_sha256=modules,engine=dict(unicorn=unicorn.__version__,pefile=pefile.__version__,python=platform.python_version(),machine=platform.machine()),dat_sha256=dat_hashes,asset_manifest_sha256=hashlib.sha256(a.asset_manifest.read_bytes()).hexdigest(),case_group=a.case_group,cycle_worker_filter=a.cycle_worker,helper_stubs=[],instruction_bound=100000,explicit_world_inputs=dict(map_pixels=[4096,4096],map_tiles=[128,128],map_rectangle=[0,0,4096,4096],clear_region_group=1,obstacle_contours=0,creep_record_dimensions=[128,128],creep_record_words=0,owner=3,controller=2,empty_ai_region_arrays=True,authored_accepted_frame_counter=True),reviewed_code_ranges=[dict(start=hex(lo),end_exclusive=hex(hi)) for lo,hi in sorted(set(ALLOW))],complete_original_function_calls=len(executed_stats),total_original_instructions_executed=sum(x['instructions'] for x in executed_stats),maximum_single_call_instructions=max(x['instructions'] for x in executed_stats),original_code_executed=True,complete_game_executed=False,complete_cases=len(cases),prefix_cases=0,all_passed=all(x['passed'] for x in cases),cases=cases)
 a.out.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({'complete_cases':len(cases),'all_passed':True}))
if __name__=='__main__':main()
