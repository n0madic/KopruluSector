#!/usr/bin/env python3
"""Probe computer combat and queued orders with populated original objects.

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
import inspect_native_computer_combat_117 as local
import probe_native_attack_orders_117 as attack_source
import inspect_native_guard_lifecycle_117 as guard_source
import inspect_native_guard_transfer_117 as transfer_source
import inspect_native_dat_requirements_117 as requirement_source
import inspect_native_military_arrival_117 as arrival_source
import inspect_native_reaction_spells_117 as reaction_source
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
ALLOW=[reaction_source.WITNESSES['weapon_target_validity']]+list(attack_source.ALLOW)+list(local.BLOCKS.values())+list(guard_source.WITNESSES.values())+list(transfer_source.WITNESSES.values())+list(requirement_source.WITNESSES.values())+list(arrival_source.WITNESSES.values())+list(arrival_source.military.BLOCKS.values())+list(arrival_source.spatial.BLOCKS.values())
PLAN=dict(target_sha256=local.TARGET_HASH,helper_stubs=[],instruction_bound=100000,scope='Computer AttackUnit with actual guard/military allocation, return-home and Stim, plus selected queued combat orders',limits='Private populated pools and stock DAT/IScript/GRP/LO; actual allocated guard/military AI metadata. Complete selected order calls and real combat effects. No full world, renderer/resource loader or continuously moving chase.')
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--exe',type=Path,required=True);p.add_argument('--data',type=Path,required=True);p.add_argument('--manifest',type=Path,required=True);p.add_argument('--assets',type=Path,required=True);p.add_argument('--asset-manifest',type=Path,required=True);p.add_argument('--pyms',type=Path,required=True);p.add_argument('--out',type=Path);p.add_argument('--case-group',choices=('all','guard','stim','orders','suicide'),default='all');p.add_argument('--execute-original-code',action='store_true');a=p.parse_args()
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
  w(u,0x673930,int(rng_enabled));w(u,0x673938,0);w(u,0x67393C,seed);w(u,0x543300,0);w(u,0x543304,0)
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
  except Exception as e:
   reg_ebx=u.reg_read(UC_X86_REG_EBX);reg_ecx=u.reg_read(UC_X86_REG_ECX);reg_edx=u.reg_read(UC_X86_REG_EDX)
   extra=dict(ebx=hex(reg_ebx),ecx=hex(reg_ecx),edx=hex(reg_edx),unit_context=hex(r(u,0x543300)))
   if IMAGE<=reg_edx<IMAGE+32*0x40:extra['image']=dict(id=r(u,reg_edx+8,2),grp=hex(r(u,reg_edx+0x2c)),loaded_grp=hex(r(u,0x5436f8+r(u,reg_edx+8,2)*4)))
   raise RuntimeError(f'{e}; diagnostic={extra}; recent={recent[-30:]}; calls={calls[-10:]}') from e
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
  sources={c['ecx'] for c in ex['calls'] if c['callee']=='0x478200'}|{c['stack'][0] for c in ex['calls'] if c['callee']=='0x478270'};counters={str(k):r(u,0x673940+k*4) for k in sorted(sources)};seed=initial_seeds[id(u)];assert sum(counters.values())==r(u,0x673938);assert r(u,0x67393C)==advance(seed,r(u,0x673938))
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
  w(u,0x69B584,0xB00000);w(u,0xB00000,4,2);
  for player in range(8):
   base=0x81e000+player*0x100;w(u,0x6b1a74+player*4,base);u.mem_write(base,b'\0'*(4*52))
   for region in range(4):w(u,base+region*52,region,2);w(u,base+region*52+4,player,1)
  w(u,0xB44A0C,200*256);w(u,0xB44A10,200*256);w(u,0xB97A1C,0x80A000)
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
 def guard_allocate(u,executions,unit=UNIT,home=None):
  w(u,0x6726d0+3*36,1,1);executions.append(call(u,0x4b2640,0));executions.append(call(u,0x4b2010,unit))
  ai=r(u,unit+0x134);assert ai and r(u,ai+8,1)==1 and r(u,ai+0xc)==unit
  if home is not None:executions.append(call(u,0x4b2270,unit,home[0],(home[1],)))
  return ai
 def military_allocate(u,executions,unit=UNIT):
  region=0x81e000+0x34;pool=0x810000;node=0x81c000
  w(u,0x6726d0+3*36,1,1);w(u,0x6b1a74+3*4,0x81e000);w(u,region,1,2);w(u,region+4,3,1);w(u,region+5,0,1);w(u,region+0x2c,pool);w(u,pool+0x4e20,node);w(u,node,0);w(u,node+4,0)
  sp=r(u,unit+0xc);x=r(u,sp+0x14,2);y=r(u,sp+0x16,2);w(u,0xb00000+0xc+2*((y>>5)*256+(x>>5)),1,2)
  descriptor=0xb00000+0x449fc+64;w(u,descriptor+0x10,x*256);w(u,descriptor+0x14,y*256)
  executions.append(call(u,0x4ae760,region,unit,(1,)));assert r(u,unit+0x134)==node and r(u,node+8,1)==4 and r(u,node+0xc)==unit and r(u,node+0x10)==region
  return node,region
 def combat_state(u):
  ai=r(u,UNIT+0x134)
  ai_fields=None if not ai else dict(address=ai,type=r(u,ai+8,1),parent=r(u,ai+0xc))
  if ai and r(u,ai+8,1)==1:ai_fields.update(home=[r(u,ai+0x12,2),r(u,ai+0x14,2)],other_home=[r(u,ai+0x16,2),r(u,ai+0x18,2)])
  elif ai and r(u,ai+8,1)==4:ai_fields['assigned_region']=r(u,ai+0x10)
  return dict(order=r(u,UNIT+0x4d,1),order_state=r(u,UNIT+0x4e,1),target=r(u,UNIT+0x5c),hp=r(u,UNIT+8),stim=r(u,UNIT+0x115,1),cooldowns=[r(u,UNIT+0x55,1),r(u,UNIT+0x56,1)],queue=[dict(address=p,id=r(u,p+8,1),pos=[r(u,p+0xc,2),r(u,p+0xe,2)],target=r(u,p+0x10)) for p in chain(u,UNIT+0x74)],ai=ai_fields,snapshot=snapshot(u))
 if a.case_group in ('all','guard'):
  for variant in ('positive','check_only','same_region','home_close','target_in_range','previous_attacker','queue_guard','queue_other'):
   u=new();distance=80 if variant=='target_in_range' else 500;executions=setup(u,positions=((200,200),(200+distance,200)))
   hx=300 if variant=='home_close' else 800;ai=guard_allocate(u,executions,home=(hx,200))
   if variant!='same_region':w(u,0xb00000+0xc+2*(6*256+(hx>>5)),1,2)
   executions.append(call(u,0x42a380,UNIT,args=(10,UNIT+0x150)))
   if variant=='previous_attacker':w(u,UNIT+0x7c,UNIT+0x150)
   if variant.startswith('queue_'):executions.append(call(u,0x429eb0,UNIT,args=(152 if variant=='queue_guard' else 14,0)))
   check=int(variant=='check_only');ex=call(u,0x4b2110,UNIT,check);executions.append(ex)
   allowed=variant in ('positive','check_only','queue_other')
   record('allocated_guard_return_home_'+variant,dict(stock_marine=True,home=[hx,200],variant=variant,actual_guard_allocation=True),[ex['return_eax'],r(u,UNIT+0x4d,1),r(u,UNIT+0x134)],[int(allowed),163 if allowed and not check else 10,ai],joined(executions),combat_state(u))
  for distance in (191,192,193):
   u=new();executions=setup(u,positions=((200,200),(700,200)));ai=guard_allocate(u,executions,home=(200+distance,200));w(u,0xb00000+0xc+2*(6*256+((200+distance)>>5)),1,2)
   executions.append(call(u,0x42a380,UNIT,args=(10,UNIT+0x150)));ex=call(u,0x41e5c0,UNIT);executions.append(ex)
   returns=distance>192
   record('computer_attack_guard_home_distance_'+str(distance),dict(stock_marine=True,actual_guard_allocation=True,home_distance=distance,different_regions_fixture=True,target_out_of_range=True),[r(u,UNIT+0x4d,1),r(u,UNIT+0x55,1)],[163 if returns else 10,0],joined(executions),combat_state(u))
  for hp in (120*256,120*256-1,119*256):
   u=new();executions=setup(u,8,0,positions=((200,200),(700,200)));ai=guard_allocate(u,executions,home=(800,200));w(u,0xb00000+0xc+2*(6*256+25),1,2);w(u,UNIT+8,hp);executions.append(call(u,0x42a380,UNIT,args=(10,UNIT+0x150)));ex=call(u,0x4b2110,UNIT);executions.append(ex)
   ok=hp==120*256
   record('guard_mechanical_exact_hp_'+str(hp),dict(stock_units=[8,0],actual_guard_allocation=True,home=[800,200],source_hp=hp,stock_race_and_properties=True),[ex['return_eax'],r(u,UNIT+0x4d,1)],[int(ok),163 if ok else 10],joined(executions),combat_state(u))
 if a.case_group in ('all','stim'):
  for hp,timer in ((10*256,0),(10*256+1,0),(40*256,37),(40*256,40)):
   u=new();executions=setup(u);w(u,UNIT+8,hp);w(u,UNIT+0x115,timer,1);ex=call(u,0x438af0,UNIT);executions.append(ex);ok=hp>10*256
   record('direct_stim_apply_'+str(hp)+'_'+str(timer),dict(stock_marine=True,initial_hp=hp,initial_stim=timer,decision_helper_not_called=True),[r(u,UNIT+8),r(u,UNIT+0x115,1),r(u,0x673940+31*4)],[hp-(10*256 if ok else 0),max(timer,37) if ok else timer,int(ok)],joined(executions),combat_state(u))
  variants=('positive','hp19','hp20','hp19_fraction','target99','target99_fraction','target100','not_researched','not_available','already_stim','ensnared','stim_ensnared','speed_upgrade','weapon_upgrade','invulnerable_target','source_invulnerable','source_disabled','out_of_range','zealot_shields','shield_threshold99','shield_threshold100')
  for variant in variants:
   vt=65 if variant.startswith(('zealot','shield')) else 8;distance=500 if variant=='out_of_range' else 80
   u=new();executions=setup(u,0,vt,positions=((200,200),(200+distance,200)));ai=guard_allocate(u,executions);executions.append(call(u,0x4080d0,0));w(u,0x534104+3*24,int(variant!='not_available'),1);w(u,0x534224+3*24,int(variant!='not_researched'),1)
   initial_hp={'hp19':19*256,'hp20':20*256,'hp19_fraction':19*256+1}.get(variant,40*256);w(u,UNIT+8,initial_hp)
   if variant.startswith('target'):w(u,UNIT+0x150+8,99*256+1 if variant=='target99_fraction' else int(variant[6:])*256)
   if variant=='zealot_shields':w(u,UNIT+0x150+8,40*256)
   if variant.startswith('shield_threshold'):w(u,UNIT+0x150+8,40*256);w(u,UNIT+0x150+0x60,60*256 if variant.endswith('100') else 60*256-1)
   if variant in ('already_stim','stim_ensnared'):w(u,UNIT+0x115,1,1)
   if variant in ('ensnared','stim_ensnared'):w(u,UNIT+0x116,1,1)
   if variant in ('speed_upgrade','weapon_upgrade'):w(u,UNIT+0xdc,r(u,UNIT+0xdc)|(0x10000000 if variant=='speed_upgrade' else 0x20000000))
   if variant=='invulnerable_target':w(u,UNIT+0x150+0xdc,r(u,UNIT+0x150+0xdc)|0x4000000)
   if variant=='source_disabled':w(u,UNIT+0xdc,r(u,UNIT+0xdc)|0x400)
   if variant=='source_invulnerable':w(u,UNIT+0xdc,r(u,UNIT+0xdc)|0x4000000)
   executions.append(call(u,0x42a380,UNIT,args=(10,UNIT+0x150)));w(u,UNIT+0x21,64,1);w(u,UNIT+0x23,64,1);executions.append(call(u,0x417190,r(u,UNIT+0xc),args=(64,)))
   initial_stim=r(u,UNIT+0x115,1);ex=call(u,0x4b1100,UNIT);executions.append(ex)
   stimulates=variant not in ('hp19','target99','not_researched','not_available','already_stim','speed_upgrade','source_invulnerable','source_disabled','out_of_range','shield_threshold99')
   record('computer_stim_decision_'+variant,dict(stock_units=[0,vt],variant=variant,controller=1,actual_guard_allocation=True,initial_hp=initial_hp,initial_stim=initial_stim),[r(u,UNIT+8),r(u,UNIT+0x115,1),r(u,UNIT+0x134),r(u,0x673940+31*4)],[initial_hp-(10*256 if stimulates else 0),37 if stimulates else initial_stim,ai,int(stimulates)],joined(executions),combat_state(u))
  for typ,vt in ((0,8),(8,0),(8,8),(65,0)):
   distance=24 if typ==65 else 80
   u=new();executions=setup(u,typ,vt,positions=((200,200),(200+distance,200)));ai=guard_allocate(u,executions);executions.append(call(u,0x4080d0,0));w(u,0x534104+3*24,1,1);w(u,0x534224+3*24,1,1)
   executions.append(call(u,0x42a380,UNIT,args=(10,UNIT+0x150)));w(u,UNIT+0x21,64,1);w(u,UNIT+0x23,64,1);executions.append(call(u,0x417190,r(u,UNIT+0xc),args=(64,)))
   executions.append(call(u,0x41e5c0,UNIT))
   for tick in range(20):image_and_bullet_step(u,executions)
   damage={0:6,8:20 if vt==8 else 8,65:16}[typ]
   expected=(tables['units'][vt].hit_points.whole-damage)*256
   record('computer_guard_attack_effect_'+str(typ)+'_'+str(vt),dict(stock_units=[typ,vt],controller=1,actual_guard_allocation=True,stim_available_researched_fixture=True,heading=64,image_steps=20),[r(u,UNIT+0x134),r(u,UNIT+0x150+8),r(u,UNIT+0x115,1)],[ai,expected,37 if typ==0 else 0],joined(executions),combat_state(u))
  for typ,vt in ((0,8),(8,0),(8,8)):
   u=new();executions=setup(u,typ,vt);ai,region=military_allocate(u,executions);executions.append(call(u,0x4080d0,0));w(u,0x534104+3*24,1,1);w(u,0x534224+3*24,1,1)
   executions.append(call(u,0x42a380,UNIT,args=(10,UNIT+0x150)));w(u,UNIT+0x21,64,1);w(u,UNIT+0x23,64,1);executions.append(call(u,0x417190,r(u,UNIT+0xc),args=(64,)));executions.append(call(u,0x41e5c0,UNIT))
   for tick in range(20):image_and_bullet_step(u,executions)
   dmg=6 if typ==0 else (20 if vt==8 else 8)
   record('computer_military_attack_effect_'+str(typ)+'_'+str(vt),dict(stock_units=[typ,vt],controller=1,actual_military_allocation=True,region_state=0,force_allocation=1,stim_researched_available_fixture=True,image_steps=20),[r(u,UNIT+0x134),r(u,ai+8,1),r(u,region+0x30),r(u,UNIT+0x150+8),r(u,UNIT+0x115,1)],[ai,4,ai,(tables['units'][vt].hit_points.whole-dmg)*256,37 if typ==0 else 0],joined(executions),combat_state(u))
 if a.case_group in ('all','orders'):
  for typ,vt in ((0,0),(8,0),(8,8)):
   u=new();executions=setup_enemy(u,typ,vt);victim=UNIT+0x150
   w(u,0x534914+3*12+3,1,1);w(u,0x534914+4*12+4,1,1)
   executions.append(call(u,0x42a340,UNIT,args=(14,500,200)));w(u,UNIT+0x21,64,1);w(u,UNIT+0x23,64,1);executions.append(call(u,0x417190,r(u,UNIT+0xc),args=(64,)))
   executions.append(call(u,0x439420,UNIT));executions.append(call(u,0x439420,UNIT))
   for tick in range(20):image_and_bullet_step(u,executions)
   dmg=6 if typ==0 else (20 if vt==8 else 8)
   record('human_attack_move_acquire_attack_'+str(typ)+'_'+str(vt),dict(stock_units=[typ,vt],owners=[3,4],controller=2,order14_destination=[500,200],native_issue=True,image_steps=20),[r(u,UNIT+0x4d,1),r(u,UNIT+0x5c),r(u,victim+8),r(u,r(u,UNIT+0x74)+8,1)],[10,victim,(tables['units'][vt].hit_points.whole-dmg)*256,14],joined(executions),combat_state(u))
  for typ,vt in ((0,0),(0,8)):
   u=new();executions=setup_enemy(u,typ,vt);victim=UNIT+0x150;w(u,0x534914+3*12+3,1,1);w(u,0x534914+4*12+4,1,1)
   executions.append(call(u,0x42a2c0,UNIT,args=(107,)));w(u,UNIT+0x21,64,1);w(u,UNIT+0x23,64,1);executions.append(call(u,0x417190,r(u,UNIT+0xc),args=(64,)))
   executions.append(call(u,0x439420,UNIT));executions.append(call(u,0x439420,UNIT))
   for tick in range(20):image_and_bullet_step(u,executions)
   record('human_hold_acquire_attack_'+str(typ)+'_'+str(vt),dict(stock_units=[typ,vt],owners=[3,4],controller=2,order107_native_issue=True,order_calls=2,image_steps=20),[r(u,UNIT+0x4d,1),r(u,UNIT+0x5c),r(u,victim+8),r(u,0x673940+13*4)],[107,victim,(tables['units'][vt].hit_points.whole-6)*256,1],joined(executions),combat_state(u))
  for variant in ('target_invalidated','arrival'):
   u=new();executions=setup_enemy(u);victim=UNIT+0x150;w(u,0x534914+3*12+3,1,1);w(u,0x534914+4*12+4,1,1)
   executions.append(call(u,0x42a340,UNIT,args=(14,200 if variant=='arrival' else 500,200)));w(u,UNIT+0x21,64,1);w(u,UNIT+0x23,64,1);executions.append(call(u,0x417190,r(u,UNIT+0xc),args=(64,)))
   executions.append(call(u,0x439420,UNIT))
   if variant=='target_invalidated':w(u,victim+0xdc,r(u,victim+0xdc)|0x4000000);w(u,UNIT+0x85,0,1);executions.append(call(u,0x439420,UNIT))
   record('attack_move_resume_'+variant,dict(stock_units=[0,0],owners=[3,4],controller=2,native_order14=True,variant=variant),[r(u,UNIT+0x4d,1),r(u,UNIT+0x74),r(u,UNIT+0x58,2),r(u,UNIT+0x5a,2)],[14 if variant=='target_invalidated' else 2,0,500 if variant=='target_invalidated' else 0,200 if variant=='target_invalidated' else 0],joined(executions),combat_state(u))
  for typ,vt,draws,weapon in ((0,0,8,0),(8,0,9,16),(8,8,10,15)):
   for residue in range(4):
    seed=next(x for x in range(1000) if ((advance(x,draws+1)>>16)&3)==residue)
    u=new(seed=seed);executions=setup_enemy(u,typ,vt);victim=UNIT+0x150;w(u,0x534914+3*12+3,1,1);w(u,0x534914+4*12+4,1,1)
    executions.append(call(u,0x42a2c0,UNIT,args=(107,)));w(u,UNIT+0x21,64,1);w(u,UNIT+0x23,64,1);executions.append(call(u,0x417190,r(u,UNIT+0xc),args=(64,)));executions.append(call(u,0x439420,UNIT));executions.append(call(u,0x439420,UNIT))
    record('hold_cooldown_jitter_'+str(typ)+'_'+str(vt)+'_'+str(residue),dict(stock_units=[typ,vt],owners=[3,4],controller=2,native_order107=True,main_dispatch_calls=2,seed=seed,next_source13_residue=residue),[r(u,UNIT+0x55,1),r(u,UNIT+0x56,1),r(u,0x673940+13*4)],[tables['weapons'][weapon].weapon_cooldown+residue-1]*2+[1],joined(executions),combat_state(u))
 if a.case_group in ('all','suicide'):
  for computer in (False,True):
   u=new();executions=setup(u,47,8,positions=((200,200),(224,200)));ai=guard_allocate(u,executions) if computer else 0
   executions.append(call(u,0x42a380,UNIT,args=(10,UNIT+0x150)));w(u,UNIT+0x21,64,1);w(u,UNIT+0x23,64,1);executions.append(call(u,0x417190,r(u,UNIT+0xc),args=(64,)))
   executions.append(call(u,0x439420,UNIT));timeline=[]
   for tick in range(20):image_and_bullet_step(u,executions,update_attacker=bool(r(u,UNIT+0xc)));timeline.append(trace_row(u,tick+1))
   record('stock_scourge_attack_suicide_'+str(computer),dict(stock_units=[47,8],controller=1 if computer else 2,actual_guard_allocation=computer,positions=[[200,200],[224,200]],native_order10=True,main_dispatch=True,image_steps=20),[r(u,UNIT+0x4d,1),r(u,UNIT+0x150+8)],[0,10*256],joined(executions),dict(timeline=timeline,final=combat_state(u)))
  for order in (134,135):
   u=new();executions=setup_enemy(u,50,0,positions=((200,200),(212,200)));victim=UNIT+0x150;w(u,0x534914+3*12+3,1,1);w(u,0x534914+4*12+4,1,1)
   if order==134:executions.append(call(u,0x42a380,UNIT,args=(order,victim)))
   else:executions.append(call(u,0x42a340,UNIT,args=(order,200,200)))
   w(u,UNIT+0x21,64,1);w(u,UNIT+0x23,64,1);executions.append(call(u,0x417190,r(u,UNIT+0xc),args=(64,)));executions.append(call(u,0x439420,UNIT));timeline=[]
   for tick in range(20):
    image_and_bullet_step(u,executions,update_attacker=bool(r(u,UNIT+0xc)))
    if r(u,UNIT+0x4d,1)==order:executions.append(call(u,0x439420,UNIT))
    timeline.append(trace_row(u,tick+1))
   record('stock_infested_sap_'+str(order),dict(stock_units=[50,0],controller=2,positions=[[200,200],[212,200]],native_primary_order=order,main_dispatch=True,image_steps=20),[r(u,UNIT+0x4d,1),r(u,victim+8)],[0,0],joined(executions),dict(timeline=timeline,final=combat_state(u)))
 modules={Path(module.__file__).name:hashlib.sha256(Path(module.__file__).read_bytes()).hexdigest() for name,module in sorted(sys.modules.items()) if getattr(module,'__file__',None) and Path(module.__file__).parent.resolve()==Path(__file__).parent.resolve() and name!='__main__'}
 result=dict(target_sha256=local.TARGET_HASH,probe_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),source_dependencies_sha256=modules,engine=dict(unicorn=unicorn.__version__,pefile=pefile.__version__,machine=platform.machine()),dat_sha256=dat_hashes,stock_weapon_rows=[dict(id=i,weapon=vars(tables['weapons'][i]),flingy=vars(tables['flingy'][tables['weapons'][i].graphics]),sprite=vars(tables['sprites'][tables['flingy'][tables['weapons'][i].graphics].sprite])) for i in (0,8,15,16,54,55,64)],asset_manifest_sha256=hashlib.sha256(a.asset_manifest.read_bytes()).hexdigest(),case_group=a.case_group,helper_stubs=[],instruction_bound=100000,reviewed_code_ranges=[dict(start=hex(lo),end_exclusive=hex(hi)) for lo,hi in sorted(set(ALLOW))],complete_cases=len(cases),prefix_cases=0,all_passed=all(x['passed'] for x in cases),cases=cases)
 a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(dict(cases=len(cases),all_passed=result['all_passed'])))
if __name__=='__main__':main()
