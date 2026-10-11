#!/usr/bin/env python3
"""Probe original Town pools, creation and owner/area assignment.

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
import inspect_native_towns_117 as local
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
UNIT=0x5DA3A0;SPRITE=0x804000;IMAGE=0x805000;STACK=0x900000;STOP=0xA00000;SCRIPT=0xD00000;ASSETS=0xE00000
SELECT=('live_unit_add','live_unit_account','unit_group_upgrade_effects','speed_from_flingy','acceleration_from_flingy','turn_from_flingy','speed_status_modifier','enable_speed_upgrade','iscript_speed_control_zero_guard','upgrade_level_get')
DEATH=('drop_powerup_zero_carry','clear_unit_ai','remove_military_ai_null','remove_town_ai_null','clear_town_resource_refs','clear_guard_ai_null','clear_queen_selection')
ALLOW=sorted(set(list(construction_source.ALLOW)+list(worker_blocks.BLOCKS.values())+list(removal_blocks.WITNESSES.values())+list(guard_blocks.WITNESSES.values())+list(script_source.WITNESSES.values())+list(local.BLOCKS.values())+[combat.BLOCKS['change_region_state'],frame_source.BLOCKS['build_request_wrapper'],frame_source.BLOCKS['town_request_insert']]))
PLAN=dict(target_sha256=local.TARGET_HASH,helper_stubs=[],instruction_bound=250000,scope='Original Town/worker/building pool initialization, allocation, owner and area creation',limits='Explicit clear world and controller/campaign inputs; no outer accepted frame, original loader or renderer.')
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--exe',type=Path,required=True);p.add_argument('--data',type=Path,required=True);p.add_argument('--manifest',type=Path,required=True);p.add_argument('--assets',type=Path,required=True);p.add_argument('--asset-manifest',type=Path,required=True);p.add_argument('--pyms',type=Path,required=True);p.add_argument('--out',type=Path);p.add_argument('--case-group',choices=('all','pools','start','area'),default='all');p.add_argument('--execute-original-code',action='store_true');a=p.parse_args()
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
 from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_EBX,UC_X86_REG_ECX,UC_X86_REG_EDX,UC_X86_REG_EDI,UC_X86_REG_ESP,UC_X86_REG_ESI,UC_X86_REG_EIP
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
  try:u.emu_start(start,STOP+1,count=250000)
  except Exception as e:raise RuntimeError(f'{e}; recent={recent[-30:]}; calls={calls[-10:]}') from e
  finally:u.hook_del(h)
  expected_sp=sp+4*(len(args)+1)
  if not boundary or u.reg_read(UC_X86_REG_ESP)!=expected_sp:
   raise RuntimeError(f'Incomplete original return entry={start:#x}; instructions={instructions}; EIP={u.reg_read(UC_X86_REG_EIP):#x}; boundary={boundary}; ESP={u.reg_read(UC_X86_REG_ESP):#x}; expected_ESP={expected_sp:#x}; recent={recent[-30:]}; calls={calls[-10:]}')
  executed_stats.append(dict(entry=hex(start),instructions=instructions))
  return dict(complete_return=True,return_eax=u.reg_read(UC_X86_REG_EAX),instruction_count=instructions,calls=calls,iscript_opcodes=script_ops,bullet_initializations=initializations,impact_events=impacts,movement_state_dispatches=movement_states,visited_functions=[hex(x) for x in sorted(visited)])
 def snapshot(u):
  parent=r(u,0x665B08+3*4);result=dict(owner_head=parent,unit_count=r(u,0x665B38),free_unit_head=r(u,0x665AF0),free_sprite_head=r(u,0x5C51C8),free_image_head=r(u,0x544694),hidden_head=r(u,0x665AF8),rng_state=r(u,0x67393C),pending_delete_head=r(u,0x665AE8),error=r(u,0x5DA398,2),rng_counters={str(k):r(u,0x673940+k*4) for k in (3,6,7,14,15,37)},units=[])
  for j in range(16):
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
  u=new(unit_count=16,sprite_count=32,image_count=128)
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
  return dict(address=town,next=r(u,town),prev=r(u,town+4),worker_pool=r(u,town+8),workers=ai_list(town+0xc),building_pool=r(u,town+0x10),buildings=ai_list(town+0x14),owner=r(u,town+0x18,1),initialized=r(u,town+0x19,1),worker_count=r(u,town+0x1a,1),area=r(u,town+0x1c,1),needs_resources=r(u,town+0x1d,1),center=[r(u,town+0x20,2),r(u,town+0x22,2)],main=r(u,town+0x24),builder=r(u,town+0x28),resources=[r(u,town+k) for k in (0x2c,0x30,0x34,0x38)])
 if a.case_group in ('all','pools'):
  u=new();executions=[clear_world(u)];u.mem_write(WORKERPOOL,bytes([0xa5])*0x5dc4);u.mem_write(BUILDINGPOOL,bytes([0xa5])*0xabe4);u.mem_write(TOWNPOOL,bytes([0xa5])*0xb3b4);executions.append(call(u,0x486a20,0))
  expected=bytearray([0xa5])*0xb3b4
  for j in range(100):
   at=j*460;struct.pack_into('<6I',expected,at,TOWNPOOL+at+460 if j<99 else 0,TOWNPOOL+at-460 if j else 0,WORKERPOOL,0,BUILDINGPOOL,0);expected[at+24:at+460]=bytes(436)
  struct.pack_into('<I',expected,0xb3b0,TOWNPOOL);actual=bytes(u.mem_read(TOWNPOOL,0xb3b4));assert actual==expected
  for pool,pool_size,stride,count in ((WORKERPOOL,0x5dc4,24,1000),(BUILDINGPOOL,0xabe4,44,1000)):
   rawpool=bytes(u.mem_read(pool,pool_size));want=bytearray([0xa5])*pool_size
   for j in range(count):
    at=j*stride;struct.pack_into('<II',want,at,pool+at+stride if j<count-1 else 0,pool+at-stride if j else 0)
    if stride==24:want[at+8:at+24]=bytes(16)
    else:want[at+8:at+14]=bytes(6);want[at+16:at+44]=bytes(28)
   struct.pack_into('<I',want,pool_size-4,pool);assert rawpool==want,(hex(pool),[(i,x,want[i]) for i,x in enumerate(rawpool) if x!=want[i]][:10])
  record('original_all_town_worker_building_pool_initialization',dict(initial_byte=165,town_count=100,worker_count=1000,building_count=1000),[[r(u,0x69b518+player*8),r(u,0x69b51c+player*8)] for player in range(8)],[[TOWNPOOL,0]]*8,joined(executions),dict(town_pool_sha256=hashlib.sha256(actual).hexdigest(),worker_free=r(u,WORKERPOOL+0x5dc0),building_free=r(u,BUILDINGPOOL+0xabe0),building_padding14_15=list(u.mem_read(BUILDINGPOOL+14,2))))
  u,executions=initialized_world();created=[]
  for j in range(101):
   owner=1 if j%2 else 3;ex=call(u,0x485770,owner,0x10001+j,((0xfffffffb+j)&0xffffffff,));executions.append(ex);got=ex['return_eax'];assert got==(TOWNPOOL+j*460 if j<100 else 0)
   if got:
    state=town_snapshot(u,got);assert state['owner']==owner and state['center']==[(1+j)&65535,(-5+j)&65535] and state['worker_pool']==WORKERPOOL and state['building_pool']==BUILDINGPOOL;created.append(state)
  record('original_shared_pool_100_allocations_and_exhaustion',dict(alternating_owners=[3,1],calls=101,wide_coordinates=True),[r(u,TOWNPOOL+0xb3b0),len(chain(u,0x69b51c+8,0)),len(chain(u,0x69b51c+24,0))],[0,50,50],joined(executions),dict(created=created))
  for ident in (0,1,100,101,65536,0xffffffff):
   u,executions=initialized_world();executions.append(call(u,0x4877d0,ident))
   expected=0 if ident==0 else (ident*460+0x68ff94)&0xffffffff
   record('original_town_index_decode_unchecked_width',dict(index=ident),executions[-1]['return_eax'],expected,joined(executions))
  u,executions=initialized_world();w(u,0x6726d0+3*36,1,1);executions.append(call(u,0x485770,3,512,(512,)));town=executions[-1]['return_eax'];executions.append(call(u,0x487550,town,1,(109,80,0)));before=list(u.mem_read(town+0x3c,4));assert before!=[0,0,0,0]
  executions.append(call(u,0x4856a0,town));assert r(u,TOWNPOOL+0xb3b0)==town and r(u,0x69b51c+24)==0
  executions.append(call(u,0x485770,3,700,(600,)));assert executions[-1]['return_eax']==town
  record('original_empty_town_request_reuse_clears_400bytes',dict(request_inserted_by='0x487550',request_before=before,delete_by='0x4856a0'),[bytes(u.mem_read(town+0x3c,400))==bytes(400),r(u,town+8),r(u,town+0x10),[r(u,town+0x20,2),r(u,town+0x22,2)]],[True,WORKERPOOL,BUILDINGPOOL,[700,600]],joined(executions),town_snapshot(u,town))
 if a.case_group in ('all','start'):
  u,executions=initialized_world();executions.append(call(u,0x487030,3,512,(512,0)))
  record('original_start_town_no_owned_units_returns_null',dict(owner=3,owned_head=0),[executions[-1]['return_eax'],r(u,TOWNPOOL+0xb3b0),r(u,0x69b51c+24)],[0,TOWNPOOL,0],joined(executions))
  for area in (0,7,256):
   u,executions=initialized_world();worker,ex=create(u,7,512,512,3);executions+=ex
   executions.append(call(u,0x487030,3,0x10002,(0xfffffffe,area)));town=executions[-1]['return_eax'];assert town==TOWNPOOL
   state=town_snapshot(u,town);ai=r(u,worker+0x134)
   record('original_start_town_assigns_native_worker',dict(owner=3,campaign_flag_written_by='0x482ee0',wide_center=[65538,4294967294],area_argument=area,native_worker=worker),[state['center'],state['area'],state['needs_resources'],state['worker_count'],ai,state['workers'][0]['unit'],state['workers'][0]['town']],[[2,65534],area&255,int(area!=0),1,WORKERPOOL,worker,town],joined(executions),state)
  for condition in ('far','incomplete','train106','reset150','death','existing_ai'):
   u,executions=initialized_world();worker,ex=create(u,7,2000 if condition=='far' else 512,512,3);executions+=ex;original_ai=0
   if condition=='incomplete':w(u,worker+0xdc,r(u,worker+0xdc)&~1)
   if condition in ('train106','reset150'):
    set_context(u,worker);executions.append(call(u,0x42a2c0,worker,args=(106 if condition=='train106' else 150,)))
   if condition=='death':set_context(u,worker);executions.append(call(u,0x42a4d0,worker))
   if condition=='existing_ai':
    executions.append(call(u,0x485770,3,100,(100,)));oldtown=executions[-1]['return_eax'];executions.append(call(u,0x484cd0,oldtown,worker));original_ai=r(u,worker+0x134)
   executions.append(call(u,0x487030,3,512,(512,0)));town=executions[-1]['return_eax'];assert town
   state=town_snapshot(u,town);accepted=condition=='far'
   record('original_start_town_worker_admission_'+condition,dict(condition=condition,native_worker=worker,completed_bit_fixture=condition=='incomplete',existing_ai=original_ai),[state['worker_count'],r(u,worker+0x134),state['center']],[int(accepted),WORKERPOOL if accepted else original_ai,[512,512]],joined(executions),state)
  for typ in (106,109):
   u,executions=initialized_world();worker,ex=create(u,7,512,512,3);executions+=ex;building,ex=create(u,typ,800,512,3);executions+=ex
   executions.append(call(u,0x487030,3,512,(512,7)));town=executions[-1]['return_eax'];state=town_snapshot(u,town)
   record('original_start_town_native_worker_and_building',dict(native_worker=7,native_building=typ,campaign_flag_producer='0x482ee0'),[state['worker_count'],len(state['buildings']),state['main'],r(u,building+0x134),state['initialized']],[1,1,building if typ==106 else 0,BUILDINGPOOL,1],joined(executions),dict(town=state,region_state=r(u,0x80d000+3*0x400+0x34+5,1)))
  for profile in ('no_units','first','second','exhausted'):
   u,executions=initialized_world();worker=0
   if profile!='no_units':worker,ex=create(u,7,512,512,3);executions+=ex
   if profile=='second':executions.append(call(u,0x485770,3,100,(100,)))
   if profile=='exhausted':
    for j in range(100):executions.append(call(u,0x485770,1,100,(100,)))
   executions.append(call(u,0x4b22a0,3,7,(512,512)));guard=r(u,0x6be614+24);assert guard
   executions.append(call(u,0x487030,3,512,(512,0)));town=executions[-1]['return_eax'];removed=profile in ('first','exhausted')
   record('original_start_town_parentless_guard_ordering_'+profile,dict(profile=profile,precreated_guard=guard,fully_exhausted_pool=profile=='exhausted'),[bool(town),r(u,0x6be614+24),r(u,worker+0x134) if worker else 0],[profile in ('first','second'),0 if removed else guard,WORKERPOOL if profile in ('first','second') else 0],joined(executions),dict(town=town_snapshot(u,town) if town else None,guard_free_head=r(u,0x6b6908+0x7d00)))
 if a.case_group in ('all','area'):
  for owned,typ,expected_center in ((False,7,None),(True,7,[512,512]),(True,0,[600,600])):
   u,executions=initialized_world();unit,ex=create(u,typ,512,512,3 if owned else 4);executions+=ex
   rect=0x80e000;u.mem_write(rect,struct.pack('<4I',400,400,800,800));executions.append(call(u,0x486e80,3,rect,(7,)));town=executions[-1]['return_eax']
   assert town==(TOWNPOOL if owned else 0)
   state=town_snapshot(u,town) if town else None
   got=[state['center'],state['area'],state['needs_resources'],state['worker_count']] if state else [None,0,0,0]
   record('original_area_town_owner_filter_and_center',dict(owner=3,native_type=typ,native_owner=3 if owned else 4,rectangle=[400,400,800,800]),got,[expected_center,7 if owned else 0,1 if owned else 0,int(owned and typ==7)],joined(executions),dict(town=state,spatial_query_depth=r(u,0x5d6e48),spatial_query_offset=r(u,0x5d6e5c)))
  for condition,point in (('outside',[1000,512]),('edge_overlap',[391,512]),('incomplete',[512,512]),('existing_ai',[512,512]),('death',[512,512])):
   u,executions=initialized_world();worker,ex=create(u,7,*point,3);executions+=ex;original_ai=0
   if condition=='incomplete':w(u,worker+0xdc,r(u,worker+0xdc)&~1)
   if condition=='existing_ai':
    executions.append(call(u,0x485770,3,100,(100,)));oldtown=executions[-1]['return_eax'];executions.append(call(u,0x484cd0,oldtown,worker));original_ai=r(u,worker+0x134)
   if condition=='death':set_context(u,worker);executions.append(call(u,0x42a4d0,worker))
   rect=0x80e000;u.mem_write(rect,struct.pack('<4I',400,400,800,800));executions.append(call(u,0x486e80,3,rect,(0,)));town=executions[-1]['return_eax'];accepted=condition!='outside';assert bool(town)==accepted
   state=town_snapshot(u,town) if town else None;assigned=condition in ('edge_overlap','incomplete','death')
   record('original_area_town_worker_admission_'+condition,dict(condition=condition,native_point=point,completed_bit_fixture=condition=='incomplete',existing_ai=original_ai),[bool(town),state['worker_count'] if town else 0,r(u,worker+0x134),state['center'] if town else None],[accepted,int(assigned),WORKERPOOL if assigned else original_ai,point if assigned else [600,600] if town else None],joined(executions),state)
  for typ in (106,109):
   u,executions=initialized_world();worker,ex=create(u,7,512,512,3);executions+=ex;building,ex=create(u,typ,700,600,3);executions+=ex;rect=0x80e000;u.mem_write(rect,struct.pack('<4I',400,400,900,900));executions.append(call(u,0x486e80,3,rect,(7,)));town=executions[-1]['return_eax'];state=town_snapshot(u,town)
   record('original_area_town_worker_and_building_center',dict(native_worker=7,native_building=typ,rectangle=[400,400,900,900]),[state['worker_count'],len(state['buildings']),state['center'],state['main']],[1,1,[700,600] if typ==106 else [512,512],building if typ==106 else 0],joined(executions),state)
 modules={Path(module.__file__).name:hashlib.sha256(Path(module.__file__).read_bytes()).hexdigest() for name,module in sorted(sys.modules.items()) if getattr(module,'__file__',None) and Path(module.__file__).parent.resolve()==Path(__file__).parent.resolve() and name!='__main__'}
 result=dict(target_sha256=local.TARGET_HASH,probe_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),source_dependencies_sha256=modules,engine=dict(unicorn=unicorn.__version__,pefile=pefile.__version__,python=platform.python_version(),machine=platform.machine()),dat_sha256=dat_hashes,asset_manifest_sha256=hashlib.sha256(a.asset_manifest.read_bytes()).hexdigest(),case_group=a.case_group,helper_stubs=[],instruction_bound=250000,explicit_world_inputs=dict(map_pixels=[4096,4096],map_tiles=[128,128],map_rectangle=[0,0,4096,4096],clear_region_group=1,obstacle_contours=0,owner=3,controller=2,campaign_flag_producer='0x482ee0',race=1,original_pool_initialization='0x486a20',genuine_unit_creation='0x424900 and 0x424da0',whole_object_callback_executed=False),reviewed_code_ranges=[dict(start=hex(lo),end_exclusive=hex(hi)) for lo,hi in sorted(set(ALLOW))],complete_original_function_calls=len(executed_stats),total_original_instructions_executed=sum(x['instructions'] for x in executed_stats),maximum_single_call_instructions=max(x['instructions'] for x in executed_stats),original_code_executed=True,complete_game_executed=False,complete_cases=len(cases),prefix_cases=0,all_passed=all(x['passed'] for x in cases),cases=cases)
 a.out.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({'complete_cases':len(cases),'all_passed':True}))
if __name__=='__main__':main()
