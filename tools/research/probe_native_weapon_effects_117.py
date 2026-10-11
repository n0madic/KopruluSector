#!/usr/bin/env python3
"""Probe stock multi-target weapon effects with populated original objects.

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
import inspect_native_weapon_effects_117 as local
import probe_native_computer_combat_117 as computer_source
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
UNIT=0x5DA3A0;SPRITE=0x803000;IMAGE=0x805000;STACK=0x900000;STOP=0xA00000;SCRIPT=0xD00000;ASSETS=0xE00000
SELECT=('live_unit_add','live_unit_account','unit_group_upgrade_effects','speed_from_flingy','acceleration_from_flingy','turn_from_flingy','speed_status_modifier','enable_speed_upgrade','iscript_speed_control_zero_guard','upgrade_level_get')
DEATH=('drop_powerup_zero_carry','clear_unit_ai','remove_military_ai_null','remove_town_ai_null','clear_town_resource_refs','clear_guard_ai_null','clear_queen_selection')
ALLOW=list(computer_source.ALLOW)+list(local.BLOCKS.values())
ALLOW += [admission.WITNESSES[k] for k in ('enqueue_training_unit','unit_technology_upgrade_check','cached_resource_stock_compare','actual_stock_debit','resource_feedback')]
PLAN=dict(target_sha256=local.TARGET_HASH,helper_stubs=[],instruction_bound=250000,scope='Stock multi-target weapon/explosion behavior and real pools',limits='Native-created populated units/images and real complete selected shot/effect calls; explicit readiness/positions and visibility. No whole accepted frame, renderer, host imports or helper substitutions.')

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--exe',type=Path,required=True);p.add_argument('--data',type=Path,required=True);p.add_argument('--manifest',type=Path,required=True);p.add_argument('--assets',type=Path,required=True);p.add_argument('--asset-manifest',type=Path,required=True);p.add_argument('--pyms',type=Path,required=True);p.add_argument('--out',type=Path);p.add_argument('--case-group',choices=('all','bounce','splash','line','pools','scarab'),default='all');p.add_argument('--execute-original-code',action='store_true');a=p.parse_args()
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
 script=stock('scripts\\iscript.bin');stat_txt=stock('rez\\stat_txt.tbl');table_raw['orders']=stock('arr\\orders.dat');orderrows=orders.stock_orders(table_raw['orders'])
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
 def new(unit_count=16,sprite_count=64,image_count=128,thingy_count=64,seed=12345,rng_enabled=True):
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
  u.mem_write(at,stat_txt);w(u,0x665de0,at);at+=(len(stat_txt)+4095)&~4095
  assert at<=ASSETS+0x600000
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
  visited=set();calls=[];script_ops=[];impacts=[];initializations=[];damage_events=[];instructions=0;boundary=False;recent=[];entries={lo for lo,hi in ALLOW}
  def hook(m,addr,width,_):
   nonlocal instructions,boundary
   if addr==STOP:boundary=True;m.emu_stop();return
   key=(addr,width)
   if key not in cache:cache[key]=tuple(lo for lo,hi in ALLOW if lo<=addr and addr+width<=hi)
   if not cache[key]:raise RuntimeError(f'Outside reviewed original code {addr:#x}; recent={recent[-15:]}')
   visited.update(cache[key]);instructions+=1;recent.append(hex(addr))
   if addr==0x4104C4:
    pc=m.reg_read(UC_X86_REG_ESI);im=m.reg_read(UC_X86_REG_EBX);script_ops.append(dict(offset=pc-SCRIPT,opcode=r(m,pc,1),next_byte=r(m,pc+1,1),image_id=r(m,im+8,2),unit_context=r(m,0x543300),flingy_context=r(m,0x543304),bullet_context=r(m,0x543308)))
   if addr==0x4090E0:
    at=m.reg_read(UC_X86_REG_ECX);sprite=r(m,at+0xc);impacts.append(dict(bullet=at,weapon=r(m,at+0x60,1),flags=r(m,at+0x62,1),position=[r(m,at+0x28,2),r(m,at+0x2A,2)],sprite_position=None if not sprite else [r(m,sprite+0x14,2),r(m,sprite+0x16,2)],source=r(m,at+0x64),target=r(m,at+0x5C),unit_context=r(m,0x543300),flingy_context=r(m,0x543304),bullet_context=r(m,0x543308)))
   if addr==0x409E15:
    at=m.reg_read(UC_X86_REG_EDI);initializations.append(dict(bullet=at,weapon=r(m,at+0x60,1),spawner=m.reg_read(UC_X86_REG_ESI),heading=r(m,at+0x21,1),unit_context=r(m,0x543300),flingy_context=r(m,0x543304),bullet_context=r(m,0x543308)))
   if addr==0x41dfc0:
    at=m.reg_read(UC_X86_REG_ECX);sp=m.reg_read(UC_X86_REG_ESP);arguments=[r(m,sp+4+i*4) for i in range(6)];damage_events.append(dict(target=at,raw_stack_arguments=arguments,damage=arguments[0],weapon=arguments[1]&255,divisor=arguments[2],direction=arguments[3]&255,attacker=arguments[4],attacking_player=arguments[5],hp_before=r(m,at+8),shields_before=r(m,at+0x60),status=r(m,at+0xdc)))
   if addr in entries:
    sp=m.reg_read(UC_X86_REG_ESP);entry=dict(callee=hex(addr),ecx=m.reg_read(UC_X86_REG_ECX),edx=m.reg_read(UC_X86_REG_EDX),stack=[r(m,sp+4+i*4) for i in range(6)])
    if addr==0x420030:
     wid=entry['stack'][0]&255;entry['weapon_creation_live_dat']=dict(weapon=wid,behavior=r(m,0x5257d0+wid,1),forward_offset=r(m,0x5260b0+wid,1),upward_offset=r(m,0x526138+wid,1))
    calls.append(entry)
  h=u.hook_add(UC_HOOK_CODE,hook);sp=STACK+0x3000;u.mem_write(sp,struct.pack('<'+'I'*(len(args)+1),STOP,*args));u.reg_write(UC_X86_REG_ESP,sp);u.reg_write(UC_X86_REG_ECX,ecx);u.reg_write(UC_X86_REG_EDX,edx)
  try:u.emu_start(start,STOP+1,count=250000)
  except Exception as e:
   reg_ebx=u.reg_read(UC_X86_REG_EBX);reg_ecx=u.reg_read(UC_X86_REG_ECX);reg_edx=u.reg_read(UC_X86_REG_EDX)
   extra=dict(ebx=hex(reg_ebx),ecx=hex(reg_ecx),edx=hex(reg_edx),unit_context=hex(r(u,0x543300)))
   if IMAGE<=reg_edx<IMAGE+32*0x40:extra['image']=dict(id=r(u,reg_edx+8,2),grp=hex(r(u,reg_edx+0x2c)),loaded_grp=hex(r(u,0x5436f8+r(u,reg_edx+8,2)*4)))
   raise RuntimeError(f'{e}; diagnostic={extra}; recent={recent[-30:]}; calls={calls[-10:]}') from e
  finally:u.hook_del(h)
  assert boundary and u.reg_read(UC_X86_REG_ESP)==sp+4*(len(args)+1),(hex(start),instructions,boundary,hex(u.reg_read(UC_X86_REG_ESP)),hex(sp),recent[-10:])
  return dict(complete_return=True,return_eax=u.reg_read(UC_X86_REG_EAX),instruction_count=instructions,calls=calls,iscript_opcodes=script_ops,bullet_initializations=initializations,impact_events=impacts,damage_events=damage_events,visited_functions=[hex(x) for x in sorted(visited)])
 def snapshot(u):
  parent=r(u,0x665B08+3*4);result=dict(owner_head=parent,unit_count=r(u,0x665B38),free_unit_head=r(u,0x665AF0),free_sprite_head=r(u,0x5C51C8),free_image_head=r(u,0x544694),hidden_head=r(u,0x665AF8),rng_state=r(u,0x67393C),pending_delete_head=r(u,0x665AE8),error=r(u,0x5DA398,2),rng_counters={str(k):r(u,0x673940+k*4) for k in (3,6,7,14,15,37)},units=[])
  for j in range(32):
   unit=UNIT+j*0x150
   if r(u,unit+0xC)==0:continue
   sp=r(u,unit+0xC);images=[];at=r(u,sp+0x1C)
   while at:
    images.append(dict(address=at,id=r(u,at+8,2),flags=r(u,at+0xC,2),direction=r(u,at+0xB,1),offset=list(struct.unpack('<2b',u.mem_read(at+0xE,2))),iscript=list(struct.unpack('<3H2B',u.mem_read(at+0x10,8))),base_frame=r(u,at+0x18,2),frame=r(u,at+0x1A,2),bounds=list(struct.unpack('<8h',u.mem_read(at+0x1C,16))),sprite=r(u,at+0x3C),grp=r(u,at+0x2C)))
    at=r(u,at+4);assert len(images)<128
   result['units'].append(dict(address=unit,type=r(u,unit+0x64,2),owner=r(u,unit+0x4C,1),status=r(u,unit+0xDC),hp=r(u,unit+8),shield=r(u,unit+0x60),energy=r(u,unit+0xA2,2),direction=r(u,unit+0x21,1),cooldowns=[r(u,unit+x,1) for x in (0x54,0x55,0x56,0x57)],movement_state=r(u,unit+0x97,1),live_counter=r(u,0x529604+(r(u,unit+0x64,2)*12+r(u,unit+0x4c,1))*4),completed_counter=r(u,0x52C0C4+(r(u,unit+0x64,2)*12+r(u,unit+0x4c,1))*4),wireframe_randomizer=r(u,unit+0xE1,1),generation=r(u,unit+0xA5,1),primary_order=r(u,unit+0x4D,1),secondary_order=r(u,unit+0xA6,1),build_time=r(u,unit+0xAC,2),queue=list(struct.unpack('<5H',u.mem_read(unit+0x98,10))),speed=r(u,unit+0x34),subunit=r(u,unit+0x70),sprite=dict(address=sp,id=r(u,sp+8,2),flags=r(u,sp+0xE,1),owner=r(u,sp+0xA,1),visibility=r(u,sp+0xC,1),main=r(u,sp+0x18),images=images)))
  return result
 def record(name,inputs,actual,expected,ex,state=None):
  if actual!=expected:a.out.with_suffix('.failed.json').write_text(json.dumps(dict(case=name,inputs=inputs,actual=actual,expected=expected,execution=ex,native_state=state,passed=False,probe_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest()),indent=2)+'\n')
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
  return dict(complete_return=all(x['complete_return'] for x in executions),complete_call_count=len(executions),return_eax=executions[-1]['return_eax'],instruction_count=sum(x['instruction_count'] for x in executions),calls=[c for x in executions for c in x['calls']],iscript_opcodes=[c for x in executions for c in x['iscript_opcodes']],bullet_initializations=[c for x in executions for c in x['bullet_initializations']],impact_events=[c for x in executions for c in x['impact_events']],damage_events=[c for x in executions for c in x['damage_events']],visited_functions=sorted({c for x in executions for c in x['visited_functions']}))
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
   turret=r(u,UNIT+0x70)
   if turret and r(u,turret+0x70)==UNIT:
    w(u,0x543300,turret);w(u,0x543304,turret);executions.append(call(u,0x418b20,turret))
   if r(u,UNIT+0x64,2)==103:executions.append(call(u,0x40a880,0))
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
  return dict(order=r(u,UNIT+0x4d,1),order_state=r(u,UNIT+0x4e,1),target=r(u,UNIT+0x5c),hp=r(u,UNIT+8),stim=r(u,UNIT+0x115,1),cooldowns=[r(u,UNIT+0x55,1),r(u,UNIT+0x56,1)],firebat_weapon_live_forward_offset=r(u,0x5260b0+25,1),queue=[dict(address=p,id=r(u,p+8,1),pos=[r(u,p+0xc,2),r(u,p+0xe,2)],target=r(u,p+0x10)) for p in chain(u,UNIT+0x74)],ai=ai_fields,snapshot=snapshot(u))
 def setup_many(u,typ,targets):
  executions=[environment(u,255),call(u,0x40a0e0,0)];w(u,0x6726d0+4*36,2,1);w(u,0x534914+3*12+3,1,1);w(u,0x534914+4*12+4,1,1)
  created=call(u,0x424900,typ,200,(200,3));assert created['return_eax']==UNIT;executions.extend((created,call(u,0x424da0,UNIT)));addresses=[]
  for target in targets:
   at=r(u,0x665af0);created=call(u,0x424900,target.get('type',0),target['x'],(target['y'],target.get('owner',4)));assert created['return_eax']==at;executions.extend((created,call(u,0x424da0,at)));addresses.append(at)
   if 'status_add' in target:w(u,at+0xdc,r(u,at+0xdc)|target['status_add'])
   if 'detection' in target:w(u,at+0xe4,target['detection'])
   if 'hp_fixture' in target:w(u,at+8,target['hp_fixture'])
  w(u,0x543300,UNIT);w(u,0x543304,UNIT);w(u,0x543308,0)
  return executions,addresses
 def fire(u,executions,victim,order=10):
  w(u,0x543300,UNIT);w(u,0x543304,UNIT);w(u,0x543308,0)
  executions.append(call(u,0x42a380,UNIT,args=(order,victim)));w(u,UNIT+0x21,64,1);w(u,UNIT+0x23,64,1);w(u,UNIT+0x85,0,1);executions.append(call(u,0x417190,r(u,UNIT+0xc),args=(64,)));executions.append(call(u,0x439420,UNIT))
  turret=r(u,UNIT+0x70)
  if turret and r(u,turret+0x70)==UNIT:
   w(u,0x543300,turret);w(u,0x543304,turret);executions.append(call(u,0x42a380,turret,args=(22,victim)));w(u,turret+0x21,64,1);w(u,turret+0x23,64,1);w(u,turret+0x85,0,1);executions.append(call(u,0x417190,r(u,turret+0xc),args=(64,)));executions.append(call(u,0x439420,turret))
 if a.case_group in ('all','bounce'):
  variants=[('three',[(280,200),(320,200),(360,200)],[9,3,1]),('one',[(280,200)],[9]),('two',[(280,200),(320,200)],[9,3]),('iteration_not_nearest',[(280,200),(250,200),(290,200)],[9,3,1]),('four',[(280,200),(320,200),(360,200),(400,200)],[9,3,1,0])]
  for label,positions,loss in variants:
   targets=[dict(x=x,y=y) for x,y in positions];u=new();executions,addresses=setup_many(u,43,targets);fire(u,executions,addresses[0]);timeline=[]
   for tick in range(50):image_and_bullet_step(u,executions);timeline.append(dict(tick=tick+1,hp=[r(u,at+8) for at in addresses],bullets=bullets(u)))
   record('stock_mutalisk_bounce_'+label,dict(source_unit=43,targets=targets,native_created_targets=addresses,main_attack_unit=True,image_steps=50),[r(u,at+8) for at in addresses],[(40-d)*256 for d in loss],joined(executions),dict(timeline=timeline,final=combat_state(u)))
  extra=[('square_corner',[dict(x=280,y=200),dict(x=370,y=290)],[9,3]),('outside_square',[dict(x=280,y=200),dict(x=400,y=200)],[9,0]),('same_owner_skip',[dict(x=280,y=200),dict(x=310,y=200,owner=3),dict(x=340,y=200)],[9,0,3]),('invulnerable_skip',[dict(x=280,y=200),dict(x=310,y=200,status_add=0x4000000),dict(x=340,y=200)],[9,0,3]),('undetected_cloak_skip',[dict(x=280,y=200),dict(x=310,y=200,status_add=0x100,detection=0),dict(x=340,y=200)],[9,0,3]),('detected_cloak',[dict(x=280,y=200),dict(x=310,y=200,status_add=0x100,detection=8),dict(x=340,y=200)],[9,3,1]),('air_secondary',[dict(x=280,y=200),dict(x=320,y=200,type=8)],[9,3])]
  for label,targets,loss in extra:
   u=new();executions,addresses=setup_many(u,43,targets);initial=[r(u,at+8) for at in addresses];fire(u,executions,addresses[0]);timeline=[]
   for tick in range(50):image_and_bullet_step(u,executions);timeline.append(dict(tick=tick+1,hp=[r(u,at+8) for at in addresses],bullets=bullets(u)))
   record('stock_mutalisk_bounce_'+label,dict(source_unit=43,targets=targets,native_created_targets=addresses,main_attack_unit=True,image_steps=50),[r(u,at+8) for at in addresses],[hp-d*256 for hp,d in zip(initial,loss)],joined(executions),dict(timeline=timeline,final=combat_state(u)))
 if a.case_group in ('all','splash'):
  for typ,distance in ((32,24),(68,48),(30,160)):
   targets=[dict(x=200+distance,y=200),dict(x=200+distance,y=220),dict(x=200+distance,y=240)];u=new();executions,addresses=setup_many(u,typ,targets)
   if typ in (30,68):
    for tick in range(64):image_and_bullet_step(u,executions)
   fire(u,executions,addresses[0],tables['units'][typ].attack_unit);timeline=[]
   for tick in range(40):image_and_bullet_step(u,executions);timeline.append(dict(tick=tick+1,hp=[r(u,at+8) for at in addresses],bullets=bullets(u)))
   if typ==32:
    assert r(u,0x5260b0+25,1)==80
    assert [c['weapon_creation_live_dat']['forward_offset'] for x in executions for c in x['calls'] if c['callee']=='0x420030']==[24,52,80]
   record('stock_splash_baseline_'+str(typ),dict(source_unit=typ,targets=targets,native_created_targets=addresses,main_attack_unit=True,image_steps=40,init_image_warmup_steps=64 if typ in (30,68) else 0,order_wait_fixture=0),[r(u,at+8) for at in addresses],[v*256 for v in ({32:[28,30,40],68:[10,25,40],30:[5,22.5,31.25]}[typ])],joined(executions),dict(timeline=timeline,final=combat_state(u)))
  for typ,dist,offsets,loss in ((68,48,(12,13,24,25,39,40),(30,15,15,7.5,7.5,0)),(30,160,(19,20,34,35,49,50),(35,17.5,17.5,8.75,8.75,0))):
   targets=[dict(x=200+dist,y=200)]+[dict(x=200+dist,y=200+v) for v in offsets];u=new();executions,addresses=setup_many(u,typ,targets)
   for tick in range(64):image_and_bullet_step(u,executions)
   fire(u,executions,addresses[0],tables['units'][typ].attack_unit)
   for tick in range(40):image_and_bullet_step(u,executions)
   record('stock_splash_radius_boundaries_'+str(typ),dict(source_unit=typ,targets=targets,inner_medium_outer_adjacent=True,init_image_warmup_steps=64),[r(u,at+8) for at in addresses],[(40-d)*256 for d in ([30 if typ==68 else 35]+list(loss))],joined(executions),combat_state(u))
  for typ,dist,damage in ((32,24,8),(68,48,30),(30,160,35)):
   for mode in ('own','allied','burrow_inner','burrow_outer','air','invulnerable','lethal'):
    other=dict(x=200+dist,y=200 if mode in ('own','allied','burrow_inner','air','invulnerable','lethal') else 230)
    if mode=='own':other['owner']=3
    if mode=='allied':other['owner']=5
    if mode.startswith('burrow'):other['status_add']=0x10
    if mode=='air':other['type']=8
    if mode=='invulnerable':other['status_add']=0x4000000
    if mode=='lethal':other['hp_fixture']=256
    targets=[dict(x=200+dist,y=200),other];u=new();executions,addresses=setup_many(u,typ,targets);initial=r(u,addresses[1]+8)
    if mode=='allied':w(u,0x6726d0+5*36,2,1);w(u,0x534914+3*12+5,1,1);w(u,0x534914+5*12+3,1,1)
    if typ in (30,68):
     for tick in range(64):image_and_bullet_step(u,executions)
    fire(u,executions,addresses[0],tables['units'][typ].attack_unit)
    for tick in range(40):image_and_bullet_step(u,executions)
    if mode=='own':other_hp=initial-(35*256 if typ==30 else 0)
    elif mode=='allied':other_hp=initial-({32:12,68:30,30:35}[typ])*256
    elif mode=='burrow_inner':other_hp=initial-({32:8,68:30,30:35}[typ])*256
    elif mode=='burrow_outer':other_hp=initial
    elif mode=='air':other_hp=initial-(30*256 if typ==68 else 0)
    elif mode=='invulnerable':other_hp=initial
    else:other_hp=0
    record('stock_splash_'+str(typ)+'_'+mode,dict(source_unit=typ,targets=targets,explicit_ally_bytes=mode=='allied'),[r(u,addresses[1]+8),r(u,UNIT+0x8f,1)],[other_hp,1 if mode=='lethal' else 0],joined(executions),dict(target_order=r(u,addresses[1]+0x4d,1),target_live_count=r(u,0x529604+(other.get('type',0)*12+other.get('owner',4))*4),final=combat_state(u)))
 if a.case_group in ('all','line'):
  targets=[dict(x=280,y=200),dict(x=320,y=200),dict(x=380,y=200),dict(x=340,y=240)];u=new();executions,addresses=setup_many(u,103,targets);w(u,UNIT+0xdc,r(u,UNIT+0xdc)|0x10);fire(u,executions,addresses[0],11);timeline=[]
  for tick in range(60):image_and_bullet_step(u,executions);timeline.append(dict(tick=tick+1,hp=[r(u,at+8) for at in addresses],bullets=bullets(u)))
  record('stock_lurker_line_baseline',dict(source_unit=103,targets=targets,native_created_targets=addresses,burrowed_readiness_fixture=True,main_attack_unit=11,image_and_history_steps=60),[r(u,at+8) for at in addresses],[v*256 for v in (20,20,20,40)],joined(executions),dict(timeline=timeline,final=combat_state(u)))
  for gap in (24,29,30,31,32):
   targets=[dict(x=x,y=200) for x in (280,320,380)];u=new();executions,addresses=setup_many(u,103,targets);w(u,UNIT+0xdc,r(u,UNIT+0xdc)|0x10);fire(u,executions,addresses[0],11)
   for tick in range(gap):image_and_bullet_step(u,executions)
   assert [r(u,at+8) for at in addresses]==[20*256]*3
   w(u,UNIT+0x55,0,1);fire(u,executions,addresses[0],11)
   for tick in range(20):image_and_bullet_step(u,executions)
   record('stock_lurker_second_shot_gap_'+str(gap),dict(source_unit=103,targets=targets,selected_step_gap=gap,second_ground_cooldown_readiness_fixture=0,burrowed_readiness_fixture=True),[r(u,at+8) for at in addresses],[0 if gap>=30 else 20*256]*3,joined(executions),combat_state(u))
  targets=[dict(x=280,y=200)]+[dict(x=240,y=200) for i in range(17)];u=new(unit_count=32);executions,addresses=setup_many(u,103,targets);w(u,UNIT+0xdc,r(u,UNIT+0xdc)|0x10);fire(u,executions,addresses[0],11)
  for tick in range(20):image_and_bullet_step(u,executions)
  hp=[r(u,at+8) for at in addresses];record('stock_lurker_history_slot_capacity',dict(source_unit=103,targets=targets,near_overlapping_targets=17,all_objects_native_created=True,burrowed_readiness_fixture=True),[hp[0],hp[1:].count(20*256),hp[1:].count(0),r(u,UNIT+0x8f,1)],[20*256,16,1,1],joined(executions),dict(hp_by_native_created_unit=list(zip(addresses,hp)),history_slot=r(u,0x541768),current_slot_pairs=r(u,0x54176c),final=combat_state(u)))
 if a.case_group in ('all','scarab'):
  for label,hp,selected in (('last_above_initial',[10,18,14],2),('earlier_above_initial',[10,18,9],1),('strict_tie',[10,10,9],0),('four_last_above_initial',[10,18,14,17],3)):
   u=new();targets=[dict(x=235,y=200)];executions,addresses=setup_many(u,83,targets)
   for tick in range(64):image_and_bullet_step(u,executions)
   w(u,0x5263d0+12,1000);w(u,0x526400+12,1000)
   for unused in hp:executions.append(call(u,0x41b4b0,UNIT,args=(85,)));assert executions[-1]['return_eax']==1
   executions.append(call(u,0x42a1e0,UNIT,args=(63,)));training_steps=0
   while r(u,UNIT+0xc8,1)<len(hp):
    executions.append(call(u,0x439310,UNIT));training_steps+=1;assert training_steps<=120*len(hp)
   inside=[];at=r(u,UNIT+0xc0)
   while at:inside.append(at);at=r(u,at+0xc4);assert len(inside)<=len(hp)
   assert len(inside)==len(hp)
   for at,value in zip(inside,hp):w(u,at+8,value*256)
   w(u,UNIT+0x21,64,1);w(u,0x543300,UNIT);w(u,0x543304,UNIT);executions.append(call(u,0x41ccd0,UNIT));launched=executions[-1]['return_eax']
   record('stock_reaver_hangar_choice_'+label,dict(source_unit=83,all_scarabs_native_paid_trained=True,training_calls=training_steps,inside_list_order=inside,hp_fixture_in_inside_order=hp,direct_native_launch=True),[launched,r(u,UNIT+0xc8,1),r(u,UNIT+0xc9,1),r(u,0x5263d0+12)],[inside[selected],len(hp)-1,1,1000-15*len(hp)],joined(executions),dict(selected_inside_index=selected,final=combat_state(u)))
  for label,profile in (('empty_hangar',{}),('empty_unit_pool',dict(unit_count=2))):
   targets=[dict(x=235,y=200)];u=new(**profile);executions,addresses=setup_many(u,83,targets)
   for tick in range(64):image_and_bullet_step(u,executions)
   if label=='empty_unit_pool':
    w(u,0x5263d0+12,1000);w(u,0x526400+12,1000);executions.append(call(u,0x41b4b0,UNIT,args=(85,)));assert executions[-1]['return_eax']==1;executions.append(call(u,0x42a1e0,UNIT,args=(63,)));executions.append(call(u,0x439310,UNIT));assert [r(u,UNIT+0xe2,1),r(u,UNIT+0xec),r(u,UNIT+0x98,2),r(u,0x5263d0+12)]==[1,0,85,985]
   fire(u,executions,addresses[0],59);w(u,UNIT+0x85,0,1);executions.append(call(u,0x439420,UNIT))
   record('stock_reaver_'+label,dict(source_unit=83,targets=targets,profile=profile,native_queue_admission=label=='empty_unit_pool'),[r(u,UNIT+0xc4),r(u,0x53db8c),r(u,addresses[0]+8),r(u,UNIT+0x4d,1)],[0,0,40*256,58],joined(executions),combat_state(u))
  targets=[dict(x=235,y=200),dict(x=250,y=240),dict(x=250,y=270)];u=new();executions,addresses=setup_many(u,83,targets)
  for tick in range(64):image_and_bullet_step(u,executions)
  w(u,0x5263d0+3*4,1000);w(u,0x526400+3*4,1000);executions.append(call(u,0x41b4b0,UNIT,args=(85,)));assert executions[-1]['return_eax']==1 and r(u,0x5263d0+3*4)==985;executions.append(call(u,0x42a1e0,UNIT,args=(63,)));executions.append(call(u,0x439310,UNIT));scarab=r(u,UNIT+0xec);assert scarab and r(u,scarab+0xc0)==UNIT
  training_steps=0
  while r(u,UNIT+0xec):
   executions.append(call(u,0x439310,UNIT));training_steps+=1;assert training_steps<=120
  assert r(u,UNIT+0xc0)==scarab and r(u,UNIT+0xc8,1)==1
  fire(u,executions,addresses[0],59);w(u,UNIT+0x85,0,1);executions.append(call(u,0x439420,UNIT));assert r(u,UNIT+0xc4)==scarab
  w(u,0x543300,scarab);w(u,0x543304,scarab);executions.append(call(u,0x439420,scarab));executions.append(call(u,0x439420,scarab));timeline=[]
  for tick in range(40):
   w(u,0x543300,scarab);w(u,0x543304,scarab);w(u,0x543308,0)
   if scarab not in chain(u,0x665ae8) and r(u,scarab+0xc):executions.append(call(u,0x439420,scarab))
   executions.append(call(u,0x418b20,scarab));w(u,0x543300,0);executions.append(call(u,0x40a5f0,0));executions.append(call(u,0x418990,0));timeline.append(dict(tick=tick+1,hp=[r(u,at+8) for at in addresses],bullets=bullets(u),scarab_order=r(u,scarab+0x4d,1),scarab_sprite=r(u,scarab+0xc),pending=chain(u,0x665ae8)))
  assert r(u,scarab+0xc)==0 and scarab in chain(u,0x665ae8) and not (r(u,scarab+0xdc)&0x4000)
  executions.append(call(u,0x4256b0,scarab));assert scarab in chain(u,0x665af0) and scarab not in chain(u,0x665ae8)
  record('stock_reaver_train_launch_scarab',dict(source_unit=83,queue_type=85,stock_training_steps=training_steps,targets=targets,native_created_targets=addresses,main_attack_unit=59,image_steps=40,direct_pending_recycler_after_native_null_sprite=True),[r(u,at+8) for at in addresses]+[r(u,UNIT+0x8f,1),r(u,UNIT+0xc9,1),r(u,0x665b38),r(u,0x5263d0+3*4)],[0,0,40*256,2,0,4,985],joined(executions),dict(timeline=timeline,scarab=scarab,final=combat_state(u)))
 if a.case_group in ('all','pools'):
  for label,profile,attempts,expected_count in (('native_bullet_cap',dict(sprite_count=128),101,100),('sprite_exhaustion',dict(sprite_count=3),2,1),('mandatory_image_exhaustion',dict(image_count=4),1,0)):
   u=new(**profile);targets=[dict(x=280,y=200)];executions,addresses=setup_many(u,43,targets);executions.append(call(u,0x42a380,UNIT,args=(10,addresses[0])));w(u,UNIT+0x21,64,1);returns=[];count_before_failure=None;seed_before_failure=None
   for i in range(attempts):
    if i==attempts-1:count_before_failure=r(u,0x53db8c);seed_before_failure=r(u,0x67393c)
    executions.append(call(u,0x420030,UNIT,args=(48,)));returns.append(r(u,0x53db8c))
   record('stock_weapon_pool_'+label,dict(profile=profile,weapon=48,direct_original_weapon_creation_calls=attempts,normal_attack_cadence_not_claimed=True),[r(u,0x53db8c),len(chain(u,0x540758)),r(u,0x53db8c)==count_before_failure,r(u,0x67393c)==seed_before_failure],[expected_count,100-expected_count,True,True],joined(executions),dict(bullet_counts_after_each_call=returns,final_bullets=bullets(u),final=snapshot(u)))
 modules={Path(module.__file__).name:hashlib.sha256(Path(module.__file__).read_bytes()).hexdigest() for name,module in sorted(sys.modules.items()) if getattr(module,'__file__',None) and Path(module.__file__).parent.resolve()==Path(__file__).parent.resolve() and name!='__main__'}
 result=dict(target_sha256=local.TARGET_HASH,probe_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),source_dependencies_sha256=modules,engine=dict(unicorn=unicorn.__version__,pefile=pefile.__version__,machine=platform.machine()),dat_sha256=dat_hashes,stock_weapon_rows=[dict(id=i,weapon=vars(tables['weapons'][i]),flingy=vars(tables['flingy'][tables['weapons'][i].graphics]),sprite=vars(tables['sprites'][tables['flingy'][tables['weapons'][i].graphics].sprite])) for i in (25,27,48,54,55,70,82,109)],asset_manifest_sha256=hashlib.sha256(a.asset_manifest.read_bytes()).hexdigest(),case_group=a.case_group,helper_stubs=[],instruction_bound=250000,reviewed_code_ranges=[dict(start=hex(lo),end_exclusive=hex(hi)) for lo,hi in sorted(set(ALLOW))],complete_cases=len(cases),prefix_cases=0,all_passed=all(x['passed'] for x in cases),cases=cases)
 a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(dict(cases=len(cases),all_passed=result['all_passed'])))
if __name__=='__main__':main()
