#!/usr/bin/env python3
"""Probe selected natural remaining ability casts, effects and consuming contracts.

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
import probe_native_storm_117 as storm_source
import probe_native_support_casts_117 as support_source
import probe_native_hallucination_117 as hallucination_source
import inspect_native_ability_casts_117 as local
import inspect_native_image_geometry_117 as geometry_source
import inspect_native_campaign_attack_117 as campaign_source
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
ALLOW=list(hallucination_source.ALLOW)+list(local.BLOCKS.values())+[geometry_source.BLOCKS['image_follow_main_enable'],campaign_source.WITNESSES['native_stack_probe']]
PLAN=dict(target_sha256=local.TARGET_HASH,helper_stubs=[],instruction_bound=100000,scope='Natural Yamato113, Lockdown115, OpticalFlare185, Parasite120, Broodlings121 and Consume145 selected cast/effect/expiry contracts',limits='Private stock populated objects/resources, authentic network.tbl and flat synthetic terrain/regions. Natural caster regeneration, actual issuer/primary spell handler/Image VM/Bullet dispatch; selected original expiry, Restoration and death/pool consumers. Command admission, caster turning/travel, other families, renderer and complete game/campaign excluded')
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--exe',type=Path,required=True);p.add_argument('--data',type=Path,required=True);p.add_argument('--manifest',type=Path,required=True);p.add_argument('--assets',type=Path,required=True);p.add_argument('--asset-manifest',type=Path,required=True);p.add_argument('--pyms',type=Path,required=True);p.add_argument('--string-manifest',type=Path,required=True);p.add_argument('--out',type=Path);p.add_argument('--execute-original-code',action='store_true');p.add_argument('--families',nargs='+',choices=('Yamato','Lockdown','OpticalFlare','Parasite','Broodlings','Consume'),default=('Yamato','Lockdown','OpticalFlare','Parasite','Broodlings','Consume'));a=p.parse_args()
 raw=a.exe.read_bytes();assert hashlib.sha256(raw).hexdigest()==local.TARGET_HASH
 if not a.execute_original_code:print(json.dumps(PLAN,indent=2));return
 assert a.out and platform.machine().lower() in ('x86_64','amd64')
 assert subprocess.check_output(['git','-C',str(a.pyms),'rev-parse','HEAD'],text=True).strip()=='bfc5d3aad0b5614a5aff72c223f8efa00afddfa4'
 manifest=json.loads(a.manifest.read_text());assetmanifest=json.loads(a.asset_manifest.read_text());stringmanifest=json.loads(a.string_manifest.read_text());network=(a.assets/'rez/network.tbl').read_bytes();assert hashlib.sha256(network).hexdigest()==stringmanifest['assets']['rez\\network.tbl']['sha256'];dat_hashes={};assets={};lo_assets=[]
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
 from unicorn import Uc,UC_ARCH_X86,UC_MODE_32,UC_HOOK_CODE,UC_HOOK_MEM_INVALID
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
  u.mem_write(ASSETS+0x580000,stat_txt);w(u,0x6AB4F8,ASSETS+0x580000);assert len(network)<0x10000;u.mem_write(ASSETS+0x590000,network);w(u,0x665DE0,ASSETS+0x590000)
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
  w(u,0x508D50,0);w(u,0x67CC90,0);w(u,0x6726D0+3*36,2,1);w(u,0x6726D0+4*36,2,1)
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
   if addr==0x40EE7C and m.reg_read(UC_X86_REG_EBX)==0:
    at=m.reg_read(UC_X86_REG_EDX);ident=r(m,at+8,2)
    raise RuntimeError(f'Unbound native GRP Image={ident}, ImagePtr={at:#x}, nativeGRPTable={r(m,0x5436F8+ident*4):#x}')
   if addr==0x4104C4:
    pc=m.reg_read(UC_X86_REG_ESI);im=m.reg_read(UC_X86_REG_EBX);script_ops.append(dict(offset=pc-SCRIPT,opcode=r(m,pc,1),image_id=r(m,im+8,2),unit_context=r(m,0x543300),flingy_context=r(m,0x543304),bullet_context=r(m,0x543308)))
   if addr==0x4090E0:
    at=m.reg_read(UC_X86_REG_ECX);impacts.append(dict(bullet=at,weapon=r(m,at+0x60,1),flags=r(m,at+0x62,1),position=[r(m,at+0x28,2),r(m,at+0x2A,2)],source=r(m,at+0x64),target=r(m,at+0x5C),unit_context=r(m,0x543300),flingy_context=r(m,0x543304),bullet_context=r(m,0x543308)))
   if addr==0x409E15:
    at=m.reg_read(UC_X86_REG_EDI);initializations.append(dict(bullet=at,weapon=r(m,at+0x60,1),spawner=m.reg_read(UC_X86_REG_ESI),heading=r(m,at+0x21,1),unit_context=r(m,0x543300),flingy_context=r(m,0x543304),bullet_context=r(m,0x543308)))
   if addr in entries:
    sp=m.reg_read(UC_X86_REG_ESP);calls.append(dict(callee=hex(addr),ecx=m.reg_read(UC_X86_REG_ECX),edx=m.reg_read(UC_X86_REG_EDX),stack=[r(m,sp+4+i*4) for i in range(6)]))
  def invalid(m,access,address,width,value,_):
   raise RuntimeError(f'Invalid native memory access={access} address={address:#x} size={width}; EAX={m.reg_read(UC_X86_REG_EAX):#x} ECX={m.reg_read(UC_X86_REG_ECX):#x} EDX={m.reg_read(UC_X86_REG_EDX):#x}')
  bad=u.hook_add(UC_HOOK_MEM_INVALID,invalid)
  h=u.hook_add(UC_HOOK_CODE,hook);sp=STACK+0x3000;u.mem_write(sp,struct.pack('<'+'I'*(len(args)+1),STOP,*args));u.reg_write(UC_X86_REG_ESP,sp);u.reg_write(UC_X86_REG_ECX,ecx);u.reg_write(UC_X86_REG_EDX,edx)
  try:u.emu_start(start,STOP+1,count=bound)
  except Exception as e:raise RuntimeError(f'{e}; recent={recent[-30:]}; calls={calls[-10:]}') from e
  finally:u.hook_del(h);u.hook_del(bad)
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
   result['units'].append(dict(address=unit,type=r(u,unit+0x64,2),owner=r(u,unit+0x4C,1),status=r(u,unit+0xDC),timed_life=r(u,unit+0x110,2),buttons=r(u,unit+0x94,2),kill_count=r(u,unit+0x8F,1),hp=r(u,unit+8),shield=r(u,unit+0x60),energy=r(u,unit+0xA2,2),direction=r(u,unit+0x21,1),cooldowns=[r(u,unit+x,1) for x in (0x54,0x55,0x56,0x57)],movement_state=r(u,unit+0x97,1),live_counter=r(u,0x529604+(r(u,unit+0x64,2)*12+r(u,unit+0x4C,1))*4),completed_counter=r(u,0x52C0C4+(r(u,unit+0x64,2)*12+r(u,unit+0x4C,1))*4),wireframe_randomizer=r(u,unit+0xE1,1),generation=r(u,unit+0xA5,1),primary_order=r(u,unit+0x4D,1),secondary_order=r(u,unit+0xA6,1),build_time=r(u,unit+0xAC,2),queue=list(struct.unpack('<5H',u.mem_read(unit+0x98,10))),speed=r(u,unit+0x34),subunit=r(u,unit+0x70),sprite=dict(address=sp,position=[r(u,sp+0x14,2),r(u,sp+0x16,2)],elevation=r(u,sp+0xD,1),id=r(u,sp+8,2),flags=r(u,sp+0xE,1),owner=r(u,sp+0xA,1),visibility=r(u,sp+0xC,1),main=r(u,sp+0x18),images=images)))
  return result
 def record(name,inputs,actual,expected,ex,state=None):
  assert actual==expected,(name,actual,expected)
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
 def setup(u,typ=0,victim_type=0,positions=((200,200),(280,200)),victim_owner=3):
  environment(u,255);executions=[call(u,0x40A0E0,0)]
  for at,t,(x,y) in ((UNIT,typ,positions[0]),(UNIT+0x150,victim_type,positions[1])):
   created=call(u,0x424900,t,x,(y,3 if at==UNIT else victim_owner));assert created['return_eax']==at
   executions.extend((created,call(u,0x424DA0,at)))
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
 def sprite_position(uc,at):
  sp=r(uc,at+0xC);return [r(uc,sp+0x14,2),r(uc,sp+0x16,2)]
 def effect_snapshot(uc):
  result=[]
  for at in chain(uc,0x5C7A28):
   sp=r(uc,at+0xC);result.append(dict(address=at,sprite=sp,position=[r(uc,sp+0x14,2),r(uc,sp+0x16,2)],elevation=r(uc,sp+0xD,1),images=[dict(id=r(uc,im+8,2),grp=r(uc,im+0x2C),animation=r(uc,im+0x16,1),pc=r(uc,im+0x12,2),wait=r(uc,im+0x17,1))for im in chain(uc,sp+0x1C)]))
  return result
 def image_ids(uc,at):
  sp=r(uc,at+0xC)
  return [r(uc,im+8,2)for im in chain(uc,sp+0x1C)]if sp else []
 def retire_units(uc,addresses):
  tail=[];states=[]
  for at in addresses:
   assert r(uc,at+0x4D,1)==0
   w(uc,0x543300,at);w(uc,0x543304,at);tail.append(call(uc,0x439420,at));updates=0
   while r(uc,at+0xC):
    tail.append(call(uc,0x418B20,at));updates+=1;assert updates<=500
   states.append(dict(address=at,actual_body_image_updates=updates,pending_before_recycle=chain(uc,0x665AE8),effects=effect_snapshot(uc)))
   w(uc,0x543300,0);w(uc,0x543304,0);tail.append(call(uc,0x4256B0,at))
  services=0
  while r(uc,0x5C7A28):
   tail.append(call(uc,0x418990,0));services+=1;assert services<=2000
  return tail,states,services
 for family,typ,victim_type,order,cost,positions in (
  ('Yamato',12,12,113,150,((600,400),(800,450))),
  ('Lockdown',1,8,115,100,((600,400),(400,360))),
  ('OpticalFlare',34,8,185,75,((600,400),(400,360))),
  ('Parasite',45,8,120,75,((200,200),(250,400))),
  ('Broodlings',45,39,121,150,((200,200),(250,400))),
  ('Consume',46,37,145,0,((200,200),(216,224)))):
  if family not in a.families:continue
  u=new();executions=setup(u,typ,victim_type,positions=positions,victim_owner=3 if family=='Consume'else 4);target=UNIT+0x150
  for service in range(max(0,cost-50)*32):executions.append(call(u,0x43A320,UNIT))
  before=snapshot(u);executions.append(call(u,0x42A380,UNIT,args=(order,target)));executions.append(call(u,0x439420,UNIT));print(family,'start',r(u,UNIT+0x4D,1),r(u,UNIT+0x4E,1),r(u,UNIT+0xA2,2),'direction',r(u,UNIT+0x21,1),flush=True)
  timeline=[];effect=None
  for tick in range(200):
   w(u,0x543300,UNIT);w(u,0x543304,UNIT);executions.extend((call(u,0x43A320,UNIT),call(u,0x439420,UNIT),call(u,0x418B20,UNIT)));executions.append(call(u,0x43A320,target));w(u,0x543300,0);w(u,0x543304,0);executions.extend((call(u,0x40A5F0,0),call(u,0x418990,0)))
   row=dict(tick=tick+1,caster_order=r(u,UNIT+0x4D,1),caster_energy=r(u,UNIT+0xA2,2),target_hp=r(u,target+8),target_order=r(u,target+0x4D,1),Lockdown=r(u,target+0x117,1),parasite_mask=r(u,target+0x121,1),blind_mask=r(u,target+0x123,1),target_images=image_ids(u,target),bullets=bullets(u),units=snapshot(u)['units']);timeline.append(row)
   affected=(r(u,target+8)<before['units'][1]['hp'] if family=='Yamato' else r(u,target+0x117,1)>0 if family=='Lockdown' else r(u,target+0x123,1)>0 if family=='OpticalFlare' else r(u,target+0x121,1)>0 if family=='Parasite' else r(u,target+0x4D,1)==0)
   if effect is None and affected:effect=tick+1
   if effect is not None and not r(u,0x53DB8C) and r(u,UNIT+0x4D,1)in(2,3,175):break
  print(family,'effect',effect,'end',tick+1,flush=True)
  record('natural_'+family+'_cast',dict(before=before,order=order,cost=cost,first_effect=effect,timeline=timeline,declared_services=['caster43a320','caster439420','caster418b20','target43a320','Bullets40a5f0','Thingys418990'],full_game_frame=False),[effect,tick+1,r(u,0x53DB8C),r(u,UNIT+0xA2,2),r(u,target+8),r(u,target+0x117,1),r(u,target+0x121,1),r(u,target+0x123,1),r(u,UNIT+0x8F,1)],{'Yamato':[50,78,0,624,243*256,0,0,0,0],'Lockdown':[19,39,0,312,120*256,129,0,0,0],'OpticalFlare':[14,24,0,192,120*256,0,0,8,0],'Parasite':[15,16,0,128,120*256,0,8,0,0],'Broodlings':[15,16,0,128,400*256,0,0,0,1],'Consume':[36,40,0,25920,35*256,0,0,0,0]}[family],joined(executions),snapshot(u))
  if family=='Lockdown':
   phase=r(u,target+0x122,1);remaining=r(u,target+0x117,1);services=remaining*8-phase;tail=[]
   for service in range(services):tail.append(call(u,0x43A320,target))
   expiry_images=[dict(id=r(u,im+8,2),animation=r(u,im+0x16,1))for im in chain(u,r(u,target+0xC)+0x1C)]
   record('natural_Lockdown_actual_eight_service_expiry',dict(remaining_timer=remaining,phase=phase,actual_unit_timer_calls=services,expiry_images=expiry_images,full_game_frame=False),[r(u,target+0x117,1),r(u,target+0x94,2),r(u,target+0x122,1)],[0,8,0],joined(tail),snapshot(u))
   cleanup=[]
   for service in range(60):w(u,0x543300,target);w(u,0x543304,target);cleanup.append(call(u,0x418B20,target))
   record('natural_Lockdown_stock_overlay_VM_cleanup',dict(original_image_calls=60),int(362 in image_ids(u,target)),0,joined(cleanup),snapshot(u))
  if family in('Parasite','OpticalFlare'):
   at=0x121 if family=='Parasite'else 0x123;tail=[]
   for service in range(1024):tail.append(call(u,0x43A320,target))
   for service in range(100):w(u,0x543300,target);w(u,0x543304,target);tail.append(call(u,0x418B20,target))
   record('natural_'+family+'_mask_survives_periodic_and_stock_VM',dict(actual_unit_timer_calls=1024,actual_target_image_calls=100,no_duration_override=True,full_game_frame=False),[r(u,target+at,1),int(977 in image_ids(u,target))],[8,0],joined(tail),snapshot(u))
   restore=[call(u,0x4383E0,target)]
   for service in range(100):w(u,0x543300,target);w(u,0x543304,target);restore.append(call(u,0x418B20,target))
   record('natural_'+family+'_actual_Restoration_effect_clears_mask',dict(direct_complete_effect='0x4383e0',upstream_Medic_Restoration_cast_not_executed=True,stock_overlay_VM_calls=100),[r(u,target+0x121,1),r(u,target+0x123,1),int(968 in image_ids(u,target))],[0,0,0],joined(restore),snapshot(u))
  if family=='Broodlings':
   children=(UNIT+0x2A0,UNIT+0x3F0);tail=[];born=[[r(u,at+0x64,2),r(u,at+0x110,2),r(u,at+8),sprite_position(u,at)]for at in children]
   assert born==[[40,1800,30*256,[248,398]],[40,1800,30*256,[252,402]]],born
   for at in children:
    for service in range(1800):tail.append(call(u,0x43A320,at));assert r(u,at+0x110,2)==1799-service
   record('natural_Broodlings_actual_timedlife_issues_Death',dict(created_children=born,actual_unit_timer_calls_per_child=1800,per_child_calls_sequential=True,original_caster_kills=r(u,UNIT+0x8F,1),full_game_frame=False),[[r(u,at+0x110,2),r(u,at+0x4D,1)]for at in children],[[0,0],[0,0]],joined(tail),snapshot(u))
  if family in('Broodlings','Consume'):
   addresses=[target]+(list(children)if family=='Broodlings'else [])
   tail,states,thingy_services=retire_units(u,addresses)
   record('natural_'+family+'_selected_actual_Death_body_and_CUnit_return',dict(original_primary_Death='0x439420',original_body_VM='0x418b20',original_pending_recycler='0x4256b0',actual_Thingy_calls=thingy_services,retired=states,full_global438ef0_not_executed=True),[r(u,0x665B38),len(chain(u,0x665AE8)),len(chain(u,0x665AF0)),[r(u,at+0xC)for at in addresses]],[1,0,3,[0]*len(addresses)],joined(tail),snapshot(u))
   assert not r(u,0x5C7A28)
 if 'Broodlings'in a.families:
  for available in(0,1):
   u=new(unit_count=2+available);executions=setup(u,45,39,positions=((200,200),(250,400)),victim_owner=4);target=UNIT+0x150
   before=snapshot(u);executions.append(call(u,0x43E120,UNIT,args=(target,)));children=[UNIT+0x2A0+i*0x150 for i in range(available)]
   record('Broodlings_actual_effect_finite_Unit_pool_'+str(available),dict(free_Units_before_effect=available,direct_complete_effect='0x43e120',upstream_Queen_cast_not_executed=True,authentic_network_tbl_bound=True,before=before),[r(u,target+0x4D,1),r(u,UNIT+0x8F,1),r(u,0x665B38),[[r(u,at+0x64,2),r(u,at+0x110,2)]for at in children],r(u,0x531644+(39*12+4)*4),r(u,0x52EB84+(39*12+3)*4)],[0,1,2+available,[[40,1800]]*available,1,1],joined(executions),snapshot(u))
 if 'Consume'in a.families:
  for owner in(3,4):
   u=new();executions=setup(u,46,37,positions=((200,200),(216,224)),victim_owner=owner);target=UNIT+0x150
   for service in range(4800):executions.append(call(u,0x43A320,UNIT))
   assert r(u,UNIT+0xA2,2)==200*256
   before=snapshot(u);executions.append(call(u,0x43DB70,UNIT,args=(target,)))
   record('Consume_actual_effect_'+('caps_energy'if owner==3 else 'rejects_other_owner'),dict(target_owner=owner,direct_complete_effect='0x43db70',upstream_Defiler_cast_not_executed=True,actual_caster_regeneration_calls=4800,before=before),[r(u,UNIT+0xA2,2),r(u,target+0x4D,1),r(u,UNIT+0x8F,1),r(u,0x531644+(37*12+owner)*4)],[200*256,0 if owner==3 else 2,0,1 if owner==3 else 0],joined(executions),snapshot(u))
 if 'Yamato'in a.families:
  for mode,point in(('unaligned_wait',(400,360)),('Death_before_launch',(800,450))):
   u=new();executions=setup(u,12,12,positions=((600,400),point),victim_owner=4);target=UNIT+0x150
   for service in range(3200):executions.append(call(u,0x43A320,UNIT))
   before=snapshot(u);executions.append(call(u,0x42A380,UNIT,args=(113,target)));executions.append(call(u,0x439420,UNIT))
   if mode=='unaligned_wait':
    record('natural_Yamato_unaligned_initial_primary_waits',dict(before=before,target_point=list(point),no_movement_or_facing_services=True),[r(u,UNIT+0x4D,1),r(u,UNIT+0x4E,1),r(u,UNIT+0xA2,2),r(u,0x53DB8C)],[113,1,150*256,0],joined(executions),snapshot(u))
   else:
    assert r(u,UNIT+0xA2,2)==0 and r(u,0x53DB8C)==0
    executions.append(call(u,0x42A4D0,UNIT));tail,states,thingy_services=retire_units(u,[UNIT]);executions.extend(tail)
    record('natural_Yamato_Death_before_stock_projectile_launch',dict(before=before,actual_Death_issuer='0x42a4d0',retired=states,actual_Thingy_calls=thingy_services,no_damage_or_score_attribution_claim=True),[r(u,target+8),r(u,0x53DB8C),r(u,0x665B38),r(u,UNIT+0xC)],[500*256,0,1,0],joined(executions),snapshot(u))
 modules={Path(module.__file__).name:hashlib.sha256(Path(module.__file__).read_bytes()).hexdigest() for name,module in sorted(sys.modules.items()) if getattr(module,'__file__',None) and Path(module.__file__).parent.resolve()==Path(__file__).parent.resolve() and name!='__main__'}
 result=dict(target_sha256=local.TARGET_HASH,target_version='1.17.0.1',probe_script_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),source_dependencies_sha256=modules,engine=dict(unicorn=unicorn.__version__,pefile=pefile.__version__,capstone=local.capstone.__version__,machine=platform.machine(),python=platform.python_version(),platform=platform.system()),dat_sha256=dat_hashes,asset_manifest_sha256=hashlib.sha256(a.asset_manifest.read_bytes()).hexdigest(),string_asset_manifest_sha256=hashlib.sha256(a.string_manifest.read_bytes()).hexdigest(),network_tbl_sha256=hashlib.sha256(network).hexdigest(),helper_stubs=[],original_code_executed=True,complete_game_executed=False,stock_ability_contracts=[dict(order_id=o,weapon_id=orders_raw[2646+o],technology_id=orders_raw[2835+o],energy_points=struct.unpack_from('<H',tech_raw,264+orders_raw[2835+o]*2)[0],stock_weapon=vars(tables['weapons'][orders_raw[2646+o]]))for o in(113,115,120,121,145,185)],instruction_bound_default=100000,complete_original_function_calls=len(executed_stats),max_single_call_instructions=max(x['instruction_count']for x in executed_stats),total_original_instructions_executed=sum(x['instruction_count']for x in executed_stats),visited_functions=sorted({f for x in executed_stats for f in x['visited_functions']}),reviewed_code_ranges=[dict(start=hex(lo),end_exclusive=hex(hi)) for lo,hi in sorted(set(ALLOW))],complete_cases=len(cases),prefix_cases=0,all_passed=all(x['passed']for x in cases),cases=cases)
 a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(dict(cases=len(cases),all_passed=result['all_passed'])))
if __name__=='__main__':main()
