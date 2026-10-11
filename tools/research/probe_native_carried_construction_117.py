#!/usr/bin/env python3
"""Probe actual carried-worker construction, native Drop/origin and retention.

Without the explicit execution flag this prints a plan only. The guest executes
reviewed original instructions with actual callees, no stubs, Windows imports,
process startup, game loop, host files or network.
"""
import argparse
from collections import Counter
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
import inspect_native_carried_construction_117 as local
import inspect_native_powerup_goals_117 as goals_source
import probe_native_powerup_goals_117 as goals_probe
import probe_native_construction_117 as terran_source
import probe_native_zerg_construction_117 as zerg_source
import probe_native_special_producers_117 as producers_source
import probe_native_nydus_117 as nydus_source
import probe_native_powerup_lifecycle_117 as powerup_source
import probe_native_special_orders_117 as special_orders_source
import probe_native_computer_combat_117 as computer_source
import probe_native_bunker_117 as object_source
import inspect_native_nuke_117 as retreat_source
import inspect_native_special_initialization_117 as special
import inspect_native_mission_units_117 as mission
import inspect_native_populated_lifecycle_117 as populated
import inspect_native_creep_117 as creep
import probe_native_trigger_creep_117 as creep_source
import inspect_native_harvesting_117 as harvesting
import inspect_native_unit_death_117 as death
import inspect_native_image_geometry_117 as geometry
import probe_native_movement_fsm_117 as movement
import probe_native_unit_properties_117 as properties
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
ALLOW=list(goals_probe.ALLOW)+list(terran_source.ALLOW)+list(zerg_source.ALLOW)+list(producers_source.ALLOW)+list(actions.counts_source.BLOCKS.values())+list(actions.BLOCKS.values())+list(nydus_source.ALLOW)+list(powerup_source.ALLOW)+[retreat_source.BLOCKS[k] for k in ('guard_home_or_unbound','reaction_retreat_point')]+list(special_orders_source.ALLOW)+list(computer_source.ALLOW)+list(object_source.ALLOW)+list(hidden_source.ALLOW)+list(initialized_source.ALLOW)+list(populated.BLOCKS.values())+list(local.BLOCKS.values())+list(special.BLOCKS.values())+[harvesting.BLOCKS[k] for k in ('cargo_overlay_remove',)]+[death.BLOCKS[k] for k in ('hidden_unit_add','active_unit_remove')]+[geometry.BLOCKS[k] for k in ('image_follow_main_offset','image_follow_main_enable')]+list(movement.ALLOW)+list(mission.BLOCKS.values())+list(properties.ALLOW)+list(creep.BLOCKS.values())+list(creep_source.BLOCKS.values())+[harvesting.BLOCKS[k] for k in ('sprite_replace_images','reapply_unit_status_overlays','resource_creation_sprite_change')]
PLAN=dict(target_sha256=local.HASH,helper_stubs=[],instruction_bound=1000000,scope='Genuine carrying worker, paid construction/order transition, Terran carry retention and Drone Drop/origin relocation, cancel/rejection',limits='Private stock resources/pools, flat terrain/visible tile/controller inputs; outer accepted-frame and resource loading/rendering excluded')
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--exe',type=Path,required=True);p.add_argument('--data',type=Path,required=True);p.add_argument('--manifest',type=Path,required=True);p.add_argument('--assets',type=Path,required=True);p.add_argument('--asset-manifest',type=Path,required=True);p.add_argument('--notification-assets',type=Path,required=True);p.add_argument('--notification-manifest',type=Path,required=True);p.add_argument('--pyms',type=Path,required=True);p.add_argument('--out',type=Path);p.add_argument('--case-group',choices=('all','terran','zerg','cancel','failure'),default='all');p.add_argument('--execute-original-code',action='store_true');a=p.parse_args()
 raw=a.exe.read_bytes();assert hashlib.sha256(raw).hexdigest()==local.HASH
 if not a.execute_original_code:print(json.dumps(PLAN,indent=2));return
 assert a.out and platform.machine().lower() in ('x86_64','amd64')
 assert subprocess.check_output(['git','-C',str(a.pyms),'rev-parse','HEAD'],text=True).strip()=='bfc5d3aad0b5614a5aff72c223f8efa00afddfa4'
 manifest=json.loads(a.manifest.read_text());assetmanifest=json.loads(a.asset_manifest.read_text());dat_hashes={};assets={};lo_assets=[]
 notification_manifest=json.loads(a.notification_manifest.read_text());network=(a.notification_assets/'rez/network.tbl').read_bytes();assert hashlib.sha256(network).hexdigest()==notification_manifest['assets']['rez\\network.tbl']['sha256']
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
 from unicorn import Uc,UC_ARCH_X86,UC_MODE_32,UC_HOOK_CODE,UC_HOOK_MEM_WRITE
 from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_EDX,UC_X86_REG_ESP,UC_X86_REG_ESI,UC_X86_REG_EBP,UC_X86_REG_EDI,UC_X86_REG_EBX
 pe=pefile.PE(data=raw);assert pe.OPTIONAL_HEADER.ImageBase==0x400000 and pe.FILE_HEADER.Machine==0x14C
 image=pe.get_memory_mapped_image();size=(pe.OPTIONAL_HEADER.SizeOfImage+4095)&~4095;cases=[];cache={};all_visited=set();write_count=0
 def w(u,address,value,width=4):u.mem_write(address,(value&((1<<(8*width))-1)).to_bytes(width,'little'))
 def r(u,address,width=4):return int.from_bytes(u.mem_read(address,width),'little')
 def new(unit_count=16,sprite_count=32,image_count=128,seed=12345,rng_enabled=True,thingy_count=0):
  u=Uc(UC_ARCH_X86,UC_MODE_32);u.mem_map(0x400000,size);u.mem_write(0x400000,image)
  for addr,length in ((0x800000,0x20000),(STACK,0x4000),(STOP,0x1000),(SCRIPT,0x10000),(ASSETS,0x600000),(0xB00000,0xA0000),(0xC00000,0x80000),(0x1400000,0x100000),(0x1600000,0x200000)):u.mem_map(addr,length)
  u.mem_write(SCRIPT,script);w(u,0x54330C,SCRIPT)
  at=ASSETS;allocated={}
  for ident,(row,v) in assets.items():
   if row['path'] not in allocated:u.mem_write(at,v);allocated[row['path']]=at;at+=(len(v)+4095)&~4095
   w(u,0x5436F8+ident*4,allocated[row['path']])
  for row,v in lo_assets:
   if row['path'] not in allocated:u.mem_write(at,v);allocated[row['path']]=at;at+=(len(v)+4095)&~4095
   if row['kind']<5:w(u,0x596400+(row['kind']*999+row['image_id'])*4,allocated[row['path']])
  u.mem_write(0x81C000,network);w(u,0x665DE0,0x81C000)
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
  for pool,count,head,tail,stride in ((0x80D000,thingy_count,0x5C7A20,0x5C7A24,0x10),):
   u.mem_write(pool,b'\0'*(max(count,1)*stride));w(u,head,pool if count else 0);w(u,tail,pool+(count-1)*stride if count else 0)
   for i in range(count):w(u,pool+i*stride,pool+(i-1)*stride if i else 0);w(u,pool+i*stride+4,pool+(i+1)*stride if i+1<count else 0)
  w(u,0x5C7A28,0);w(u,0x5C7A2C,0)
  w(u,0x543230,4096,2);w(u,0x543234,4096,2);w(u,0x5264B4,128,2);w(u,0x5264B6,128,2)
  for base,count in ((0x665B08,12),(0x5C51D0,256),(0x5C55D0,256),(0x673940,50)):u.mem_write(base,b'\0'*(count*4))
  for base in (0x665AF8,0x665AFC,0x665AE0,0x665AE4,0x665B38):w(u,base,0)
  w(u,0x673930,int(rng_enabled));w(u,0x67393C,seed);w(u,0x543300,0);w(u,0x543304,0)
  w(u,0x508D50,0);w(u,0x67CC90,0);w(u,0x6726D0+3*36,2,1)
  return u
 def guest_write(u,access,address,length,value,_):
  nonlocal write_count
  writable=[(0x800000,0x820000),(STACK,STACK+0x4000),(SCRIPT,SCRIPT+0x10000),(ASSETS,ASSETS+0x600000),(0xB00000,0xBA0000),(0xC00000,0xC80000),(0x1400000,0x1500000),(0x1600000,0x1800000)]
  writable += [(0x400000+section.VirtualAddress,0x400000+section.VirtualAddress+max(section.Misc_VirtualSize,section.SizeOfRawData)) for section in pe.sections if not section.Characteristics&0x20000000]
  assert any(lo<=address and address+length<=hi for lo,hi in writable),('Undeclared guest write',hex(address),length)
  write_count+=1
 def call(u,start,ecx=0,edx=0,args=(),stop_before=None,fragment=False,registers=None):
  visited=set();calls=[];carry_events=[];script_ops=[];bullet_initializations=[];impact_events=[];instructions=0;boundary=False;prefix_pc=None;recent=[];entries={lo for lo,hi in ALLOW}
  def hook(m,addr,width,_):
   nonlocal instructions,boundary,prefix_pc
   if stop_before and addr in stop_before:prefix_pc=addr;m.emu_stop();return
   if addr==STOP:boundary=True;m.emu_stop();return
   key=(addr,width)
   if key not in cache:cache[key]=tuple(lo for lo,hi in ALLOW if lo<=addr and addr+width<=hi)
   if not cache[key]:raise RuntimeError(f'Outside reviewed original code {addr:#x}; recent={recent[-15:]}')
   visited.update(cache[key]);all_visited.add(addr);instructions+=1;recent.append(hex(addr))
   if addr==0x4272C0:
    at=m.reg_read(UC_X86_REG_ECX);typ=r(m,at+0x64,2)
    carry_events.append(dict(kind='Drop entry',carrier=at,carrier_type=typ,carrier_item=r(m,at+0xC0) if typ in (7,41,64) else 0,resource=r(m,at+0xE0,1),position=list(struct.unpack('<2H',m.mem_read(at+0x28,4)))))
   if addr in (0x43B9F3,0x43BA0C,0x43BA27):
    item=m.reg_read(UC_X86_REG_ESI);worker=m.reg_read(UC_X86_REG_EBX)
    if item:carry_events.append(dict(kind={0x43B9F3:'Drone after Drop',0x43BA0C:'Drone after conversion',0x43BA27:'Drone after item origin relocation'}[addr],worker=worker,worker_type=r(m,worker+0x64,2),item=item,item_position=list(struct.unpack('<2H',m.mem_read(item+0x28,4))),item_origin=list(struct.unpack('<2H',m.mem_read(item+0xD0,4))),item_carrier=r(m,item+0xD4),worker_resource=r(m,worker+0xE0,1),worker_item=r(m,worker+0xC0)))
   if addr==0x4104C4:
    pc=m.reg_read(UC_X86_REG_ESI);script_ops.append(dict(offset=pc-SCRIPT,opcode=r(m,pc,1)))
   if addr==0x409E15:
    at=m.reg_read(UC_X86_REG_EDI);bullet_initializations.append(dict(bullet=at,weapon=r(m,at+0x60,1),spawner=m.reg_read(UC_X86_REG_ESI),heading=r(m,at+0x21,1)))
   if addr==0x4090E0:
    at=m.reg_read(UC_X86_REG_ECX);impact_events.append(dict(bullet=at,weapon=r(m,at+0x60,1),flags=r(m,at+0x62,1),position=[r(m,at+0x28,2),r(m,at+0x2A,2)],source=r(m,at+0x64),target=r(m,at+0x5C)))
   if addr in entries:
    sp=m.reg_read(UC_X86_REG_ESP);calls.append(dict(callee=hex(addr),ecx=m.reg_read(UC_X86_REG_ECX),edx=m.reg_read(UC_X86_REG_EDX),stack=[r(m,sp+4+i*4) for i in range(6)]))
  h=u.hook_add(UC_HOOK_CODE,hook);wh=u.hook_add(UC_HOOK_MEM_WRITE,guest_write);sp=STACK+0x3000;u.mem_write(sp,struct.pack('<'+'I'*(len(args)+1),STOP,*args));u.reg_write(UC_X86_REG_ESP,sp);u.reg_write(UC_X86_REG_ECX,ecx);u.reg_write(UC_X86_REG_EDX,edx)
  if registers:
   for register,value in registers.items():u.reg_write(register,value)
  try:u.emu_start(start,STOP+1,count=1000000)
  except Exception as e:raise RuntimeError(f'{e}; recent={recent[-30:]}; calls={calls[-10:]}') from e
  finally:u.hook_del(h);u.hook_del(wh)
  if prefix_pc is None:assert boundary and u.reg_read(UC_X86_REG_ESP)==sp+4*(len(args)+1),(hex(start),instructions,recent[-30:],snapshot(u))
  else:assert not boundary
  return dict(carry_construction_events=carry_events,complete_return=boundary,complete_original_function=boundary and not fragment,fragment_executed=fragment,boundary_va=None if prefix_pc is None else hex(prefix_pc),return_eax=u.reg_read(UC_X86_REG_EAX),instruction_count=instructions,calls=calls,iscript_opcodes=script_ops,bullet_initializations=bullet_initializations,impact_events=impact_events,visited_functions=[hex(x) for x in sorted(visited)])
 def snapshot(u):
  parent=r(u,0x665B08+3*4);result=dict(owner_head=parent,unit_count=r(u,0x665B38),free_unit_head=r(u,0x665AF0),free_sprite_head=r(u,0x5C51C8),free_image_head=r(u,0x544694),hidden_head=r(u,0x665AF8),rng_state=r(u,0x67393C),pending_delete_head=r(u,0x665AE8),error=r(u,0x5DA398,2),rng_counters={str(k):r(u,0x673940+k*4) for k in (3,6,7,14,15,37)},units=[])
  for j in range(16):
   unit=UNIT+j*0x150
   if r(u,unit+0xC)==0:continue
   sp=r(u,unit+0xC);images=[];at=r(u,sp+0x1C)
   while at:
    images.append(dict(address=at,id=r(u,at+8,2),flags=r(u,at+0xC,2),direction=r(u,at+0xB,1),offset=list(struct.unpack('<2b',u.mem_read(at+0xE,2))),iscript=list(struct.unpack('<3H2B',u.mem_read(at+0x10,8))),base_frame=r(u,at+0x18,2),frame=r(u,at+0x1A,2),bounds=list(struct.unpack('<8h',u.mem_read(at+0x1C,16))),sprite=r(u,at+0x3C),grp=r(u,at+0x2C)))
    at=r(u,at+4);assert len(images)<32
   result['units'].append(dict(address=unit,type=r(u,unit+0x64,2),owner=r(u,unit+0x4C,1),status=r(u,unit+0xDC),hp=r(u,unit+8),shield=r(u,unit+0x60),energy=r(u,unit+0xA2,2),direction=r(u,unit+0x21,1),wireframe_randomizer=r(u,unit+0xE1,1),generation=r(u,unit+0xA5,1),primary_order=r(u,unit+0x4D,1),secondary_order=r(u,unit+0xA6,1),build_time=r(u,unit+0xAC,2),queue=list(struct.unpack('<5H',u.mem_read(unit+0x98,10))),speed=r(u,unit+0x34),subunit=r(u,unit+0x70),sprite=dict(address=sp,id=r(u,sp+8,2),flags=r(u,sp+0xE,1),owner=r(u,sp+0xA,1),visibility=r(u,sp+0xC,1),main=r(u,sp+0x18),images=images)))
  return result
 def record(name,inputs,actual,expected,ex,state=None):
  assert actual==expected,(name,inputs,actual,expected)
  rngc={str(i):r(u,0x673940+i*4) for i in range(52)};total=sum(rngc.values());assert r(u,0x673938)==total and r(u,0x67393C)==advance(12345,total)
  ex['rng_validation']=dict(initial_seed=12345,total_calls=total,source_counters=rngc,final_state=r(u,0x67393C),independent_integer_LCG_matches=True)
  cases.append(dict(case=name,inputs=inputs,actual=actual,expected=expected,passed=True,execution=ex,native_state=state))
 def chain(u,head,next_offset=4):
  result=[];at=r(u,head)
  while at:result.append(at);at=r(u,at+next_offset);assert len(result)<=64
  return result
 def advance(seed,n):
  for i in range(n):seed=(seed*22695477+1)&0xFFFFFFFF
  return seed
 def joined(executions):
  selected={lo for lo,hi in local.BLOCKS.values()}|{0x445070,0x469460,0x41B4B0,0x42A340,0x42A2C0,0x41A340,0x43B380,0x43A270,0x4086B0,0x408650,0x408490,0x424900,0x424DA0,0x4272C0,0x422F60,0x4B8F20,0x4B9820}
  counts=Counter(c['callee'] for x in executions for c in x['calls'])
  return dict(carry_construction_events=[e for x in executions for e in x.get('carry_construction_events',[])],call_entry_counts=dict(counts),complete_return=all(x['complete_return'] for x in executions),complete_call_count=len(executions),return_eax=executions[-1]['return_eax'],instruction_count=sum(x['instruction_count'] for x in executions),calls=[c for x in executions for c in x['calls'] if int(c['callee'],16) in selected],iscript_opcodes=[c for x in executions for c in x['iscript_opcodes']],bullet_initializations=[c for x in executions for c in x['bullet_initializations']],impact_events=[c for x in executions for c in x['impact_events']],visited_functions=sorted({c for x in executions for c in x['visited_functions']}))
 def flat_fixture(u,controllers=None,width=64,height=64):
  # Prepared one-region walkable terrain, empty contour sets, and zero shared
  # vision are explicit caller inputs; this does not load the actual MTXM map.
  controllers=controllers or [2]*12
  for owner in range(12):
   w(u,0x6726D0+owner*36,controllers[owner],1);w(u,0x673F08+owner,0,1)
   ai=0x1400000+owner*0x8000;guard=0x14F0000+owner*0x20
   w(u,0x6BE610+owner*8,ai);w(u,ai+0x7D00,guard);w(u,guard,0);w(u,guard+4,0)
   w(u,0x6BE614+owner*8,0);w(u,0x5264CC+owner*4,0)
  w(u,0x543230,width*32,2);w(u,0x543234,height*32,2)
  u.mem_write(0x543228,struct.pack('<4H',0,0,width*32,height*32));w(u,0x5264B4,width,2);w(u,0x5264B6,height,2)
  w(u,0x69B584,0xB00000);w(u,0xB00000,4,2);w(u,0xB44A0C,1024*256);w(u,0xB44A10,1024*256);w(u,0xB97A1C,0x80A000)
  for addr,offset in ((0x666454,0),(0x666458,0x40000),(0x66696C,0x80000),(0x6CE650,0xC0000),(0x66644C,0x100000)):
   w(u,addr,0x1600000+offset)
  w(u,0x54323C,0x1640000);u.mem_write(0x1600000,struct.pack('<I',0x1FFFF)*65536);w(u,0x67CF60,0x1740000)
  w(u,0x6D319C,2);w(u,0x5C99B0,0);w(u,0x5D6E40,256);w(u,0x5D6E44,160)
  w(u,0x672668,1);w(u,0x67266C,1);w(u,0x536720,1,1);w(u,0x508D50,0)
  w(u,0x5AEF00,0x80B000);w(u,0x5AEF04,0x80B000+31*20);w(u,0x5AEF08,0)
  for i in range(32):w(u,0x80B000+i*20,0x80B000+(i-1)*20 if i else 0);w(u,0x80B000+i*20+4,0x80B000+(i+1)*20 if i<31 else 0)
  return u
 def sections(blob):
  at=0
  while at+8<=len(blob):
   name,length=struct.unpack_from('<4sI',blob,at);assert at+8+length<=len(blob)
   yield name,blob[at+8:at+8+length];at+=8+length
  assert at==len(blob)
 maps=[];corpus=[]
 for maprow in manifest['campaign_found']:
  path=maprow['path'];blob=(a.data/path.replace('\\','/')).read_bytes();assert hashlib.sha256(blob).hexdigest()==maprow['sha256']
  blocks=list(sections(blob));record_blocks=[v for k,v in blocks if k==b'UNIT'];units=[]
  for section_index,values in enumerate(record_blocks):
   assert len(values)%36==0
   for j in range(len(values)//36):
    v=values[j*36:(j+1)*36];entry=dict(map=path,section_index=section_index,record_index=j,serial=struct.unpack_from('<I',v)[0],x=struct.unpack_from('<H',v,4)[0],y=struct.unpack_from('<H',v,6)[0],type=struct.unpack_from('<H',v,8)[0],owner=v[16],relation=struct.unpack_from('<I',v,32)[0],record_sha256=hashlib.sha256(v).hexdigest());entry['_bytes']=v;units.append(entry);corpus.append(entry)
  maps.append(dict(path=path,sha256=maprow['sha256'],unit_sections=len(record_blocks),unit_records=len(units),version=struct.unpack('<H',next(v for k,v in blocks if k==b'VER '))[0],duplicate_serial_values=sum(n>1 for n in Counter(entry['serial'] for entry in units).values()),records_with_relation=sum(bool(entry['relation']) for entry in units),ownr=next((list(v) for k,v in blocks if k==b'OWNR'),None),dimensions=list(struct.unpack('<2H',next(v for k,v in blocks if k==b'DIM ')))))
 def combat_world(u):
  flat_fixture(u);u.mem_write(0x1600000,struct.pack('<I',0x1FF00)*65536)
  for owner in range(8):
   at=0x81E000+owner*0x100;u.mem_write(at,bytes(4*52));w(u,0x6B1A74+owner*4,at)
   for region in range(4):w(u,at+region*52,region,2);w(u,at+region*52+4,owner,1)
  for owner in range(12):w(u,0x534914+owner*12+owner,1,1)
  w(u,0x5DA390,2);w(u,0x5DA394,2);w(u,0x6663EC,0,1);w(u,0x6751F4,0);w(u,0x6BE650,0,1);w(u,0x665D40,0);w(u,0x665B60,0)
  w(u,0xB00000,2,2);u.mem_write(0xB0000C,struct.pack('<H',1)*65536)
  region=0xB00000+0x449FC+64;w(u,region,0,2);w(u,region+2,1,2);w(u,region+0xC,region+0x2C)
  w(u,0x69B574,0x80C000);w(u,0x69B570,0x80C000);w(u,0x80C000,0)
  for at in (0x6C46D4,0x69B568,0x69B564,0x6663E4):w(u,at,2000)
  w(u,0x6A2E70,0);w(u,0x6A5D68,0);w(u,0x6B1A94,0);w(u,0x6B1A98,1);w(u,0x5432F0,0)
  return [call(u,0x425610),call(u,0x40A0E0)]
 def object_step(u,executions):
  w(u,0x52651C,r(u,0x52651C)+1);ex=call(u,0x445070);executions.append(ex)
  assert r(u,0x543300)==r(u,0x543304)==r(u,0x543308)==0
  return ex
 def trace(u,tick,unit,victim):
  return dict(tick=tick,order=r(u,unit+0x4D,1),state=r(u,unit+0x4E,1),signal=r(u,unit+0x4F,1),heading=r(u,unit+0x21,1),status=r(u,unit+0xDC),target=r(u,unit+0x5C),timer=r(u,unit+0x54,1),wait=r(u,unit+0x85,1),queue=[dict(id=r(u,x+8,1),target=r(u,x+0x10)) for x in chain(u,unit+0x74)],hp=r(u,victim+8),victim_order=r(u,victim+0x4D,1),victim_position=list(struct.unpack('<2H',u.mem_read(victim+0x28,4))),sprite_visibility=r(u,r(u,victim+0xC)+0xC,1),bullets=r(u,0x53DB8C))
 inventory=goals_source.campaign_inputs(a.data,manifest)
 def portable_state(u,carrier,item):
  sp=r(u,item+0xC);carrier_sp=r(u,carrier+0xC)
  overlays=[];at=r(u,carrier_sp+0x1C)
  while at:overlays.append(r(u,at+8,2));at=r(u,at+4);assert len(overlays)<32
  return dict(carrier_item=r(u,carrier+0xC0),carrier_resource=r(u,carrier+0xE0,1),carrier_position=list(struct.unpack('<2H',u.mem_read(carrier+0x28,4))),item_carrier=r(u,item+0xD4),item_position=list(struct.unpack('<2H',u.mem_read(item+0x28,4))),item_origin=list(struct.unpack('<2H',u.mem_read(item+0xD0,4))),item_order=r(u,item+0x4D,1),item_sprite=sp,item_hidden=bool(r(u,sp+0xE,1)&32) if sp else None,item_user_flags=r(u,item+0x93,1),overlays=overlays,allocated=r(u,0x665B38),query_depth=r(u,0x5D6E48))
 def goal_profile(u,row,location=None):
  blob=(a.data/row['map'].replace('\\','/')).read_bytes();parts=dict(sections(blob));mrgn=parts[b'MRGN'];force=parts[b'FORC'];u.mem_write(0x534F40,mrgn)
  w(u,0x6CA754,0);u.mem_write(0x5349E0,bytes(8));w(u,0x672664,0,1);w(u,0x67294D,0,1)
  for owner in range(8):w(u,0x6726D2+owner*36,force[owner]+1,1)
  loc=location if location is not None else row['location']
  if loc:
   left,top,right,bottom,name,mask=struct.unpack_from('<4I2H',mrgn,(loc-1)*20);x=(left+right)//2;y=(top+bottom)//2
  else:x=y=768;mask=0
  return parts,x,y,mask,force
 def construction_world(u,creep=True,minerals=1000):
  # Frozen construction's coherent 128x128 world is caller input, prepared
  # before any original unit creation or resource-consuming admission.
  w(u,0x69B584,0xB00000);w(u,0xB00000,2,2);u.mem_write(0xB0000C,struct.pack('<H',1)*65536)
  region=0xB00000+0x449FC+64;w(u,region,0,2);w(u,region+2,1,2);w(u,region+0xC,region+0x2C)
  w(u,0xB44A0C,200*256);w(u,0xB44A10,200*256);w(u,0xB97A1C,0x80A000)
  for address,offset in ((0x666454,0),(0x666458,0x10000),(0x66696C,0x20000),(0x6CE650,0x40000),(0x66644C,0x50000)):w(u,address,0xC00000+offset)
  w(u,0x54323C,0xC10000);u.mem_write(0xC00000,struct.pack('<I',0x5041F700 if creep else 0x1F700)*16384);w(u,0x67CF60,0xC60000)
  w(u,0x6CA7A0,0xC30000);w(u,0x6CA7AC,128);w(u,0x6CA7B0,128)
  w(u,0x6D319C,2);w(u,0x5C99B0,0);w(u,0x5D6E40,256);w(u,0x5D6E44,160)
  w(u,0x5AEF00,0x80B000);w(u,0x5AEF04,0x80B000+31*20);w(u,0x5AEF08,0)
  for i in range(32):w(u,0x80B000+i*20,0x80B000+(i-1)*20 if i else 0);w(u,0x80B000+i*20+4,0x80B000+(i+1)*20 if i<31 else 0)
  w(u,0x69B574,0x80C000);w(u,0x69B570,0x80C000);w(u,0x80C000,0)
  for owner in range(12):w(u,0x6726D0+owner*36,2,1);w(u,0x534914+owner*12+owner,1,1)
  for owner in range(8):
   at=0x81E000+owner*0x100;u.mem_write(at,bytes(4*52));w(u,0x6B1A74+owner*4,at)
   for index in range(4):w(u,at+index*52,index,2);w(u,at+index*52+4,owner,1)
  w(u,0x5DA390,2);w(u,0x5DA394,2);w(u,0x6663EC,0,1);w(u,0x6751F4,0);w(u,0x6BE650,0,1);w(u,0x665D40,0);w(u,0x665B60,0)
  for at in (0x6C46D4,0x69B568,0x69B564,0x6663E4):w(u,at,10000)
  w(u,0x6A2E70,0);w(u,0x6A5D68,0);w(u,0x6B1A94,0);w(u,0x6B1A98,1);w(u,0x5432F0,0);w(u,0x536720,1,1)
  w(u,0x5263D0+12,minerals);w(u,0x526400+12,1000)
  for provided,used,cap in admission.SUPPLY_TABLES:w(u,provided+12,20);w(u,cap+12,400)
  return [call(u,0x425610),call(u,0x40A0E0),call(u,0x4B44A0)]
 def build_state(u,worker,item,building=0):
  state=portable_state(u,worker,item);state.update(worker_type=r(u,worker+0x64,2),worker_status=r(u,worker+0xDC),worker_order=r(u,worker+0x4D,1),worker_stage=r(u,worker+0x4E,1),worker_signal=r(u,worker+0x4F,1),worker_heading=r(u,worker+0x21,1),queue=list(struct.unpack('<5H',u.mem_read(worker+0x98,10))),minerals=r(u,0x5263D0+12),gas=r(u,0x526400+12),path=r(u,worker+0x100),completed_worker=r(u,0x52C0C4+(r(u,worker+0x64,2)*12+3)*4))
  if building:state['building']=dict(address=building,type=r(u,building+0x64,2),sprite=r(u,building+0xC),completed=bool(r(u,building+0xDC)&1),order=r(u,building+0x4D,1),stage=r(u,building+0x4E,1),hp=r(u,building+8),time=r(u,building+0xAC,2),worker=r(u,building+0x80))
  return state
 def setup_carry(u,executions,worker_type,row,item_owner=11):
  parts,x,y,mask,force=goal_profile(u,row);w(u,0x6CA754,3)
  executions.append(call(u,0x469460,worker_type,x,(y,3)));worker=executions[-1]['return_eax'];assert worker==UNIT
  executions.append(call(u,0x469460,row['type'],x+8,(y,item_owner)));item=executions[-1]['return_eax'];assert item==UNIT+0x150,(worker_type,row,hex(item),snapshot(u),executions[-1])
  for tick in range(32):
   object_step(u,executions)
   if r(u,worker+0xC0)==item:break
  assert r(u,worker+0xC0)==item and r(u,worker+0xE0,1)==4 and r(u,item+0xD4)==worker
  raw=parts[b'TRIG'][row['trigger_index']*2400+row['index']*20:row['trigger_index']*2400+(row['index']+1)*20];assert hashlib.sha256(raw).hexdigest()==row['record_sha256'];condition=0x80F100;u.mem_write(condition,raw)
  return worker,item,condition,(x,y),tick+1
 def query_goal(u,executions,condition):
  executions.append(call(u,0x4B9820));ex=call(u,0x4B8F20,condition);executions.append(ex);return ex['return_eax']
 def walk_to_goal(u,executions,worker,item,condition,origin):
  executions.append(call(u,0x42A340,worker,args=(6,*origin)));timeline=[]
  for tick in range(600):
   object_step(u,executions);state=build_state(u,worker,item);timeline.append(dict(callback=tick+1,state=state))
   if state['carrier_item']==item and state['worker_order']!=6:break
  assert r(u,worker+0xC0)==item and query_goal(u,executions,condition)==1,timeline[-1]
  return timeline
 chrysalis=next(row for row in inventory['goal_conditions'] if row['type']==216)
 if a.case_group in ('all','terran','cancel','failure'):
  for mode in ('complete','cancel','finite_allocation_failure'):
   if a.case_group=='terran' and mode=='finite_allocation_failure':continue
   if a.case_group=='cancel' and mode!='cancel':continue
   if a.case_group=='failure' and mode!='finite_allocation_failure':continue
   u=new(unit_count=2 if mode=='finite_allocation_failure' else 16,thingy_count=32);executions=construction_world(u,creep=False)
   worker,item,condition,origin,pickup=setup_carry(u,executions,7,chrysalis);carry_before=build_state(u,worker,item);assert query_goal(u,executions,condition)==1
   admitted=call(u,0x41B4B0,worker,args=(109,));executions.append(admitted);assert admitted['return_eax']==1 and r(u,0x5263D0+12)==1000
   target=(2624,2432);executions.append(call(u,0x42A340,worker,args=(30,*target)));timeline=[];building=0;paid=None;cancelled=None;completed=None;last=None
   for tick in range(1600):
    object_step(u,executions)
    if not building:
     for at in chain(u,0x665B08+12,next_offset=0x6C):
      if r(u,at+0x64,2)==109:building=at;paid=tick+1
    state=build_state(u,worker,item,building);key=(state['worker_type'],state['worker_order'],state['worker_stage'],state['minerals'],state['building']['completed'] if building else None,state['building']['stage'] if building else None,state['carrier_item'])
    if key!=last:timeline.append(dict(callback=tick+1,state=state));last=key
    assert state['carrier_item']==item and state['carrier_resource']==4 and state['item_carrier']==worker and state['item_hidden']
    if mode=='finite_allocation_failure' and state['minerals']==900 and state['queue'][0]==228 and state['worker_order']==3:break
    if mode=='cancel' and building and state['building']['time']<=580 and cancelled is None:
     before_cancel=state;executions.append(call(u,0x41A340,building));cancelled=tick+1
    if cancelled is not None and r(u,building+0xC)==0 and r(u,0x665B38)==2:break
    if mode=='complete' and building and state['building']['completed']:completed=tick+1;break
   assert (mode=='finite_allocation_failure' and not building) or (mode=='cancel' and cancelled is not None and not r(u,building+0xC)) or (mode=='complete' and completed is not None)
   departed_goal=query_goal(u,executions,condition);assert departed_goal==0
   returned=walk_to_goal(u,executions,worker,item,condition,origin)
   positive_drops=[event for x in executions for event in x.get('carry_construction_events',[]) if event['kind']=='Drop entry' and event.get('carrier_item')==item]
   final=build_state(u,worker,item,building);expected_minerals=975 if mode=='cancel' else 900
   observed=[r(u,0x5263D0+12),r(u,0x526400+12),r(u,worker+0xC0),r(u,worker+0xE0,1),r(u,item+0xD4),len(positive_drops),r(u,0x665B38),query_goal(u,executions,condition)]
   ex=joined(executions)
   record('carried_SCV_paid_Depot_'+mode,dict(worker_type=7,item_type=216,actual_goal_condition=chrysalis,actual_pickup_callback=pickup,original_queue_admission=True,initial_balances=[1000,1000],target_request=list(target),finite_unit_pool2=mode=='finite_allocation_failure',paid_callback=paid,cancelled_callback=cancelled,completed_callback=completed,no_post_start_host_carry_order_position_signal_hp_queue_resource_substitution=True,full_original_object_callback=True),observed,[expected_minerals,1000,item,4,worker,0,3 if mode=='complete' else 2,1],ex,dict(before=carry_before,timeline=timeline,goal_while_departed=departed_goal,natural_return=returned,final=final,native=snapshot(u)))
   print(json.dumps(dict(phase='carried SCV construction retention',mode=mode,paid=paid,cancelled=cancelled,completed=completed,cases=len(cases))),flush=True)
 if a.case_group in ('all','zerg','cancel','failure'):
  for mode in ('complete','cancel','late_stock_failure','admission_failure'):
   if a.case_group=='zerg' and mode not in ('complete','cancel'):continue
   if a.case_group=='cancel' and mode!='cancel':continue
   if a.case_group=='failure' and mode not in ('late_stock_failure','admission_failure'):continue
   row=next(r for r in inventory['goal_conditions'] if r['type']==(128 if mode=='complete' else 129 if mode=='late_stock_failure' else 216 if mode=='cancel' else 217))
   initial=200 if mode=='late_stock_failure' else 199 if mode=='admission_failure' else 1000
   u=new(thingy_count=32);executions=construction_world(u,creep=True,minerals=initial)
   worker,item,condition,origin,pickup=setup_carry(u,executions,41,row,item_owner=6 if row['type']==128 else 0 if row['type']==129 else 11)
   before=build_state(u,worker,item);goal_before=query_goal(u,executions,condition);admitted=call(u,0x41B4B0,worker,args=(142,));executions.append(admitted)
   assert admitted['return_eax']==int(mode!='admission_failure') and r(u,0x5263D0+12)==initial
   if mode=='admission_failure':
    for tick in range(32):object_step(u,executions)
    final=build_state(u,worker,item);ex=joined(executions)
    record('carried_Drone_Pool_admission_rejects199',dict(worker_type=41,item_type=row['type'],initial_minerals=199,original_queue_admission=True,no_build_order_after_rejection=True,no_post_start_host_carry_order_position_signal_hp_queue_resource_substitution=True,full_original_object_callback=True),[r(u,worker+0x64,2),r(u,worker+0xC0),r(u,worker+0xE0,1),r(u,worker+0x98,2),r(u,0x5263D0+12),r(u,0x665B38)],[41,item,4,228,199,2],ex,dict(before=before,after=final,native=snapshot(u)))
    print(json.dumps(dict(phase='carried Drone admission failure',cases=len(cases))),flush=True);continue
   competing=None
   if mode=='late_stock_failure':
    created=call(u,0x469460,111,400,(400,3));executions.append(created);competing=created['return_eax'];assert competing
    charged=call(u,0x41B4B0,competing,args=(0,));executions.append(charged);assert charged['return_eax']==1 and r(u,0x5263D0+12)==150
   # Stock Pool width128 and height64: original center grid alignment.
   target=(384,304) if mode=='complete' else (2656,2448) if mode=='cancel' else (3968,3472)
   executions.append(call(u,0x42A340,worker,args=(25,*target)));timeline=[];converted=None;cancelled=None;shown=None;completed=None;last=None
   for tick in range(1800):
    object_step(u,executions);typ=r(u,worker+0x64,2);state=build_state(u,worker,item,worker if typ==142 else 0)
    key=(typ,state['worker_order'],state['worker_stage'],state['minerals'],state['carrier_item'],state['item_hidden'],state['building']['completed'] if typ==142 else None)
    if key!=last:timeline.append(dict(callback=tick+1,state=state));last=key
    if typ==142 and converted is None:converted=tick+1
    if converted and shown is None and not state['item_hidden']:shown=tick+1
    if mode=='late_stock_failure' and typ==41 and state['worker_order'] in (3,150) and tick>32:break
    if mode=='cancel' and typ==142 and state['building']['time']<=1180 and cancelled is None:
     executions.append(call(u,0x43B380,worker));cancelled=tick+1
    if mode=='cancel' and cancelled is not None and typ==41 and state['worker_status']&1 and not state['item_hidden']:break
    if mode=='complete' and typ==142 and state['building']['completed'] and not state['item_hidden']:completed=tick+1;break
   ex=joined(executions);events=[event for x in executions for event in x.get('carry_construction_events',[])]
   if mode=='late_stock_failure':
    assert converted is None and r(u,worker+0x64,2)==41 and r(u,worker+0xC0)==item and r(u,worker+0xE0,1)==4
    positive_drops=[v for v in events if v['kind']=='Drop entry' and v.get('carrier_item')==item]
    record('carried_Drone_late_Pool_payment_rejected_after_paid_Marine_admission',dict(worker_type=41,item_type=129,initial_minerals200=True,Pool_admitted_without_payment=True,competing_actual_Barracks111=competing,original_paid_Marine0_queue_debits50=True,secondary_Train_not_issued=True,no_host_resource_change=True,target_request=list(target),no_post_start_host_carry_order_position_signal_hp_queue_resource_substitution=True,full_original_object_callback=True),[r(u,0x5263D0+12),r(u,worker+0x64,2),r(u,worker+0xC0),r(u,worker+0xE0,1),len(positive_drops),r(u,0x665B38),r(u,worker+0x98,2),r(u,item+0xD4),bool(r(u,r(u,item+0xC)+0xE,1)&32),r(u,worker+0x4D,1)],[150,41,item,4,0,3,142,worker,True,150],ex,dict(before=before,timeline=timeline,events=events,final=build_state(u,worker,item),native=snapshot(u)))
   else:
    assert converted is not None and shown is not None,(mode,timeline[-5:])
    accepted=[v for v in events if v['kind']=='Drone after Drop'];relocated=[v for v in events if v['kind']=='Drone after item origin relocation'];assert len(accepted)==len(relocated)==1
    assert accepted[0]['item_position']==list(target) and relocated[0]['item_position']==relocated[0]['item_origin']
    after_drop=build_state(u,worker,item,worker if r(u,worker+0x64,2)==142 else 0);goal_after_drop=query_goal(u,executions,condition)
    assert r(u,item+0xD4)==0 and r(u,item+0x4D,1)==97 and not bool(r(u,r(u,item+0xC)+0xE,1)&32)
    returned=walk_to_goal(u,executions,worker,item,condition,origin) if mode=='cancel' else None
    if mode=='cancel':assert r(u,worker+0x64,2)==41 and r(u,worker+0xC0)==item and r(u,worker+0xE0,1)==4
    else:assert r(u,worker+0x64,2)==142 and r(u,worker+0xDC)&1 and r(u,worker+0xC0)==0
    observed=[r(u,0x5263D0+12),r(u,0x526400+12),r(u,worker+0x64,2),r(u,0x665B38),query_goal(u,executions,condition),r(u,0x5D6E48)]
    ex=joined(executions);record('carried_Drone_paid_Pool_Drop_origin_'+mode,dict(worker_type=41,item_type=row['type'],actual_goal_condition=row,item_owner_fixture=6 if row['type']==128 else 11,current_player_fixture=3,original_queue_admission=True,initial_balances=[1000,1000],target_request=list(target),actual_pickup_callback=pickup,converted_callback=converted,shown_callback=shown,cancelled_callback=cancelled,completed_callback=completed,natural_return_and_repickup_after_cancel=mode=='cancel',no_post_start_host_carry_order_position_signal_hp_queue_resource_substitution=True,full_original_object_callback=True),observed,[950 if mode=='cancel' else 800,1000,41 if mode=='cancel' else 142,2,1,0],ex,dict(before=before,goal_before=goal_before,timeline=timeline,events=events,after_drop=after_drop,goal_after_drop=goal_after_drop,natural_return=returned,native=snapshot(u)))
   print(json.dumps(dict(phase='carried Drone build consumer',mode=mode,converted=converted,shown=shown,cancelled=cancelled,completed=completed,cases=len(cases))),flush=True)
 result=dict(target_sha256=local.HASH,target_version='1.17.0.1',probe_script_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),runtime=dict(unicorn=unicorn.__version__,pefile=pefile.__version__,architecture=platform.machine(),python_version=platform.python_version()),retail_manifest_sha256=hashlib.sha256(a.manifest.read_bytes()).hexdigest(),asset_manifest_sha256=hashlib.sha256(a.asset_manifest.read_bytes()).hexdigest(),notification_manifest_sha256=hashlib.sha256(a.notification_manifest.read_bytes()).hexdigest(),network_tbl_sha256=hashlib.sha256(network).hexdigest(),dat_sha256=dat_hashes,source_dependencies_sha256={Path(module.__file__).name:hashlib.sha256(Path(module.__file__).read_bytes()).hexdigest() for module in list(sys.modules.values()) if getattr(module,'__file__',None) and Path(module.__file__).resolve().parent==Path(__file__).resolve().parent and module.__name__!='__main__'},reviewed_code_ranges=[dict(start=hex(lo),end_exclusive=hex(hi)) for lo,hi in sorted(set(ALLOW))],original_code_executed=True,host_imports_executed=False,helper_stubs=[],post_start_host_unit_or_resource_substitutes=False,complete_game_executed=False,complete_cases=sum(c['execution']['complete_return'] for c in cases),prefix_cases=sum(not c['execution']['complete_return'] and not c['execution'].get('fragment_executed') for c in cases),fragment_cases=sum(c['execution'].get('fragment_executed',False) for c in cases),all_passed=all(c['passed'] for c in cases),executed_pc_count=len(all_visited),original_memory_writes=write_count,maps=maps,unit_records_audited=len(corpus),campaign_inputs=inventory,cases=cases,instruction_bound_per_call=PLAN['instruction_bound'],full_original_object_callback_executed=any(c['inputs'].get('full_original_object_callback') for c in cases),outer_accepted_frame_executed=False,host_heading_signal_hp_type_order_location_writes_after_creation=False,total_complete_original_calls=sum(c['execution'].get('complete_call_count',1) for c in cases if c['execution']['complete_return']),total_original_instructions=sum(c['execution']['instruction_count'] for c in cases),full_object_callback_calls=sum(x['callee']=='0x445070' for c in cases for x in c['execution']['calls']),execution_scope='Original carried worker admission/payment/construction/cancel and natural movement; flat explicit terrain/creep/controllers/pools; authentic Bring predicates, whole mission VM/outer accepted frame excluded')
 preparations=[x for c in cases for x in c['execution'].get('preparation_calls',[])]
 result['total_complete_original_calls']+=sum(x['complete_return'] for x in preparations)
 result['total_original_instructions']+=sum(x['instruction_count'] for x in preparations)
 result.update(complete_fragment_preparation_calls=len(preparations),case_group=a.case_group,CTF_delivery_scoring_executed=False,full_trigger_condition_action_vm_executed=False)
 a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({k:result[k] for k in ('complete_cases','prefix_cases','fragment_cases','all_passed','unit_records_audited','executed_pc_count')}))
if __name__=='__main__':main()
