#!/usr/bin/env python3
"""Probe stock ranged IScript, projectile consumers and damage in private memory.

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
import inspect_native_projectiles_117 as local
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
ALLOW=list(active_source.ALLOW)+list(local.BLOCKS.values())
PLAN=dict(target_sha256=local.TARGET_HASH,helper_stubs=[],instruction_bound=100000,scope='Stock ranged IScript, actual bullet creation/motion/impact/recycling',limits='Private populated pools, stock DAT/IScript/GRP/LO input. Selected object steps use an explicit unit status gate, positive creep countdown, deferred slow AI and no FOW refresh. Original resource loading/rendering, campaign attack-order decisions and full-game execution excluded')
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--exe',type=Path,required=True);p.add_argument('--data',type=Path,required=True);p.add_argument('--manifest',type=Path,required=True);p.add_argument('--assets',type=Path,required=True);p.add_argument('--asset-manifest',type=Path,required=True);p.add_argument('--pyms',type=Path,required=True);p.add_argument('--out',type=Path);p.add_argument('--case-group',choices=('all','ranged','miss','pools','numeric','frame','tracking'),default='all');p.add_argument('--execute-original-code',action='store_true');a=p.parse_args()
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
 script=stock('scripts\\iscript.bin');orderrows=orders.stock_orders(stock('arr\\orders.dat'))
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
  for key,base in (('units',0x5014F0),('flingy',0x501148),('sprites',0x501408),('images',0x5011A8),('weapons',0x501828)):
   v=table_raw[key];offset=0
   for prop_index in range({'units':54,'flingy':7,'sprites':6,'images':14,'weapons':24}[key]):
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
   result['units'].append(dict(address=unit,type=r(u,unit+0x64,2),owner=r(u,unit+0x4C,1),status=r(u,unit+0xDC),hp=r(u,unit+8),shield=r(u,unit+0x60),energy=r(u,unit+0xA2,2),direction=r(u,unit+0x21,1),cooldowns=[r(u,unit+x,1) for x in (0x54,0x55,0x56,0x57)],movement_state=r(u,unit+0x97,1),live_counter=r(u,0x529604+(r(u,unit+0x64,2)*12+3)*4),completed_counter=r(u,0x52C0C4+(r(u,unit+0x64,2)*12+3)*4),wireframe_randomizer=r(u,unit+0xE1,1),generation=r(u,unit+0xA5,1),primary_order=r(u,unit+0x4D,1),secondary_order=r(u,unit+0xA6,1),build_time=r(u,unit+0xAC,2),queue=list(struct.unpack('<5H',u.mem_read(unit+0x98,10))),speed=r(u,unit+0x34),subunit=r(u,unit+0x70),sprite=dict(address=sp,id=r(u,sp+8,2),flags=r(u,sp+0xE,1),owner=r(u,sp+0xA,1),visibility=r(u,sp+0xC,1),main=r(u,sp+0x18),images=images)))
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
 if a.case_group in ('all','ranged'):
  for typ,animation,victim_type,weapon_id,hit_step,retire_step in ((0,2,0,0,3,18),(8,2,0,16,3,14),(8,3,8,15,10,30)):
   weapon=tables['weapons'][weapon_id];armor=tables['units'][victim_type]
   for attack_upgrade,defense_upgrade,hallu,invincible in [(x,y,False,False) for x in (0,1,3) for y in (0,1,3)]+[(0,0,True,False),(0,0,False,True)]:
    u=new();executions=setup(u,typ,victim_type)
    w(u,0x534590+3*46+weapon.damage_upgrade,attack_upgrade,1);w(u,0x534590+3*46+armor.armor_upgrade,defense_upgrade,1)
    if hallu:w(u,UNIT+0xDC,r(u,UNIT+0xDC)|0x40000000)
    if invincible:w(u,UNIT+0x150+0xDC,r(u,UNIT+0x150+0xDC)|0x4000000)
    executions.append(call(u,0x417220,r(u,UNIT+0xC),args=(animation,1)));timeline=[]
    for tick in range(48):image_and_bullet_step(u,executions);timeline.append(trace_row(u,tick+1))
    hit=max(128,(weapon.damage_amount+weapon.damage_bonus*attack_upgrade-armor.armor-defense_upgrade)*256)
    initial_hp=armor.hit_points.whole*256+armor.hit_points.fraction
    expected_hp=[initial_hp]*(hit_step-1)+[initial_hp-(0 if hallu or invincible else hit)]*(49-hit_step)
    assert all(b['flags']==(2 if hallu else 0) for t in timeline for b in t['bullets'])
    actual=[[t['victim_hp'] for t in timeline],next(t['tick'] for t in timeline if t['bullet_count']),next(t['tick'] for t in timeline if t['tick']>=hit_step and not t['bullet_count']),len(chain(u,0x540758)),len(chain(u,0x544694)),len(chain(u,0x5C51C8)),len(chain(u,0x5C7A20))]
    expected=[expected_hp,3 if typ==0 else 1,retire_step,100,28,6,8]
    record(f'stock_ranged_{typ}_{animation}_upgrades_{attack_upgrade}_{defense_upgrade}_hallu_{hallu}_invincible_{invincible}',dict(attacker_type=typ,victim_type=victim_type,animation=animation,weapon=weapon_id,attack_upgrade=attack_upgrade,defense_upgrade=defense_upgrade,hallucinated_source_status=hallu,invincible_target_status=invincible,steps=48,positions=[[200,200],[280,200]],order_decision_execution=False),actual,expected,joined(executions),timeline)
  print('stock ranged completed',flush=True)
 if a.case_group in ('all','ranged','tracking'):
  for move_target in (False,True):
   u=new(sprite_count=32,image_count=64,thingy_count=32);environment(u,255)
   executions=[call(u,0x40A0E0,0),call(u,0x424900,3,200,(200,3)),call(u,0x424DA0,UNIT),call(u,0x424900,8,280,(200,3)),call(u,0x424DA0,UNIT+0x2A0)]
   turret=UNIT+0x150;victim=UNIT+0x2A0;assert r(u,UNIT+0x70)==turret and r(u,turret+0x70)==UNIT
   w(u,turret+0x5C,victim);w(u,victim+0x7C,UNIT);w(u,0x543300,turret);w(u,0x543304,turret)
   executions.append(call(u,0x417220,r(u,turret+0xC),args=(3,1)));timeline=[]
   for tick in range(80):
    w(u,0x543300,turret);w(u,0x543304,turret);w(u,0x543308,0);executions.append(call(u,0x418B20,turret))
    w(u,0x543300,0);executions.append(call(u,0x40A5F0,0));executions.append(call(u,0x418990,0))
    if tick==0 and move_target:executions.append(call(u,0x40CC00,victim,args=(320*256,200*256)))
    timeline.append(dict(tick=tick+1,victim_hp=r(u,victim+8),bullets=bullets(u)))
   sources={b['source'] for t in timeline for b in t['bullets']}
   assert sources=={UNIT};assert all(b['state'] in (0,2,5) for t in timeline for b in t['bullets'])
   record(f'stock_goliath_air_two_missiles_target_moved_{move_target}',dict(attacker_type=3,turret_type=4,victim_type=8,animation=3,weapon=8,stock_damage_factor=2,stock_launch_spin=64,target_reposition_after_first_bullet_step=[320,200] if move_target else None,native_reposition_helper='0x40cc00',spatial_move_pipeline_executed=False,steps=80),[r(u,victim+8),max(len(t['bullets']) for t in timeline),r(u,0x53DB8C),len(chain(u,0x540758)),r(u,0x673940),r(u,0x673940+4)],[100*256,2,0,100,1,2],joined(executions),timeline)
  u=new();executions=setup(u,8,8);victim=UNIT+0x150;executions.append(call(u,0x417220,r(u,UNIT+0xC),args=(3,1)));timeline=[]
  for tick in range(48):
   image_and_bullet_step(u,executions)
   if tick==0:executions.append(call(u,0x40CC00,victim,args=(320*256,200*256)))
   timeline.append(trace_row(u,tick+1))
  first_hit=next(t for t in timeline if t['victim_hp']<120*256)
  record('stock_wraith_point_missile_target_repositioned',dict(attacker_type=8,victim_type=8,animation=3,weapon=15,stock_behavior=0,target_reposition_after_first_step=[320,200],native_reposition_helper='0x40cc00',spatial_move_pipeline_executed=False,steps=48),[r(u,victim+8),first_hit['tick'],first_hit['bullets'][0]['position'],r(u,r(u,victim+0xC)+0x14,2),r(u,0x53DB8C)],[100*256,10,[280,200],320,0],joined(executions),timeline)
  print('target-following and subunit attribution completed',flush=True)
 if a.case_group in ('all','miss'):
  seeds={}
  for candidate in range(20000):
   draw=(advance(candidate,9)>>16)&255
   if draw not in seeds:seeds[draw]=candidate
   if len(seeds)==256:break
  assert len(seeds)==256
  for cover in (False,True):
   for draw in (0,1,118,119,120,255):
    for air_source,air_target in ((False,False),(True,False),(False,True)):
     u=new(seed=seeds[draw]);executions=setup(u);victim=UNIT+0x150
     assert sum(r(u,0x673940+i*4) for i in range(50))==8
     w(u,UNIT+0x21,64,1);w(u,UNIT+0x23,64,1)
     if air_source:w(u,UNIT+0xDC,r(u,UNIT+0xDC)|4)
     if air_target:w(u,victim+0xDC,r(u,victim+0xDC)|4)
     if cover:w(u,0xC00000+(6*128+8)*4,0x1FF00|0x100000)
     chance=119 if cover and not air_target else 0
     executions.append(call(u,0x41FD10,UNIT,args=(victim,)));assert executions[-1]['return_eax']==chance
     executions.append(call(u,0x420030,UNIT,args=(0,)))
     b=bullets(u);assert len(b)==1;miss=draw<=chance
     before=b[0];image_and_bullet_step(u,executions,False)
     actual=[before['flags'],before['position'],r(u,victim+8),r(u,0x673940+4)]
     expected=[int(miss),[246 if miss else 276,200],40*256-(0 if miss else 6*256),1]
     record(f'marine_miss_cover_{cover}_draw_{draw}_air_{air_source}_{air_target}',dict(initial_seed=seeds[draw],predicted_ninth_draw_low_byte=draw,cover_flag_100000=cover,source_air_status_fixture=air_source,target_air_status_fixture=air_target,heading=64,expected_chance=chance,direct_native_weapon_issue=True,steps=1),actual,expected,joined(executions),dict(bullet_before=before,after=trace_row(u,1)))
  print('miss thresholds completed',flush=True)
 if a.case_group in ('all','pools'):
  u=new();environment(u,255);ex=call(u,0x40A0E0,0)
  expected=[0x53DB90]+[0x53DB90+i*0x70 for i in range(99,0,-1)]
  record('native_bullet_pool_initializer',dict(records=100,stride=0x70),[r(u,0x53DB8C),chain(u,0x540758),r(u,0x54075C),r(u,0x540750),r(u,0x540754)],[0,expected,expected[-1],0,0],ex)
  for limited_pool in ('sprites','images'):
   u=new(sprite_count=2 if limited_pool=='sprites' else 8,image_count=4 if limited_pool=='images' else 32);executions=setup(u)
   before=[r(u,0x540758),r(u,0x54075C),r(u,0x53DB8C),r(u,0x5C51C8),r(u,0x544694)]
   free_sprites_before=chain(u,0x5C51C8)
   executions.append(call(u,0x408F00,0,276,(200,3,64,UNIT)))
   after=[r(u,0x540758),r(u,0x54075C),r(u,0x53DB8C),r(u,0x5C51C8),r(u,0x544694)]
   expected_after=before[:]
   if limited_pool=='images':expected_after[3]=free_sprites_before[1]
   expected_sprites=free_sprites_before if limited_pool=='sprites' else [free_sprites_before[1],free_sprites_before[0]]+free_sprites_before[2:]
   record('bullet_allocation_exhausted_'+limited_pool,dict(exhausted_pool=limited_pool,actual_stock_units=2,weapon=0),[executions[-1]['return_eax'],after,chain(u,0x5C51C8)],[0,expected_after,expected_sprites],joined(executions),snapshot(u))
  u=new(sprite_count=108,image_count=128);executions=setup(u)
  for i in range(100):
   executions.append(call(u,0x408F00,0,276,(200,3,64,UNIT)));assert executions[-1]['return_eax']==expected[i]
   if i==79:
    native_before=[r(u,0x53DB8C),r(u,0x540758),r(u,0x67393C),r(u,0x5C51C8),r(u,0x544694)]
    executions.append(call(u,0x408F00,103,200,(200,3,64,UNIT)))
    native_after=[r(u,0x53DB8C),r(u,0x540758),r(u,0x67393C),r(u,0x5C51C8),r(u,0x544694)]
    record('weapon_103_shared_80_bullet_cap',dict(existing_stock_marine_bullets=80,free_bullets=20,attempt_weapon=103),[executions[-1]['return_eax'],native_after],[0,native_before],joined(executions),dict(active_count=len(chain(u,0x540750))))
  native_before=[r(u,0x53DB8C),r(u,0x540758),r(u,0x67393C),r(u,0x5C51C8),r(u,0x544694)]
  executions.append(call(u,0x408F00,0,276,(200,3,64,UNIT)))
  native_after=[r(u,0x53DB8C),r(u,0x540758),r(u,0x67393C),r(u,0x5C51C8),r(u,0x544694)]
  record('stock_bullet_pool_100_successes_then_failure',dict(actual_bullets=100,private_sprite_records=108,private_image_records=128,actual_stock_image=527),[executions[-1]['return_eax'],len(chain(u,0x540750)),native_after],[0,100,native_before],joined(executions))
  print('finite pools completed',flush=True)
 if a.case_group in ('all','frame'):
  for typ,animation,victim_type,preimage_steps in ((0,2,0,2),(8,2,0,0),(8,3,8,0)):
   u=new();executions=setup(u,typ,victim_type);victim=UNIT+0x150
   for at in (UNIT,victim):w(u,at+0xDC,r(u,at+0xDC)|0x400)
   executions.append(call(u,0x417220,r(u,UNIT+0xC),args=(animation,1)))
   for i in range(preimage_steps):executions.append(call(u,0x418B20,UNIT))
   w(u,0x673930,0);w(u,0x6C46D4,100);w(u,0x69B568,100);w(u,0x69B564,100);w(u,0x6663E4,100)
   w(u,0x6A2E70,0);w(u,0x6A5D68,0);w(u,0x6B1A94,0);w(u,0x6B1A98,1);w(u,0x5432F0,0)
   executions.append(call(u,0x445070,0))
   expected_hp=(34 if typ==0 else (40 if victim_type==0 else 120))*256
   expected_state=5 if typ==0 else 1
   record(f'complete_object_step_positive_bullet_{typ}_{animation}',dict(attacker_type=typ,victim_type=victim_type,animation=animation,preparatory_image_steps=preimage_steps,object_steps=1,unit_status_gate_400_fixture=True,creep_countdown=100,slow_ai_countdowns=[100,100],fow_countdown=100,resource_areas=0,script_list=0,flingy_list=0,regional_cursor=[0,1]),[r(u,victim+8),r(u,0x53DB8C),bullets(u)[0]['state'],r(u,0x6C46D4),r(u,0x69B568),r(u,0x69B564),r(u,0x6663E4),r(u,0x6B1A98),r(u,0x673930),r(u,0x543300),r(u,0x543304),r(u,0x543308)],[expected_hp,1,expected_state,99,99,99,99,334,0,0,0,0],joined(executions),dict(bullets=bullets(u),units=snapshot(u)))
  print('positive object steps completed',flush=True)
 if a.case_group in ('all','numeric'):
  u=new();executions=setup(u,8,0);executions.append(call(u,0x408F00,16,245,(200,3,64,UNIT)));bullet=executions[-1]['return_eax'];assert bullet==0x53DB90
  vectors=struct.unpack('<512i',pe.get_data(0x1026B0,2048))
  for heading in (0,1,31,32,63,64,96,127,128,160,192,224,255):
   for current,delta in ((0,-1),(0,0),(0,1),(1,-2),(100,-100),(100,-99),(100,100),(17066,1),(17067,1),(17066,2),(17067,-1),(100,20000)):
    w(u,bullet+0x4A,heading,1);w(u,bullet+0x38,current);w(u,bullet+0x40,(vectors[heading*2]*current)>>8);w(u,bullet+0x44,(vectors[heading*2+1]*current)>>8)
    ex=call(u,0x40BD10,bullet,args=(delta&0xFFFFFFFF,));expected_speed=min(17067,max(0,current+delta))
    expected=[expected_speed,((vectors[heading*2]*expected_speed)>>8)&0xFFFFFFFF,((vectors[heading*2+1]*expected_speed)>>8)&0xFFFFFFFF]
    record(f'flingy_acceleration_heading_{heading}_speed_{current}_delta_{delta}',dict(stock_weapon=16,heading=heading,shared_stock_bullet_fixture=True,current_speed_fixture=current,acceleration_delta_fixture=delta,max_speed=17067),[r(u,bullet+0x38),r(u,bullet+0x40),r(u,bullet+0x44)],expected,joined(executions+[ex]))
  for x,y in ((-1,200),(4096,200),(200,-1),(200,4096),(-1,-1),(4096,4096),(0,0),(4095,4095),(2048,2048)):
   w(u,bullet+0x28,x,2);w(u,bullet+0x2A,y,2);w(u,bullet+0x2C,x*256);w(u,bullet+0x30,y*256)
   position_ex=call(u,0x417330,r(u,bullet+0xC),args=(x&0xFFFFFFFF,y&0xFFFFFFFF))
   ex=call(u,0x40BAE0,bullet);expected_x=min(4095,max(0,x));expected_y=min(4095,max(0,y));sp=r(u,bullet+0xC)
   record(f'flingy_map_clamp_{x}_{y}',dict(shared_stock_bullet_fixture=True,x_fixture=x,y_fixture=y,map_pixels=[4096,4096],stock_weapon=16),[r(u,bullet+0x28,2),r(u,bullet+0x2A,2),r(u,bullet+0x2C),r(u,bullet+0x30),r(u,sp+0x14,2),r(u,sp+0x16,2)],[expected_x,expected_y,expected_x*256,expected_y*256,expected_x,expected_y],joined(executions+[position_ex,ex]))
  print('projectile numeric helpers completed',flush=True)
 modules={Path(module.__file__).name:hashlib.sha256(Path(module.__file__).read_bytes()).hexdigest() for name,module in sorted(sys.modules.items()) if getattr(module,'__file__',None) and Path(module.__file__).parent.resolve()==Path(__file__).parent.resolve() and name!='__main__'}
 result=dict(target_sha256=local.TARGET_HASH,probe_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),source_dependencies_sha256=modules,engine=dict(unicorn=unicorn.__version__,pefile=pefile.__version__,machine=platform.machine()),dat_sha256=dat_hashes,stock_weapon_rows=[dict(id=i,weapon=vars(tables['weapons'][i]),flingy=vars(tables['flingy'][tables['weapons'][i].graphics]),sprite=vars(tables['sprites'][tables['flingy'][tables['weapons'][i].graphics].sprite])) for i in (0,8,15,16)],asset_manifest_sha256=hashlib.sha256(a.asset_manifest.read_bytes()).hexdigest(),case_group=a.case_group,helper_stubs=[],instruction_bound=100000,reviewed_code_ranges=[dict(start=hex(lo),end_exclusive=hex(hi)) for lo,hi in sorted(set(ALLOW))],complete_cases=len(cases),prefix_cases=0,all_passed=all(x['passed'] for x in cases),cases=cases)
 a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(dict(cases=len(cases),all_passed=result['all_passed'])))
if __name__=='__main__':main()
