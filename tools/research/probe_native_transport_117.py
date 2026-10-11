#!/usr/bin/env python3
"""Probe natural transport boarding, cargo and unloading.

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
import inspect_native_transport_117 as local
import probe_native_carrier_117 as carrier_source
import probe_native_subunit_scarab_117 as subunit_source
import inspect_native_requirements_117 as unit_requirements
import probe_native_weapon_effects_117 as weapon_source
import probe_native_movement_fsm_117 as motion_source
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
ALLOW=list(carrier_source.ALLOW)+list(local.BLOCKS.values())
ALLOW += [admission.WITNESSES[k] for k in ('enqueue_training_unit','unit_technology_upgrade_check','cached_resource_stock_compare','actual_stock_debit','resource_feedback')]
PLAN=dict(target_sha256=local.TARGET_HASH,helper_stubs=[],instruction_bound=250000,scope='Natural transport boarding, cargo and unloading',limits='Native-created populated units/images, real 445070 object callbacks, explicit terrain/visibility/passive-target and auxiliary-countdown fixtures. No outer accepted frame, renderer, host imports or helper substitutions.')

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--exe',type=Path,required=True);p.add_argument('--data',type=Path,required=True);p.add_argument('--manifest',type=Path,required=True);p.add_argument('--assets',type=Path,required=True);p.add_argument('--asset-manifest',type=Path,required=True);p.add_argument('--string-manifest',type=Path,required=True);p.add_argument('--pyms',type=Path,required=True);p.add_argument('--out',type=Path);p.add_argument('--case-group',choices=('all','transport','capacity','gates','failures','death','ownership'),default='all');p.add_argument('--execute-original-code',action='store_true');a=p.parse_args()
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
   elif row['kind']==5:w(u,0x59b210+row['image_id']*4,allocated[row['path']])
  u.mem_write(at,stat_txt);w(u,0x6ab4f8,at);at+=(len(stat_txt)+4095)&~4095
  u.mem_write(at,network);w(u,0x665de0,at);at+=(len(network)+4095)&~4095
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
  u.mem_write(0x543228,struct.pack('<4H',0,0,4096,4096))
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
   at=r(u,0x665af0);created=call(u,0x424900,target.get('type',0),target['x'],(target['y'],target.get('owner',4)));assert created['return_eax']==at;executions.extend((created,call(u,0x424da0,at),call(u,0x42a2c0,at,args=(23,))));addresses.append(at)
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
 def clear_world(u):
  normalize=environment(u,255)
  w(u,0xb00000,2,2);u.mem_write(0xb0000c,struct.pack('<H',1)*65536)
  region=0xb00000+0x449fc+64;w(u,region,0,2);w(u,region+2,1,2);w(u,region+0xc,region+0x2c)
  pool=0x80c000;w(u,0x69b574,pool);w(u,0x69b570,pool);w(u,pool,0)
  return normalize
 def setup_world(u,typ,targets):
  executions=[clear_world(u),call(u,0x40a0e0,0)];w(u,0x6726d0+4*36,2,1);w(u,0x534914+3*12+3,1,1);w(u,0x534914+4*12+4,1,1)
  created=call(u,0x424900,typ,200,(200,3));assert created['return_eax']==UNIT;executions.extend((created,call(u,0x424da0,UNIT)));addresses=[]
  for target in targets:
   at=r(u,0x665af0);created=call(u,0x424900,target.get('type',0),target['x'],(target['y'],target.get('owner',4)));assert created['return_eax']==at;executions.extend((created,call(u,0x424da0,at),call(u,0x42a2c0,at,args=(23,))));addresses.append(at)
  w(u,0x543300,UNIT);w(u,0x543304,UNIT);w(u,0x543308,0)
  return executions,addresses
 def gate_auxiliary_services(u):
  for at in (0x6c46d4,0x69b568,0x69b564,0x6663e4):w(u,at,1000)
  w(u,0x6a2e70,0);w(u,0x6a5d68,0);w(u,0x6b1a94,0);w(u,0x6b1a98,1);w(u,0x5432f0,0)
 def position(u,unit):
  sprite=r(u,unit+0xc);pixel=list(struct.unpack('<2H',u.mem_read(unit+0x28,4)));fixed=list(struct.unpack('<2I',u.mem_read(unit+0x2c,8)))
  if sprite:assert pixel==list(struct.unpack('<2H',u.mem_read(sprite+0x14,4))) and pixel==[v>>8 for v in fixed]
  return dict(unit=unit,pixel=pixel,fixed=fixed,sprite=sprite,status=r(u,unit+0xdc),order=r(u,unit+0x4d,1),order_state=r(u,unit+0x4e,1),target=r(u,unit+0x5c),movement_state=r(u,unit+0x97,1),speed=r(u,unit+0x38),heading=r(u,unit+0x21,1),path=r(u,unit+0x100),lifetime=r(u,unit+0x110,2),order_timer=r(u,unit+0x54,1),hangar_pointer_c0=r(u,unit+0xc0),outside_flag_cc=r(u,unit+0xcc,1))
 def cargo(u,transport):
  packed=list(struct.unpack('<8H',u.mem_read(transport+0xb0,16)));live=[]
  for slot in packed:
   if slot:
    index=(slot&0x7ff)-1;unit=UNIT+index*0x150
    if r(u,unit+0xc) and not(r(u,unit+0x4d,1)==0 and r(u,unit+0x4e,1)==1) and r(u,unit+0xa5,1)==slot>>11:live.append(unit)
  return dict(packed=packed,live=live)
 def transport_state(u,transport,passengers):
  return dict(transport=position(u,transport),cargo=cargo(u,transport),passengers=[dict(position=position(u,p),connected=r(u,p+0x80),sprite_flags=r(u,r(u,p+0xc)+0xe,1) if r(u,p+0xc) else None,cooldowns=[r(u,p+off,1) for off in (0x54,0x55,0x56,0x57)],generation=r(u,p+0xa5,1)) for p in passengers],active=chain(u,0x665ae0),hidden=chain(u,0x665af8),pending=chain(u,0x665ae8),unit_count=r(u,0x665b38))
 def object_step(u,executions):
  w(u,0x52651c,r(u,0x52651c)+1);executions.append(call(u,0x445070,0));assert r(u,0x543300)==r(u,0x543304)==r(u,0x543308)==0
 def prepared_transport(u,typ=11):
  executions,_=setup_world(u,typ,[])
  for tick in range(64):image_and_bullet_step(u,executions)
  gate_auxiliary_services(u);return executions
 def create_passenger(u,executions,typ=0,owner=3):
  p=r(u,0x665af0);ex=call(u,0x424900,typ,220,(200,owner));assert ex['return_eax']==p;executions.extend((ex,call(u,0x424da0,p),call(u,0x42a2c0,p,args=(23,))));return p
 def enter_direct(u,executions,p):
  executions.append(call(u,0x42a380,p,args=(92,UNIT)));w(u,0x543300,p);w(u,0x543304,p);w(u,0x543308,0);executions.append(call(u,0x4376d0,p))
 if a.case_group in ('all','capacity'):
  for typ,passenger_type,capacity in ((11,0,8),(69,2,4),(11,5,2),(42,0,8)):
   u=new();executions=prepared_transport(u,typ);passengers=[]
   if typ==42:w(u,0x534590+3*46+24,1,1)
   for j in range(capacity):
    p=create_passenger(u,executions,passenger_type);enter_direct(u,executions,p);passengers.append(p);assert r(u,p+0xdc)&0x40,dict(typ=typ,p=p,state=transport_state(u,UNIT,passengers))
   extra=create_passenger(u,executions,passenger_type);before=cargo(u,UNIT)['packed'];ex=call(u,0x437440,extra,args=(UNIT,));executions.append(ex);enter_direct(u,executions,extra)
   used=call(u,0x438350,UNIT);executions.append(used);space=call(u,0x4382f0,UNIT);executions.append(space)
   record('stock_transport_real_capacity_'+str(typ)+'_'+str(passenger_type),dict(transport=typ,passenger_type=passenger_type,native_created_passenger_count=capacity+1,upgrade24_explicit=int(typ==42),direct_real_enter92_consumer=True,no_cargo_link_or_status_fixture=True),[ex['return_eax'],used['return_eax'],space['return_eax'],len(cargo(u,UNIT)['live']),bool(r(u,extra+0xdc)&0x40),cargo(u,UNIT)['packed']==before],[0,8,0,capacity,False,True],joined(executions),dict(final=transport_state(u,UNIT,passengers+[extra])))
 if a.case_group in ('all','gates'):
  for typ,owner,upgrade,expected in ((11,4,0,False),(42,3,0,False),(42,3,1,True)):
   u=new();executions=prepared_transport(u,typ);p=create_passenger(u,executions,owner=owner)
   if upgrade:w(u,0x534590+3*46+24,1,1)
   enter_direct(u,executions,p)
   record('stock_transport_enter_owner_upgrade_'+str((typ,owner,upgrade)),dict(transport=typ,passenger_owner=owner,upgrade24_fixture=upgrade,direct_real_enter92_consumer=True,stock_unit_capacity=True),[bool(r(u,p+0xdc)&0x40),r(u,p+0x80),cargo(u,UNIT)['live']],[expected,UNIT if expected else 0,[p] if expected else []],joined(executions),dict(final=transport_state(u,UNIT,[p])))
  for field,value in ((0x54,1),(0xdc,0x400),(0x117,1),(0x119,1),(0x124,1)):
   u=new();executions=prepared_transport(u);p=create_passenger(u,executions);enter_direct(u,executions,p);assert r(u,p+0xdc)&0x40
   before=transport_state(u,UNIT,[p]);w(u,UNIT+field,r(u,UNIT+field)|value if field==0xdc else value,4 if field==0xdc else 1)
   ex=call(u,0x437e00,p);executions.append(ex)
   record('stock_transport_loaded_unload_guard_'+hex(field),dict(field=hex(field),value=value,disabled_or_timer_field_explicit_fixture=True,natural_original_enter92_before_guard=True,complete_direct_unload_helper=True),[ex['return_eax'],cargo(u,UNIT)['packed'],r(u,p+0x80),position(u,p)['pixel']],[0,before['cargo']['packed'],UNIT,before['passengers'][0]['position']['pixel']],joined(executions),dict(final=transport_state(u,UNIT,[p])))
 if a.case_group in ('all','failures'):
  u=new();executions=prepared_transport(u);p=create_passenger(u,executions);enter_direct(u,executions,p);assert r(u,p+0xdc)&0x40;before=transport_state(u,UNIT,[p])
  u.mem_write(0xc00000,bytes(16384*4));ex=call(u,0x437e00,p);executions.append(ex)
  record('stock_transport_loaded_unload_unwalkable_failure',dict(original_enter92_produces_load=True,whole_map_tile_flags_zero_explicit_unwalkable_fixture=True,complete_direct_unload_helper=True),[ex['return_eax'],bool(r(u,p+0xdc)&0x40),r(u,p+0x80),cargo(u,UNIT)['packed'],position(u,p)['pixel']],[0,True,UNIT,before['cargo']['packed'],[200,200]],joined(executions),dict(final=transport_state(u,UNIT,[p])))
  u.mem_write(0xc00000,struct.pack('<I',0x1ff00)*16384);ex=call(u,0x437e00,p);executions.append(ex)
  record('stock_transport_placement_failure_then_real_unload_retry',dict(original_enter92_produces_load=True,failed_attempt_before_retry=True,whole_map_tile_flags_restored_explicit_fixture=True,complete_direct_unload_helpers=True,no_cargo_status_or_position_override=True),[ex['return_eax'],bool(r(u,p+0xdc)&0x40),r(u,p+0x80),cargo(u,UNIT)['packed']],[1,False,0,[0]*8],joined(executions),dict(final=transport_state(u,UNIT,[p])))
  u=new();executions=prepared_transport(u);p=create_passenger(u,executions);enter_direct(u,executions,p);u.mem_write(0xc00000,bytes(16384*4));executions.append(call(u,0x42a2c0,UNIT,args=(111,)));object_step(u,executions)
  record('stock_transport_human_unload_order_failure_returns_idle',dict(original_enter92_produces_load=True,whole_map_tile_flags_zero_explicit_fixture=True,whole_object_unload111_consumer=True,no_parent_main_state_or_timer_override=True),[r(u,UNIT+0x4d,1),bool(r(u,p+0xdc)&0x40),r(u,p+0x80),cargo(u,UNIT)['live']],[93,True,UNIT,[p]],joined(executions),dict(final=transport_state(u,UNIT,[p])))
 if a.case_group in ('all','death'):
  for victim in ('transport','passenger'):
   u=new();executions=prepared_transport(u);p=create_passenger(u,executions);enter_direct(u,executions,p);assert r(u,p+0xdc)&0x40;executions.append(call(u,0x42a4d0,UNIT if victim=='transport' else p));timeline=[]
   for tick in range(120):
    object_step(u,executions);timeline.append(dict(tick=tick+1,state=transport_state(u,UNIT,[p])))
    if r(u,0x665b38)==(0 if victim=='transport' else 1):break
   record('stock_transport_naturally_loaded_death_'+victim,dict(actual_enter92_before_death=True,death_issued_to=victim,full_object_image_pending_recycling=True,no_cargo_death_fixture=True),[r(u,0x665b38),chain(u,0x665ae8),len(chain(u,0x665af0))],[0 if victim=='transport' else 1,[],16 if victim=='transport' else 15],joined(executions),dict(timeline=timeline,final=snapshot(u)))
 if a.case_group in ('all','ownership'):
  u=new();executions=prepared_transport(u);p=create_passenger(u,executions);enter_direct(u,executions,p);assert r(u,p+0xdc)&0x40
  before=dict(live=[[r(u,0x529604+(typ*12+owner)*4) for owner in (3,4)] for typ in (11,0)],completed=[[r(u,0x52c0c4+(typ*12+owner)*4) for owner in (3,4)] for typ in (11,0)],terran_supply=[r(u,0x5294e4+owner*4) for owner in (3,4)],owner_lists=[chain(u,0x665b08+owner*4,0x6c) for owner in (3,4)])
  executions.append(call(u,0x4b81a0,0,args=(UNIT,4)))
  after=dict(live=[[r(u,0x529604+(typ*12+owner)*4) for owner in (3,4)] for typ in (11,0)],completed=[[r(u,0x52c0c4+(typ*12+owner)*4) for owner in (3,4)] for typ in (11,0)],terran_supply=[r(u,0x5294e4+owner*4) for owner in (3,4)],owner_lists=[chain(u,0x665b08+owner*4,0x6c) for owner in (3,4)])
  assert before==dict(live=[[1,0],[1,0]],completed=[[1,0],[1,0]],terran_supply=[6,0],owner_lists=[[p,UNIT],[]]),before
  record('stock_transport_loaded_actual_give_transfer',dict(original_enter92_before_give=True,complete_actual_trigger_give=True,new_owner4=True,no_passenger_owner_or_cargo_fixture=True,before=before),[r(u,UNIT+0x4c,1),r(u,p+0x4c,1),r(u,p+0x80),bool(r(u,p+0xdc)&0x40),cargo(u,UNIT)['live'],after],[4,4,UNIT,True,[p],dict(live=[[0,1],[0,1]],completed=[[0,1],[0,1]],terran_supply=[0,6],owner_lists=[[],[p,UNIT]])],joined(executions),dict(final=transport_state(u,UNIT,[p]),snapshot=snapshot(u)))
 if a.case_group in ('all','transport'):
  for typ in (11,69,42):
   u=new();executions,passengers=setup_world(u,typ,[dict(type=0,x=260,y=200,owner=3)])
   if typ==42:w(u,0x534590+3*46+24,1,1)
   for tick in range(64):image_and_bullet_step(u,executions)
   gate_auxiliary_services(u);p=passengers[0]
   executions.append(call(u,0x42a380,UNIT,args=(94,p)));timeline=[]
   for tick in range(240):
    object_step(u,executions);timeline.append(dict(tick=tick+1,phase='boarding',state=transport_state(u,UNIT,passengers)))
    if r(u,p+0xdc)&0x40:break
   assert r(u,p+0xdc)&0x40 and r(u,p+0x80)==UNIT and cargo(u,UNIT)['live']==[p],dict(typ=typ,passenger=p,cargo=cargo(u,UNIT),timeline=timeline[-10:])
   executions.append(call(u,0x42a2c0,UNIT,args=(111,)))
   for tick in range(160):
    object_step(u,executions);timeline.append(dict(tick=tick+1,phase='unloading',state=transport_state(u,UNIT,passengers)))
    if not(r(u,p+0xdc)&0x40):break
   record('stock_transport_natural_pickup_unload_'+str(typ),dict(transport=typ,passenger=0,stock_populated_native_created=True,original_transport94_produces_passenger92=True,whole_object_board_and_unload=True,initial_positions=[[200,200],[260,200]],no_passenger_order_status_cargo_or_motion_fixture=True),[bool(r(u,p+0xdc)&0x60),r(u,p+0x80),cargo(u,UNIT)['packed'],r(u,0x665b38)],[False,0,[0]*8,2],joined(executions),dict(timeline=timeline,final=transport_state(u,UNIT,passengers)))
  for passenger_type in (0,83):
   u=new();executions=prepared_transport(u,69);p=create_passenger(u,executions,passenger_type);enter_direct(u,executions,p);assert r(u,p+0xdc)&0x40
   executions.append(call(u,0x42a340,UNIT,args=(112,400,260)));timeline=[]
   for tick in range(260):
    object_step(u,executions);timeline.append(dict(tick=tick+1,state=transport_state(u,UNIT,[p])))
    if not(r(u,p+0xdc)&0x40):break
   record('stock_transport_loaded_physical_move_unload_'+str(passenger_type),dict(transport69=True,passenger_type=passenger_type,original_enter92_produces_load=True,original_move_unload112_command=[400,260],whole_object_travel_unload=True,no_child_motion_or_cargo_fixture=True),[bool(r(u,p+0xdc)&0x60),r(u,p+0x80),cargo(u,UNIT)['packed'],r(u,0x665b38)],[False,0,[0]*8,2],joined(executions),dict(timeline=timeline,final=transport_state(u,UNIT,[p])))
  u=new();executions=prepared_transport(u);passengers=[]
  for j in range(2):p=create_passenger(u,executions);enter_direct(u,executions,p);passengers.append(p)
  executions.append(call(u,0x42a2c0,UNIT,args=(111,)));timeline=[]
  for tick in range(50):
   object_step(u,executions);timeline.append(dict(tick=tick+1,state=transport_state(u,UNIT,passengers)))
   if not cargo(u,UNIT)['live']:break
  unload_ticks=[row['tick'] for i,row in enumerate(timeline) if len(row['state']['cargo']['live']) < (2 if not i else len(timeline[i-1]['state']['cargo']['live']))]
  record('stock_transport_two_passengers_native_unload_pacing',dict(two_original_enter92_loads=True,whole_object_unload111_consumers=True,no_parent_or_child_timer_override=True),[len(unload_ticks),[r(u,p+0x80) for p in passengers],cargo(u,UNIT)['packed'],r(u,0x665b38)],[2,[0,0],[0]*8,3],joined(executions),dict(unload_ticks=unload_ticks,timeline=timeline,final=transport_state(u,UNIT,passengers)))
 modules={Path(module.__file__).name:hashlib.sha256(Path(module.__file__).read_bytes()).hexdigest() for name,module in sorted(sys.modules.items()) if getattr(module,'__file__',None) and Path(module.__file__).parent.resolve()==Path(__file__).parent.resolve() and name!='__main__'}
 result=dict(target_sha256=local.TARGET_HASH,probe_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),source_dependencies_sha256=modules,engine=dict(unicorn=unicorn.__version__,pefile=pefile.__version__,machine=platform.machine()),dat_sha256=dat_hashes,asset_manifest_sha256=hashlib.sha256(a.asset_manifest.read_bytes()).hexdigest(),string_asset_manifest_sha256=hashlib.sha256(a.string_manifest.read_bytes()).hexdigest(),network_tbl_sha256=hashlib.sha256(network).hexdigest(),case_group=a.case_group,helper_stubs=[],host_imports_executed=False,outer_accepted_frame_executed=False,instruction_bound=250000,reviewed_code_ranges=[dict(start=hex(lo),end_exclusive=hex(hi)) for lo,hi in sorted(set(ALLOW))],complete_cases=len(cases),prefix_cases=0,all_passed=all(x['passed'] for x in cases),cases=cases)
 a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(dict(cases=len(cases),all_passed=result['all_passed'])))
if __name__=='__main__':main()
