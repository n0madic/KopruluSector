#!/usr/bin/env python3
"""Probe populated stock spell creation, expiry and Recall relocation in private memory.

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
import inspect_native_spell_effects_117 as effects_source
import inspect_native_spell_fields_117 as local
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
ALLOW=list(projectile_source.ALLOW)+list(effects_source.WITNESSES.values())+[effects_source.combat.BLOCKS["energy_insufficient"]]+list(local.BLOCKS.values())
PLAN=dict(target_sha256=local.TARGET_HASH,helper_stubs=[],instruction_bound=100000,scope='Positive stock DarkSwarm creation/projectile delivery/timed-life removal and Recall relocation/effect expiry',limits='Private populated pools and stock DAT/IScript/GRP/LO/TBL input on synthetic flat terrain/regions. Recall issuer case explicitly clears InitArbiter status1000 to define a ready caster; original InitArbiter handler and frontend admission are excluded. Maximum default100000 instructions per entry; no-walkable512-map rollback allows2000000. Original rendering, complete game and campaign execution excluded')
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--exe',type=Path,required=True);p.add_argument('--data',type=Path,required=True);p.add_argument('--manifest',type=Path,required=True);p.add_argument('--assets',type=Path,required=True);p.add_argument('--asset-manifest',type=Path,required=True);p.add_argument('--pyms',type=Path,required=True);p.add_argument('--out',type=Path);p.add_argument('--execute-original-code',action='store_true');a=p.parse_args()
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
 script=stock('scripts\\iscript.bin');orders_raw=stock('arr\\orders.dat');orderrows=orders.stock_orders(orders_raw);tech_raw=stock('arr\\techdata.dat');stat_txt=stock('rez\\stat_txt.tbl')
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
  assert at-ASSETS<0x580000
  u.mem_write(ASSETS+0x580000,stat_txt);w(u,0x6AB4F8,ASSETS+0x580000)
  for key,base in (('units',0x5014F0),('flingy',0x501148),('sprites',0x501408),('images',0x5011A8),('weapons',0x501828)):
   v=table_raw[key];offset=0
   for prop_index in range({'units':54,'flingy':7,'sprites':6,'images':14,'weapons':24}[key]):
    destination,width,count=struct.unpack('<3I',pe.get_data(base-0x400000+prop_index*12,12));n=width*count;u.mem_write(destination,v[offset:offset+n]);offset+=n
   assert offset==len(v)
  u.mem_write(0x51B740,orders_raw[2646:2835]);u.mem_write(0x51B800,orders_raw[2835:3024]);u.mem_write(0x51B8C0,orders_raw[3024:3213]);u.mem_write(0x51F868,tech_raw[264:352])
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
 def call(u,start,ecx,edx=0,args=(),bound=100000):
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
  try:u.emu_start(start,STOP+1,count=bound)
  except Exception as e:raise RuntimeError(f'{e}; recent={recent[-30:]}; calls={calls[-10:]}') from e
  finally:u.hook_del(h)
  assert boundary and u.reg_read(UC_X86_REG_ESP)==sp+4*(len(args)+1),(hex(start),instructions,bound,recent[-10:])
  executed_stats.append(dict(entry=hex(start),instruction_count=instructions,instruction_bound=bound,visited_functions=[hex(x)for x in sorted(visited)]))
  return dict(complete_return=True,instruction_bound=bound,return_eax=u.reg_read(UC_X86_REG_EAX),instruction_count=instructions,calls=calls,iscript_opcodes=script_ops,bullet_initializations=initializations,impact_events=impacts,visited_functions=[hex(x) for x in sorted(visited)])
 def snapshot(u):
  parent=r(u,0x665B08+3*4);result=dict(owner_head=parent,unit_count=r(u,0x665B38),free_unit_head=r(u,0x665AF0),free_sprite_head=r(u,0x5C51C8),free_image_head=r(u,0x544694),hidden_head=r(u,0x665AF8),rng_state=r(u,0x67393C),pending_delete_head=r(u,0x665AE8),error=r(u,0x5DA398,2),rng_counters={str(k):r(u,0x673940+k*4) for k in (3,6,7,14,15,37)},units=[])
  for j in range(4):
   unit=UNIT+j*0x150
   if r(u,unit+0xC)==0:continue
   sp=r(u,unit+0xC);images=[];at=r(u,sp+0x1C)
   while at:
    images.append(dict(address=at,id=r(u,at+8,2),flags=r(u,at+0xC,2),direction=r(u,at+0xB,1),offset=list(struct.unpack('<2b',u.mem_read(at+0xE,2))),iscript=list(struct.unpack('<3H2B',u.mem_read(at+0x10,8))),base_frame=r(u,at+0x18,2),frame=r(u,at+0x1A,2),bounds=list(struct.unpack('<8h',u.mem_read(at+0x1C,16))),sprite=r(u,at+0x3C),grp=r(u,at+0x2C)))
    at=r(u,at+4);assert len(images)<32
   result['units'].append(dict(address=unit,type=r(u,unit+0x64,2),owner=r(u,unit+0x4C,1),status=r(u,unit+0xDC),hp=r(u,unit+8),shield=r(u,unit+0x60),energy=r(u,unit+0xA2,2),direction=r(u,unit+0x21,1),cooldowns=[r(u,unit+x,1) for x in (0x54,0x55,0x56,0x57)],movement_state=r(u,unit+0x97,1),live_counter=r(u,0x529604+(r(u,unit+0x64,2)*12+r(u,unit+0x4C,1))*4),completed_counter=r(u,0x52C0C4+(r(u,unit+0x64,2)*12+r(u,unit+0x4C,1))*4),wireframe_randomizer=r(u,unit+0xE1,1),generation=r(u,unit+0xA5,1),primary_order=r(u,unit+0x4D,1),secondary_order=r(u,unit+0xA6,1),build_time=r(u,unit+0xAC,2),queue=list(struct.unpack('<5H',u.mem_read(unit+0x98,10))),speed=r(u,unit+0x34),subunit=r(u,unit+0x70),sprite=dict(address=sp,position=[r(u,sp+0x14,2),r(u,sp+0x16,2)],elevation=r(u,sp+0xD,1),id=r(u,sp+8,2),flags=r(u,sp+0xE,1),owner=r(u,sp+0xA,1),visibility=r(u,sp+0xC,1),main=r(u,sp+0x18),images=images)))
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
  return dict(complete_return=all(x['complete_return'] for x in executions),instruction_bound=max(x['instruction_bound']for x in executions),complete_call_count=len(executions),return_eax=executions[-1]['return_eax'],instruction_count=sum(x['instruction_count'] for x in executions),calls=[c for x in executions for c in x['calls']],iscript_opcodes=[c for x in executions for c in x['iscript_opcodes']],bullet_initializations=[c for x in executions for c in x['bullet_initializations']],impact_events=[c for x in executions for c in x['impact_events']],visited_functions=sorted({c for x in executions for c in x['visited_functions']}))
 def environment(u,visibility=0):
  w(u,0x69B584,0xB00000);w(u,0xB00000,4,2);w(u,0xB44A0C,200*256);w(u,0xB44A10,200*256);w(u,0xB97A1C,0x80A000)
  for address,offset in ((0x666454,0),(0x666458,0x10000),(0x66696C,0x20000),(0x6CE650,0x40000),(0x66644C,0x50000)):w(u,address,0xC00000+offset)
  w(u,0x54323C,0xC10000);u.mem_write(0xC00000,struct.pack('<I',0x1FFFF if not visibility else 0x1FF00)*16384);w(u,0x67CF60,0xC60000)
  w(u,0x6D319C,2);w(u,0x5C99B0,0);w(u,0x5D6E40,256);w(u,0x5D6E44,160)
  w(u,0x5264CC+12,8);w(u,0x672668,1);w(u,0x67266C,1)
  w(u,0x5AEF00,0x80B000);w(u,0x5AEF04,0x80B000+31*20);w(u,0x5AEF08,0)
  for i in range(32):w(u,0x80B000+i*20,0x80B000+(i-1)*20 if i else 0);w(u,0x80B000+i*20+4,0x80B000+(i+1)*20 if i<31 else 0)
  w(u,0x5DA390,2);w(u,0x5DA394,2);w(u,0x6663EC,0,1);w(u,0x6751F4,0);w(u,0x6BE650,0,1);w(u,0x665D40,0);w(u,0x665B60,0)
 def setup(u,typ=0,victim_type=0,positions=((200,200),(280,200))):
  environment(u,255);executions=[call(u,0x40A0E0,0)]
  for at,t,(x,y) in ((UNIT,typ,positions[0]),(UNIT+0x150,victim_type,positions[1])):
   created=call(u,0x424900,t,x,(y,3));assert created['return_eax']==at
   executions.extend((created,call(u,0x424DA0,at)))
  w(u,UNIT+0x5C,UNIT+0x150);w(u,UNIT+0x150+0x7C,UNIT)
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
 def prepare_recall(thingy_count=8):
  uc=new(thingy_count=thingy_count);environment(uc,255);executions=[]
  for at,t,x,y in ((UNIT,71,300,300),(UNIT+0x150,0,600,600)):
   created=call(uc,0x424900,t,x,(y,3));assert created['return_eax']==at
   executions.extend((created,call(uc,0x424DA0,at)))
  w(uc,0x543300,UNIT);w(uc,0x543304,UNIT);w(uc,0x665B68,0)
  return uc,executions
 def sprite_position(uc,at):
  sp=r(uc,at+0xC);return [r(uc,sp+0x14,2),r(uc,sp+0x16,2)]
 def effect_snapshot(uc):
  result=[]
  for at in chain(uc,0x5C7A28):
   sp=r(uc,at+0xC);result.append(dict(address=at,sprite=sp,position=[r(uc,sp+0x14,2),r(uc,sp+0x16,2)],elevation=r(uc,sp+0xD,1),images=[dict(id=r(uc,im+8,2),grp=r(uc,im+0x2C),animation=r(uc,im+0x16,1),pc=r(uc,im+0x12,2),wait=r(uc,im+0x17,1))for im in chain(uc,sp+0x1C)]))
  return result
 u=new();environment(u,255)
 ex=call(u,0x43DC10,200,200,(3,))
 sp=r(u,UNIT+0xC);im=r(u,sp+0x18)
 actual=[r(u,UNIT+0x64,2),r(u,UNIT+0x4C,1),r(u,UNIT+0x110,2),r(u,sp+0xD,1),r(u,im+8,2),int(r(u,im+0x2C)!=0),r(u,0x529604+(202*12+11)*4),r(u,0x52C0C4+(202*12+11)*4),int(bool(r(u,UNIT+0xDC)&0x200000))]
 record('stock_dark_swarm_positive_create',dict(owner=3,position=[200,200],stock_type=202,populated_pools=True),actual,[202,11,900,11,337,1,1,1,1],ex,snapshot(u))
 print('Dark Swarm populated creation complete',flush=True)
 u=new();environment(u,255);executions=[call(u,0x43DC10,200,200,(3,))]
 for i in range(900):executions.append(call(u,0x43A320,UNIT))
 record('stock_dark_swarm_full_900_native_timer_calls',dict(stock_type=202,fixture_timer_override=False,native_timer_calls=900),[r(u,UNIT+0x110,2),r(u,UNIT+0x4D,1)],[0,0],joined(executions),snapshot(u))
 timeline=[]
 for tick in range(100):
  executions.append(call(u,0x438EF0,0));executions.append(call(u,0x418990,0))
  timeline.append(dict(tick=tick+1,count=r(u,0x665B38),things=chain(u,0x5C7A28),pending=chain(u,0x665AE8),free_images=len(chain(u,0x544694)),free_sprites=len(chain(u,0x5C51C8))))
  if not r(u,0x665B38) and not r(u,0x5C7A28):break
 assert tick<99
 record('stock_dark_swarm_expiry_returns_populated_pools',dict(stock_type=202,native_timer_calls=900,active_steps=timeline),[r(u,0x665B38),r(u,0x529604+(202*12+11)*4),r(u,0x52C0C4+(202*12+11)*4),len(chain(u,0x665AF0)),len(chain(u,0x544694)),len(chain(u,0x5C51C8)),chain(u,0x665AE8),chain(u,0x5C7A28)],[0,0,0,4,32,8,[],[]],joined(executions),snapshot(u))
 print('Dark Swarm expiry complete',flush=True)
 u=new();environment(u,255);executions=[]
 for at,t,x,y in ((UNIT,71,300,300),(UNIT+0x150,0,600,600)):
  created=call(u,0x424900,t,x,(y,3));assert created['return_eax']==at
  executions.extend((created,call(u,0x424DA0,at)))
 before_recall=snapshot(u)
 w(u,0x543300,UNIT);w(u,0x543304,UNIT);w(u,0x665B68,0)
 executions.append(call(u,0x4304D0,UNIT+0x150,args=(UNIT,)))
 sp=r(u,UNIT+0x150+0xC)
 actual=[r(u,sp+0x14,2),r(u,sp+0x16,2),r(u,0x665B68),r(u,UNIT+0x150+0x4D,1),len(chain(u,0x5C7A28))]
 record('stock_recall_actual_relocation',dict(caster_type=71,target_type=0,old=[600,600],destination=[300,300],populated_pools=True),actual,[304,328,1,2,1],joined(executions),snapshot(u))
 print('Recall populated relocation complete',flush=True)
 u=new();environment(u,255);executions=[]
 for at,t,x,y in ((UNIT,71,300,300),(UNIT+0x150,0,600,600)):
  created=call(u,0x424900,t,x,(y,3));assert created['return_eax']==at
  executions.extend((created,call(u,0x424DA0,at)))
 w(u,0x543300,UNIT);w(u,0x543304,UNIT);w(u,UNIT+0xA2,200*256,2)
 w(u,UNIT+0xDC,r(u,UNIT+0xDC)&~0x1000) # Explicit ready-state fixture; original InitArbiter handler is outside this batch.
 executions.append(call(u,0x42A340,UNIT,args=(137,600,600)))
 assert r(u,UNIT+0x4D,1)==137 and r(u,UNIT+0x4E,1)==0
 executions.append(call(u,0x42FC40,UNIT));started=[r(u,UNIT+0xA2,2),r(u,UNIT+0x54,1),r(u,UNIT+0x4E,1),len(chain(u,0x5C7A28))]
 assert started==[50*256,22,1,1],started
 for i in range(22):executions.append(call(u,0x43A320,UNIT))
 assert r(u,UNIT+0x54,1)==0
 executions.append(call(u,0x42FC40,UNIT));sp=r(u,UNIT+0x150+0xC)
 actual=[r(u,UNIT+0xA2,2),r(u,sp+0x14,2),r(u,sp+0x16,2),r(u,0x665B68),r(u,UNIT+0x150+0x4D,1),len(chain(u,0x5C7A28)),r(u,UNIT+0x4D,1),r(u,UNIT+0x74)]
 record('stock_recall_start_22_native_ticks_positive_completion',dict(caster_type=71,target_type=0,initial_energy=200*256,initial_order=137,old_target_position=[600,600],caster_position=[300,300],stock_cost=150*256,native_timer_calls=22,started=started,populated_pools=True,occupied_caster_fit=True,ready_state_fixture_clears_InitArbiter_gate_1000=True),actual,[50*256+22*8,304,328,1,2,2,2,0],joined(executions),snapshot(u))
 print('Recall full positive start/timer/selection/relocation complete',flush=True)
 timeline=[]
 for tick in range(100):
  executions.append(call(u,0x418990,0));timeline.append(dict(tick=tick+1,things=chain(u,0x5C7A28),free_images=len(chain(u,0x544694)),free_sprites=len(chain(u,0x5C51C8))))
  if not r(u,0x5C7A28):break
 assert tick<99
 record('stock_recall_positive_effects_expire_actual_iscript',dict(stock_image=391,native_image_steps=timeline,units_remain=2),[r(u,0x665B38),len(chain(u,0x5C7A28)),len(chain(u,0x544694)),len(chain(u,0x5C51C8)),len(chain(u,0x5C7A20))],[2,0,28,6,8],joined(executions),snapshot(u))
 print('Recall positive effect image expiry complete',flush=True)
 for x,y in ((0,0),(4095,4095),(79,79),(80,80),(4016,3984),(4017,3985)):
  for owner in (0,3,11):
   u=new();environment(u,255);ex=call(u,0x43DC10,x,y,(owner,));sp=r(u,UNIT+0xC);ext=tables['units'][202].unit_extents
   expected_pos=[max(ext.left,min(x,4096-ext.right-1)),max(ext.up,min(y,4096-ext.down-33))]
   actual=[r(u,sp+0x14,2),r(u,sp+0x16,2),r(u,UNIT+0x4C,1),r(u,UNIT+0x110,2)]
   record('stock_dark_swarm_map_clamp_neutral_owner',dict(request=[x,y],caster_owner=owner,stock_extents=vars(ext),bottom_reserved_pixels=32),actual,expected_pos+[11,900],ex,snapshot(u))
 print('Dark Swarm boundaries complete',flush=True)
 for name,mutation in (('hidden_sprite',('sprite',0x20)),('other_owner',('owner',4)),('dat_building_type',('type',106)),('invincible',('status',0x4000000)),('burrowed',('status',0x10)),('Larva',('type',35)),('Egg',('type',36)),('LurkerEgg',('type',97)),('Hallucination',('status',0x40000000)),('caster_self',('self',0))):
  u,executions=prepare_recall();target=UNIT+0x150;kind,value=mutation
  if kind=='sprite':sp=r(u,target+0xC);w(u,sp+0xE,r(u,sp+0xE,1)|value,1)
  elif kind=='owner':w(u,target+0x4C,value,1)
  elif kind=='type':w(u,target+0x64,value,2)
  elif kind=='status':w(u,target+0xDC,r(u,target+0xDC)|value)
  elif kind=='self':target=UNIT
  before=sprite_position(u,target);executions.append(call(u,0x4302C0,target,UNIT))
  record('stock_recall_callback_rejection_'+name,dict(target_mutation=mutation,stock_created_target_type=0,explicit_synthetic_field_override=True),[executions[-1]['return_eax'],r(u,0x665B68),sprite_position(u,target),len(chain(u,0x5C7A28))],[0,0,before,0],joined(executions))
 for no_effect_pool in (False,True):
  u,executions=prepare_recall(0 if no_effect_pool else 8);target=UNIT+0x150
  executions.append(call(u,0x4302C0,target,UNIT));effects=effect_snapshot(u)
  record('stock_recall_callback_positive_effect_pool_'+str(not no_effect_pool),dict(effect_pool_count=0 if no_effect_pool else 8,actual_target_type=0,actual_caster_type=71,occupied_caster_fit=True),[executions[-1]['return_eax'],r(u,0x665B68),sprite_position(u,target),r(u,target+0x4D,1),len(effects)],[0,1,[304,328],2,0 if no_effect_pool else 1],joined(executions),dict(units=snapshot(u),effects=effects))
  if effects:assert effects[0]['images'][0]['id']==391 and effects[0]['images'][0]['grp']!=0
 u,executions=prepare_recall();target=UNIT+0x150;w(u,0x665B68,55)
 executions.append(call(u,0x43A270,target,args=(400,400)))
 for at in (0x543230,0x543234):w(u,at,512,2)
 for at in (0x5264B4,0x5264B6):w(u,at,16,2)
 u.mem_write(0xC00000,bytes(16384*4));before=snapshot(u);executions.append(call(u,0x4304D0,target,args=(UNIT,),bound=2000000))
 record('stock_recall_fit_failure_restores_position',dict(target_type=0,all_tile_walk_flags_zero=True,map_pixels=[512,512],map_tiles=[16,16],initial_recalled_flag=55),[sprite_position(u,target),r(u,0x665B68),r(u,target+0x4D,1),len(chain(u,0x5C7A28)),r(u,0x5DA398,2)],[[400,400],55,2,0,60],joined(executions),dict(before=before,after=snapshot(u)))
 u,executions=prepare_recall();target=UNIT+0x150;executions.append(call(u,0x430390,target,args=(1,)));assert r(u,r(u,target+0xC)+0xE,1)&0x20
 executions.append(call(u,0x4304D0,target,args=(UNIT,)))
 record('stock_recall_direct_hidden_target_show_path',dict(target_type=0,actual_native_Hide_before_direct_helper=True,callback_would_reject_hidden=True),[sprite_position(u,target),r(u,0x665B68),r(u,r(u,target+0xC)+0xE,1)&0x20,chain(u,0x665AF8),len(chain(u,0x5C7A28))],[[300,300],1,0,[],1],joined(executions),dict(units=snapshot(u),effects=effect_snapshot(u)))
 print('Recall candidate guards, failure rollback and hidden reattach complete',flush=True)
 u,executions=prepare_recall();executions.append(call(u,0x42A340,UNIT,args=(137,600,600)));head=r(u,UNIT+0x74)
 record('stock_initial_Arbiter_gate_queues_Recall_without_activation',dict(original_stock_creation_and_completion=True,ready_gate_not_overridden=True),[r(u,UNIT+0x4D,1),int(bool(r(u,UNIT+0xDC)&0x1000)),int(bool(head)),r(u,head+8,1),r(u,0x5AEF08)],[130,1,1,137,1],joined(executions),snapshot(u))
 for energy in (0,150*256-1,150*256,150*256+1,200*256):
  for gathering in (False,True):
   u,executions=prepare_recall();w(u,UNIT+0xDC,r(u,UNIT+0xDC)&~0x1000);w(u,UNIT+0xA2,energy,2);w(u,0x5432FC,0x800 if gathering else 0)
   executions.append(call(u,0x42A340,UNIT,args=(137,600,600)));executions.append(call(u,0x42FC40,UNIT));accepted=gathering or energy>=150*256
   actual=[r(u,UNIT+0xA2,2),r(u,UNIT+0x4D,1),r(u,UNIT+0x4E,1),r(u,UNIT+0x54,1),len(chain(u,0x5C7A28))]
   expected=[energy if gathering or not accepted else energy-150*256,137 if accepted else 2,1 if accepted else 0,22 if accepted else 0,1 if accepted else 0]
   record('stock_recall_energy_boundary_and_Gathering',dict(initial_energy=energy,stock_cost=150*256,cheat_Gathering=gathering,ready_gate_clear_fixture=True,actual_stock_stat_TBL=True,notification_owner_nonlocal=True),actual,expected,joined(executions),dict(units=snapshot(u),effects=effect_snapshot(u)))
 u=new();environment(u,255);executions=[]
 for index,(typ,x,y)in enumerate(((71,300,300),(0,580,600),(0,600,600),(0,620,600))):
  at=UNIT+index*0x150;created=call(u,0x424900,typ,x,(y,3));assert created['return_eax']==at;executions.extend((created,call(u,0x424DA0,at)))
 w(u,0x543300,UNIT);w(u,0x543304,UNIT);w(u,UNIT+0xDC,r(u,UNIT+0xDC)&~0x1000);w(u,UNIT+0xA2,200*256,2)
 executions.append(call(u,0x42A340,UNIT,args=(137,600,600)));executions.append(call(u,0x42FC40,UNIT))
 for tick in range(22):executions.append(call(u,0x43A320,UNIT))
 executions.append(call(u,0x42FC40,UNIT));positions=[sprite_position(u,UNIT+i*0x150)for i in range(1,4)];effects=effect_snapshot(u);ext=tables['units'][0].unit_extents
 disjoint=all(abs(positions[i][0]-positions[j][0])>ext.left+ext.right or abs(positions[i][1]-positions[j][1])>ext.up+ext.down for i in range(3)for j in range(i+1,3))
 actual=[r(u,0x665B68),r(u,0x665B38),len(effects),r(u,UNIT+0x4D,1),r(u,UNIT+0xA2,2),int(all(max(abs(x-300),abs(y-300))<=64 for x,y in positions)),int(disjoint),[r(u,UNIT+i*0x150+0x5C)for i in range(1,4)],[r(u,UNIT+i*0x150+0x4D,1)for i in range(1,4)],int(all(e['images'][0]['id']==391 and e['images'][0]['grp']!=0 for e in effects))]
 record('stock_recall_full_multiple_candidate_composition',dict(caster_type=71,target_types=[0,0,0],old_target_positions=[[580,600],[600,600],[620,600]],destination=[300,300],native_timer_calls=22,ready_gate_clear_fixture=True,unit_pool_count=4,sprite_pool_count=8,image_pool_count=32,thingy_pool_count=8),actual,[1,4,4,2,50*256+22*8,1,1,[0,0,0],[2,2,2],1],joined(executions),dict(positions=positions,units=snapshot(u),effects=effects))
 print('Recall energy gates and multiple native candidates complete',flush=True)
 u=new();environment(u,255);executions=[call(u,0x40A0E0,0),call(u,0x424900,46,200,(200,3)),call(u,0x424DA0,UNIT)]
 w(u,0x543300,UNIT);w(u,0x543304,UNIT);w(u,UNIT+0x58,400,2);w(u,UNIT+0x5A,400,2);w(u,UNIT+0x5C,0)
 created=call(u,0x408F00,59,200,(200,3,64,UNIT));executions.append(created);assert created['return_eax']==0x53DB90
 timeline=[]
 for tick in range(20):
  w(u,0x543300,0);w(u,0x543304,0);executions.append(call(u,0x40A5F0,0));executions.append(call(u,0x418990,0));timeline.append(dict(tick=tick+1,bullets=bullets(u),swarm_completed=r(u,0x52C0C4+(202*12+11)*4)))
  if r(u,0x52C0C4+(202*12+11)*4):break
 assert tick<19,timeline
 field=UNIT+0x150;sp=r(u,field+0xC);im=r(u,sp+0x18)
 record('stock_DarkSwarm_weapon_delivery_creates_populated_field',dict(stock_source_type=46,stock_weapon=59,stock_weapon_explosion=13,projectile_constructor_called_directly=True,caster_energy_or_generic_order_not_probed_here=True,requested_position=[400,400],delivery_timeline=timeline),[r(u,field+0x64,2),r(u,field+0x4C,1),r(u,field+0x110,2),sprite_position(u,field),r(u,im+8,2),int(bool(r(u,im+0x2C))),r(u,0x52C0C4+(202*12+11)*4)],[202,11,900,[400,400],337,1,1],joined(executions),snapshot(u))
 print('Dark Swarm original projectile delivery complete',flush=True)
 cleanup_timeline=[]
 for tick in range(20):
  executions.append(call(u,0x40A5F0,0));executions.append(call(u,0x418990,0));cleanup_timeline.append(dict(tick=tick+1,bullet_count=r(u,0x53DB8C),free_bullets=len(chain(u,0x540758)),free_images=len(chain(u,0x544694)),free_sprites=len(chain(u,0x5C51C8))))
  if not r(u,0x53DB8C):break
 assert tick<19
 record('stock_DarkSwarm_delivering_projectile_recycles_after_effect',dict(delivery_ticks=len(timeline),cleanup_timeline=cleanup_timeline,stock_image=503,field_and_Defiler_remain=True),[r(u,0x53DB8C),len(chain(u,0x540758)),len(chain(u,0x544694)),len(chain(u,0x5C51C8)),r(u,0x665B38),r(u,field+0x110,2)],[0,100,29,6,2,900],joined(executions),snapshot(u))
 print('Dark Swarm delivering projectile recycling complete',flush=True)
 u,executions=prepare_recall();target=UNIT+0x150;enemy=UNIT+0x2A0
 created=call(u,0x424900,0,800,(800,4));assert created['return_eax']==enemy;executions.extend((created,call(u,0x424DA0,enemy)))
 executions.append(call(u,0x42A380,target,args=(10,enemy)));assert r(u,target+0x4D,1)==10 and r(u,target+0x5C)==enemy
 executions.append(call(u,0x4302C0,target,UNIT))
 record('stock_Recall_clears_actual_issued_AttackUnit_target',dict(caster_owner=3,recall_target_owner=3,attack_target_owner=4,actual_issued_order=10,actual_attack_target=enemy),[r(u,target+0x4D,1),r(u,target+0x5C),sprite_position(u,target),r(u,0x665B68),sprite_position(u,enemy)],[2,0,[304,328],1,[800,800]],joined(executions),dict(units=snapshot(u),effects=effect_snapshot(u)))
 print('Recall real issued AttackUnit target clear complete',flush=True)
 modules={Path(module.__file__).name:hashlib.sha256(Path(module.__file__).read_bytes()).hexdigest() for name,module in sorted(sys.modules.items()) if getattr(module,'__file__',None) and Path(module.__file__).parent.resolve()==Path(__file__).parent.resolve() and name!='__main__'}
 result=dict(target_sha256=local.TARGET_HASH,target_version='1.17.0.1',probe_script_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),probe_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),source_dependencies_sha256=modules,engine=dict(unicorn=unicorn.__version__,pefile=pefile.__version__,capstone=local.capstone.__version__,machine=platform.machine(),python=platform.python_version(),platform=platform.system()),fixture_contracts=dict(default_map_pixels=[4096,4096],default_map_tiles=[128,128],default_unit_pool_count=4,default_sprite_pool_count=8,default_image_pool_count=32,default_thingy_pool_count=8,original_sound_backend_disabled=True,frontend_command_admission_executed=False,InitArbiter_order_handler_executed=False),dat_sha256=dat_hashes,asset_manifest_sha256=hashlib.sha256(a.asset_manifest.read_bytes()).hexdigest(),helper_stubs=[],original_code_executed=True,complete_game_executed=False,instruction_bound_default=100000,instruction_bound_max=2000000,complete_original_function_calls=len(executed_stats),max_single_call_instructions=max(x['instruction_count']for x in executed_stats),total_original_instructions_executed=sum(x['instruction_count']for x in executed_stats),executed_entry_counts={entry:sum(x['entry']==entry for x in executed_stats)for entry in sorted({x['entry']for x in executed_stats})},visited_functions=sorted({f for x in executed_stats for f in x['visited_functions']}),reviewed_code_ranges=[dict(start=hex(lo),end_exclusive=hex(hi)) for lo,hi in sorted(set(ALLOW))],reviewed_dispatcher_witnesses=local.BLOCKS,complete_cases=len(cases),prefix_cases=0,all_passed=all(x['passed'] for x in cases),cases=cases)
 a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(dict(cases=len(cases),all_passed=result['all_passed'])))
if __name__=='__main__':main()
