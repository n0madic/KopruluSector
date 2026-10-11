#!/usr/bin/env python3
"""Probe original Psi Storm casting, repeated damage and populated Bullet expiry.

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
import probe_native_spell_fields_117 as fields_source
import probe_native_spell_casting_117 as casting_source
import probe_native_status_effects_117 as status_source
import inspect_native_computer_combat_117 as splash_source
import inspect_native_storm_117 as local
import inspect_native_military_arrival_117 as arrival_source
import inspect_native_attack_orders_117 as attack_source
import inspect_native_movement_fsm_117 as movement_source
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
ALLOW=list(status_source.ALLOW)+list(local.BLOCKS.values())+[splash_source.BLOCKS['ground_splash_callback'],splash_source.BLOCKS['bullet_point_to_target_bounds_within_radius']]
PLAN=dict(target_sha256=local.TARGET_HASH,helper_stubs=[],instruction_bound=100000,scope='Natural HighTemplar142 cast and populated PsiStorm ground-over-time damage/expiry',limits='Private stock populated objects/resources and flat synthetic terrain/regions. Selected caster/target/Bullet/Image services in declared order; frontend command admission, renderer and complete native game/campaign excluded')
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
 script=stock('scripts\\iscript.bin');orders_raw=stock('arr\\orders.dat');orderrows=orders.stock_orders(orders_raw);tech_raw=stock('arr\\techdata.dat');stat_txt=stock('rez\\stat_txt.tbl');table_raw['orders']=orders_raw
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
  for key,base in (('units',0x5014F0),('flingy',0x501148),('sprites',0x501408),('images',0x5011A8),('weapons',0x501828),('orders',0x501278)):
   v=table_raw[key];offset=0
   for prop_index in range({'units':54,'flingy':7,'sprites':6,'images':14,'weapons':24,'orders':19}[key]):
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
 u=new();executions=setup(u,67,8,positions=((600,400),(400,400)));target=UNIT+0x150;initial_hp=r(u,target+8)
 initial_energy=r(u,UNIT+0xA2,2);assert initial_energy==50*256
 for service in range(800):executions.append(call(u,0x43A320,UNIT))
 assert r(u,UNIT+0xA2,2)==75*256
 executions.append(call(u,0x42A340,UNIT,args=(142,400,400)));executions.append(call(u,0x434460,UNIT));print('Storm startup',r(u,UNIT+0x4D,1),r(u,UNIT+0x4E,1),r(u,UNIT+0xA2,2),'direction',r(u,UNIT+0x21,1),flush=True);assert r(u,UNIT+0x4E,1)==2 and r(u,UNIT+0xA2,2)==0
 timeline=[]
 for tick in range(120):
  w(u,0x543300,UNIT);w(u,0x543304,UNIT);executions.append(call(u,0x43A320,UNIT));executions.append(call(u,0x439420,UNIT));executions.append(call(u,0x418B20,UNIT));executions.append(call(u,0x43A320,target));w(u,0x543300,0);w(u,0x543304,0);bullet_step=call(u,0x40A5F0,0);executions.append(bullet_step);executions.append(call(u,0x418990,0))
  timeline.append(dict(tick=tick+1,order=r(u,UNIT+0x4D,1),energy=r(u,UNIT+0xA2,2),bullets=bullets(u),target_hp=r(u,target+8),under_Storm_timer=r(u,target+0x11B,1),target_periodic_phase=r(u,target+0x122,1),impact_calls=bullet_step['impact_events']))
  if tick>4 and not r(u,0x53DB8C) and r(u,UNIT+0x4D,1)in(2,3):break
 assert tick<119,timeline
 print('Storm native HP',initial_hp,r(u,target+8),'ticks',tick+1,flush=True)
 record('stock_HighTemplar142_natural_cast_populated_Storm_damage_and_expiry',dict(stock_type=67,initial_stock_energy=initial_energy,native_regeneration_calls=800,stock_weapon=84,requested_point=[400,400],stock_target_type=8,source_owner=3,target_owner=3,no_caster_energy_status_order_override=True,native_damage=tables['weapons'][84].damage_amount,declared_services=['43a320 caster','439420 caster','418b20 caster','43a320 victim','40a5f0 bullets','418990 lone sprites'],timeline=timeline,full_game_frame=False),[r(u,UNIT+0x4D,1),r(u,UNIT+0xA2,2),r(u,target+8),r(u,0x53DB8C)],[3,8*(tick+1),initial_hp-8*14*256,0],joined(executions),snapshot(u))
 expiry=[];before=[r(u,target+0x11B,1),r(u,target+0x122,1)];assert before==[1,5],before
 for service in range(3):expiry.append(call(u,0x43A320,target))
 record('stock_Storm_final_per_target_gate_expires_after_Bullet_recycling',dict(native_Bullet_recycled=True,before_gate_and_periodic_phase=before,additional_actual_unit_timer_calls=3,full_game_frame=False),[r(u,target+0x11B,1),r(u,target+0x122,1),r(u,target+8),r(u,0x53DB8C)],[0,0,initial_hp-8*14*256,0],joined(expiry),snapshot(u))
 for phase in range(8):
  u=new();executions=setup(u,67,8,positions=((600,400),(400,400)));target=UNIT+0x150;initial_hp=r(u,target+8)
  for service in range(phase):executions.append(call(u,0x43A320,target))
  executions.append(call(u,0x42A340,UNIT,args=(142,400,400)));executions.append(call(u,0x408F00,84,400,(400,3,r(u,UNIT+0x21,1),UNIT)))
  assert r(u,0x53DB8C)==1
  timeline=[];model_timer=0;model_hp=initial_hp;expected_damage_services=[]
  for service in range(100):
   executions.append(call(u,0x43A320,target));w(u,0x543300,0);w(u,0x543304,0);bullet_step=call(u,0x40A5F0,0);executions.append(bullet_step);executions.append(call(u,0x418990,0))
   step=service+1
   if(phase+step)%8==0 and model_timer:model_timer-=1
   if 5<=step<=64 and(60-(step-4))%7==0 and not model_timer:model_timer=1;model_hp-=14*256;expected_damage_services.append(step)
   assert r(u,target+8)==model_hp,(phase,step,r(u,target+8),model_hp)
   timeline.append(dict(service=step,target_hp=r(u,target+8),under_Storm_timer=r(u,target+0x11B,1),phase=r(u,target+0x122,1),bullets=bullets(u),impact_calls=bullet_step['impact_events']))
   if not r(u,0x53DB8C):break
  assert service<99 and len(expected_damage_services)==8
  record('stock_Storm_direct_constructor_all_natural_periodic_phases',dict(initial_phase_generated_with_native_timer_calls=phase,stock_target_type=8,stock_constructor='0x408f00',frontend_cast_not_executed=True,stock_initial_image_wait_operand=5,native_damage_services_matching_independent_model=expected_damage_services,timeline=timeline,full_game_frame=False),[r(u,target+8),r(u,0x53DB8C)],[initial_hp-8*14*256,0],joined(executions),snapshot(u))
 u=new();environment(u,255);executions=[call(u,0x40A0E0,0)];caster2=UNIT+0x150;target=UNIT+2*0x150
 for at,typ,owner,x,y in((UNIT,67,3,600,400),(caster2,67,4,600,600),(target,8,3,400,400)):
  created=call(u,0x424900,typ,x,(y,owner));assert created['return_eax']==at;executions.extend((created,call(u,0x424DA0,at)))
 initial_hp=r(u,target+8)
 for caster,owner in((UNIT,3),(caster2,4)):
  w(u,0x543300,caster);w(u,0x543304,caster);executions.append(call(u,0x42A340,caster,args=(142,400,400)));executions.append(call(u,0x408F00,84,400,(400,owner,r(u,caster+0x21,1),caster)))
 assert r(u,0x53DB8C)==2
 timeline=[]
 for service in range(100):
  executions.append(call(u,0x43A320,target));w(u,0x543300,0);w(u,0x543304,0);bullet_step=call(u,0x40A5F0,0);executions.append(bullet_step);executions.append(call(u,0x418990,0));timeline.append(dict(service=service+1,target_hp=r(u,target+8),under_Storm_timer=r(u,target+0x11B,1),bullets=bullets(u),impact_calls=bullet_step['impact_events']))
  if not r(u,0x53DB8C):break
 assert service<99
 record('stock_two_populated_Storms_share_per_target_damage_gate',dict(stock_caster_types=[67,67],caster_owners=[3,4],stock_target_type=8,target_owner=3,actual_created_casters=True,actual_Bullet_constructors=2,frontend_cast_not_executed=True,timeline=timeline,full_game_frame=False),[r(u,target+8),r(u,0x53DB8C)],[initial_hp-8*14*256,0],joined(executions),snapshot(u))
 u=new();environment(u,255);executions=[call(u,0x40A0E0,0)];caster2=UNIT+0x150;target=UNIT+2*0x150
 for at,typ,owner,x,y in((UNIT,67,3,600,400),(caster2,67,4,600,600),(target,12,3,400,400)):
  created=call(u,0x424900,typ,x,(y,owner));assert created['return_eax']==at;executions.extend((created,call(u,0x424DA0,at)))
 initial_hp=r(u,target+8);w(u,0x543300,UNIT);w(u,0x543304,UNIT);executions.append(call(u,0x42A340,UNIT,args=(142,400,400)));executions.append(call(u,0x408F00,84,400,(400,3,r(u,UNIT+0x21,1),UNIT)))
 timeline=[];model_hp=initial_hp;model_timer=0;expected_damage_services=[]
 for service in range(100):
  step=service+1;executions.append(call(u,0x43A320,target));w(u,0x543300,0);w(u,0x543304,0);bullet_step=call(u,0x40A5F0,0);executions.append(bullet_step);executions.append(call(u,0x418990,0))
  if step%8==0 and model_timer:model_timer-=1
  for delay in(0,8):
   local_service=step-delay
   if 5<=local_service<=64 and(60-(local_service-4))%7==0 and not model_timer:model_timer=1;model_hp-=14*256;expected_damage_services.append(dict(service=step,constructor_delay=delay))
  assert r(u,target+8)==model_hp,(step,r(u,target+8),model_hp)
  if step==8:
   w(u,0x543300,caster2);w(u,0x543304,caster2);executions.append(call(u,0x42A340,caster2,args=(142,400,400)));executions.append(call(u,0x408F00,84,400,(400,4,r(u,caster2+0x21,1),caster2)))
  timeline.append(dict(service=step,target_hp=r(u,target+8),under_Storm_timer=r(u,target+0x11B,1),bullets=bullets(u),impact_calls=bullet_step['impact_events']))
  if step>8 and not r(u,0x53DB8C):break
 assert service<99 and len(expected_damage_services)==9
 record('stock_two_Storms_staggered_by_eight_services_extend_shared_damage_window',dict(stock_caster_types=[67,67],caster_owners=[3,4],stock_target_type=12,stock_target_armor=tables['units'][12].armor,second_constructor_after_native_services=8,actual_created_casters=True,frontend_cast_not_executed=True,native_damage_services_matching_independent_model=expected_damage_services,timeline=timeline,full_game_frame=False),[r(u,target+8),r(u,0x53DB8C)],[initial_hp-9*14*256,0],joined(executions),snapshot(u))
 modules={Path(module.__file__).name:hashlib.sha256(Path(module.__file__).read_bytes()).hexdigest() for name,module in sorted(sys.modules.items()) if getattr(module,'__file__',None) and Path(module.__file__).parent.resolve()==Path(__file__).parent.resolve() and name!='__main__'}
 result=dict(target_sha256=local.TARGET_HASH,target_version='1.17.0.1',probe_script_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),source_dependencies_sha256=modules,engine=dict(unicorn=unicorn.__version__,pefile=pefile.__version__,capstone=local.capstone.__version__,machine=platform.machine(),python=platform.python_version(),platform=platform.system()),dat_sha256=dat_hashes,asset_manifest_sha256=hashlib.sha256(a.asset_manifest.read_bytes()).hexdigest(),helper_stubs=[],original_code_executed=True,complete_game_executed=False,instruction_bound_default=100000,complete_original_function_calls=len(executed_stats),max_single_call_instructions=max(x['instruction_count']for x in executed_stats),total_original_instructions_executed=sum(x['instruction_count']for x in executed_stats),visited_functions=sorted({f for x in executed_stats for f in x['visited_functions']}),reviewed_code_ranges=[dict(start=hex(lo),end_exclusive=hex(hi)) for lo,hi in sorted(set(ALLOW))],complete_cases=len(cases),prefix_cases=0,all_passed=all(x['passed']for x in cases),cases=cases)
 a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(dict(cases=len(cases),all_passed=result['all_passed'])))
if __name__=='__main__':main()
