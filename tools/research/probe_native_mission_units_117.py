#!/usr/bin/env python3
"""Probe original mission UNIT creation/defaults using private stock resources.

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
import inspect_native_mission_units_117 as local
import inspect_native_populated_lifecycle_117 as populated
import inspect_native_creep_117 as creep
import probe_native_trigger_creep_117 as creep_source
import inspect_native_harvesting_117 as harvesting
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
ALLOW=list(hidden_source.ALLOW)+list(initialized_source.ALLOW)+list(populated.BLOCKS.values())+list(local.BLOCKS.values())+list(properties.ALLOW)+list(creep.BLOCKS.values())+list(creep_source.BLOCKS.values())+[harvesting.BLOCKS[k] for k in ('sprite_replace_images','reapply_unit_status_overlays','resource_creation_sprite_change')]
PLAN=dict(target_sha256=local.HASH,helper_stubs=[],instruction_bound=100000,scope='Mission record eligibility, actual allocation/completion/defaults and UNIT property application',limits='Synthetic pools and owner state; original resource loading/rendering and full-game execution excluded')
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--exe',type=Path,required=True);p.add_argument('--data',type=Path,required=True);p.add_argument('--manifest',type=Path,required=True);p.add_argument('--assets',type=Path,required=True);p.add_argument('--asset-manifest',type=Path,required=True);p.add_argument('--notification-manifest',type=Path,required=True);p.add_argument('--pyms',type=Path,required=True);p.add_argument('--out',type=Path);p.add_argument('--execute-original-code',action='store_true');a=p.parse_args()
 raw=a.exe.read_bytes();assert hashlib.sha256(raw).hexdigest()==local.HASH
 if not a.execute_original_code:print(json.dumps(PLAN,indent=2));return
 assert a.out and platform.machine().lower() in ('x86_64','amd64')
 assert subprocess.check_output(['git','-C',str(a.pyms),'rev-parse','HEAD'],text=True).strip()=='bfc5d3aad0b5614a5aff72c223f8efa00afddfa4'
 manifest=json.loads(a.manifest.read_text());assetmanifest=json.loads(a.asset_manifest.read_text());dat_hashes={};assets={};lo_assets=[]
 notification_manifest=json.loads(a.notification_manifest.read_text());network=(a.assets/'rez/network.tbl').read_bytes();assert hashlib.sha256(network).hexdigest()==notification_manifest['assets']['rez\\network.tbl']['sha256']
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
 from unicorn import Uc,UC_ARCH_X86,UC_MODE_32,UC_HOOK_CODE,UC_HOOK_MEM_WRITE
 from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_EDX,UC_X86_REG_ESP,UC_X86_REG_ESI,UC_X86_REG_EBP,UC_X86_REG_EDI,UC_X86_REG_EBX
 pe=pefile.PE(data=raw);assert pe.OPTIONAL_HEADER.ImageBase==0x400000 and pe.FILE_HEADER.Machine==0x14C
 image=pe.get_memory_mapped_image();size=(pe.OPTIONAL_HEADER.SizeOfImage+4095)&~4095;cases=[];cache={};all_visited=set();write_count=0
 def w(u,address,value,width=4):u.mem_write(address,(value&((1<<(8*width))-1)).to_bytes(width,'little'))
 def r(u,address,width=4):return int.from_bytes(u.mem_read(address,width),'little')
 def new(unit_count=16,sprite_count=32,image_count=128,seed=12345,rng_enabled=True):
  u=Uc(UC_ARCH_X86,UC_MODE_32);u.mem_map(0x400000,size);u.mem_write(0x400000,image)
  for addr,length in ((0x800000,0x20000),(STACK,0x4000),(STOP,0x1000),(SCRIPT,0x10000),(ASSETS,0x600000),(0xB00000,0xA0000),(0xC00000,0x80000),(0x1400000,0x100000)):u.mem_map(addr,length)
  u.mem_write(SCRIPT,script);w(u,0x54330C,SCRIPT)
  at=ASSETS;allocated={}
  for ident,(row,v) in assets.items():
   if row['path'] not in allocated:u.mem_write(at,v);allocated[row['path']]=at;at+=(len(v)+4095)&~4095
   w(u,0x5436F8+ident*4,allocated[row['path']])
  for row,v in lo_assets:
   if row['path'] not in allocated:u.mem_write(at,v);allocated[row['path']]=at;at+=(len(v)+4095)&~4095
   if row['kind']<5:w(u,0x596400+(row['kind']*999+row['image_id'])*4,allocated[row['path']])
  u.mem_write(0x81C000,network);w(u,0x665DE0,0x81C000)
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
  w(u,0x508D50,0);w(u,0x67CC90,0);w(u,0x6726D0+3*36,2,1)
  return u
 def guest_write(u,access,address,length,value,_):
  nonlocal write_count
  writable=[(0x800000,0x820000),(STACK,STACK+0x4000),(SCRIPT,SCRIPT+0x10000),(ASSETS,ASSETS+0x600000),(0xB00000,0xBA0000),(0xC00000,0xC80000),(0x1400000,0x1500000)]
  writable += [(0x400000+section.VirtualAddress,0x400000+section.VirtualAddress+max(section.Misc_VirtualSize,section.SizeOfRawData)) for section in pe.sections if not section.Characteristics&0x20000000]
  assert any(lo<=address and address+length<=hi for lo,hi in writable),('Undeclared guest write',hex(address),length)
  write_count+=1
 def call(u,start,ecx=0,edx=0,args=(),stop_before=None,fragment=False,registers=None):
  visited=set();calls=[];script_ops=[];instructions=0;boundary=False;prefix_pc=None;recent=[];entries={lo for lo,hi in ALLOW}
  def hook(m,addr,width,_):
   nonlocal instructions,boundary,prefix_pc
   if stop_before and addr in stop_before:prefix_pc=addr;m.emu_stop();return
   if addr==STOP:boundary=True;m.emu_stop();return
   key=(addr,width)
   if key not in cache:cache[key]=tuple(lo for lo,hi in ALLOW if lo<=addr and addr+width<=hi)
   if not cache[key]:raise RuntimeError(f'Outside reviewed original code {addr:#x}; recent={recent[-15:]}')
   visited.update(cache[key]);all_visited.add(addr);instructions+=1;recent.append(hex(addr))
   if addr==0x4104C4:
    pc=m.reg_read(UC_X86_REG_ESI);script_ops.append(dict(offset=pc-SCRIPT,opcode=r(m,pc,1)))
   if addr in entries:
    sp=m.reg_read(UC_X86_REG_ESP);calls.append(dict(callee=hex(addr),ecx=m.reg_read(UC_X86_REG_ECX),edx=m.reg_read(UC_X86_REG_EDX),stack=[r(m,sp+4+i*4) for i in range(6)]))
  h=u.hook_add(UC_HOOK_CODE,hook);wh=u.hook_add(UC_HOOK_MEM_WRITE,guest_write);sp=STACK+0x3000;u.mem_write(sp,struct.pack('<'+'I'*(len(args)+1),STOP,*args));u.reg_write(UC_X86_REG_ESP,sp);u.reg_write(UC_X86_REG_ECX,ecx);u.reg_write(UC_X86_REG_EDX,edx)
  if registers:
   for register,value in registers.items():u.reg_write(register,value)
  try:u.emu_start(start,STOP+1,count=100000)
  except Exception as e:raise RuntimeError(f'{e}; recent={recent[-30:]}; calls={calls[-10:]}') from e
  finally:u.hook_del(h);u.hook_del(wh)
  if prefix_pc is None:assert boundary and u.reg_read(UC_X86_REG_ESP)==sp+4*(len(args)+1)
  else:assert not boundary
  return dict(complete_return=boundary,complete_original_function=boundary and not fragment,fragment_executed=fragment,boundary_va=None if prefix_pc is None else hex(prefix_pc),return_eax=u.reg_read(UC_X86_REG_EAX),instruction_count=instructions,calls=calls,iscript_opcodes=script_ops,visited_functions=[hex(x) for x in sorted(visited)])
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
  cases.append(dict(case=name,inputs=inputs,actual=actual,expected=expected,passed=True,execution=ex,native_state=state))
 def chain(u,head,next_offset=4):
  result=[];at=r(u,head)
  while at:result.append(at);at=r(u,at+next_offset);assert len(result)<=64
  return result
 def advance(seed,n):
  for i in range(n):seed=(seed*22695477+1)&0xFFFFFFFF
  return seed
 def joined(executions):
  return dict(complete_return=all(x['complete_return'] for x in executions),complete_call_count=len(executions),return_eax=executions[-1]['return_eax'],instruction_count=sum(x['instruction_count'] for x in executions),calls=[c for x in executions for c in x['calls']],iscript_opcodes=[c for x in executions for c in x['iscript_opcodes']],visited_functions=sorted({c for x in executions for c in x['visited_functions']}))
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
  for addr,offset in ((0x666454,0),(0x666458,0x10000),(0x66696C,0x20000),(0x6CE650,0x40000),(0x66644C,0x50000)):
   w(u,addr,0xC00000+offset)
  w(u,0x54323C,0xC10000);u.mem_write(0xC00000,struct.pack('<I',0x1FFFF)*16384);w(u,0x67CF60,0xC60000)
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
 selected=[x for x in corpus if x['map']=='campaign\\zerg\\tutorial\\staredit\\scenario.chk']
 assert len(selected)==6
 for entry in selected:
  if entry['type'] in (214,131):continue
  u=flat_fixture(new());v=entry['_bytes'];u.mem_write(0x800000,v)
  ex=call(u,0x469460,entry['type'],entry['x'],(entry['y'],entry['owner']));unit=ex['return_eax'];assert unit==UNIT
  prop_ex=call(u,0x46A420,unit,0x80000C);state=snapshot(u);row=tables['units'][entry['type']]
  observed=dict(type=r(u,unit+0x64,2),owner=r(u,unit+0x4C,1),health=r(u,unit+8),shields=r(u,unit+0x60),build_timer=r(u,unit+0xAC,2),complete=bool(r(u,unit+0xDC)&1),hidden=bool(r(u,r(u,unit+0xC)+0xE,1)&32),primary_order=r(u,unit+0x4D,1),ai=r(u,unit+0x134),spatial_borders=r(u,0x5C99B0))
  expected=dict(type=entry['type'],owner=entry['owner'],health=row.hit_points.whole*256+row.hit_points.fraction,shields=row.shield_amount*256,build_timer=0,complete=True,hidden=False,primary_order=row.human_ai_idle,ai=0,spatial_borders=2)
  record('stock_tutorial_connected_record_'+str(entry['record_index']),{k:v for k,v in entry.items() if k!='_bytes'},observed,expected,joined([ex,prop_ex]),state)
 # Smallest supported-version stock map: real mobile UNIT records, stock
 # UNIS default flags, and complete creation followed by the real properties.
 supported=[entry for entry in corpus if entry['map']=='campaign\\terran\\tutorial\\staredit\\scenario.chk' and entry['type'] in (0,7,37)]
 assert len(supported)==33
 for entry in supported:
  v=entry['_bytes'];assert not struct.unpack_from('<H',v,26)[0]&15
  u=flat_fixture(new());u.mem_write(0x800000,v);ex=call(u,0x469460,entry['type'],entry['x'],(entry['y'],entry['owner']));assert ex['return_eax']==UNIT
  properties_ex=call(u,0x46A420,UNIT,0x80000C);row=tables['units'][entry['type']];maximum=row.hit_points.whole*256+row.hit_points.fraction;valid=struct.unpack_from('<H',v,14)[0]
  hp=min(maximum,max(1,((maximum*v[17])&0xFFFFFFFF)//100)) if valid&2 else maximum
  invincible=bool(struct.unpack_from('<H',v,12)[0]&struct.unpack_from('<H',v,26)[0]&16)
  observed=[r(u,UNIT+0x64,2),r(u,UNIT+0x4C,1),r(u,UNIT+8),r(u,UNIT+0xDC)&0x4000001,r(u,UNIT+0x4D,1),bool(r(u,r(u,UNIT+0xC)+0xE,1)&32)]
  record('supported_tutorial_connected_record_'+str(entry['record_index']),{k:value for k,value in entry.items() if k!='_bytes'},observed,[entry['type'],entry['owner'],hp,0x4000001 if invincible else 1,row.human_ai_idle,False],joined([ex,properties_ex]))
 # Exact complete per-record creation is separate from the original outer
 # section loop's Storm allocation. Controller table values are already-set
 # runtime inputs; raw OWNR values are not silently normalized here.
 # A real Hatchery allocation reaches CRT context acquisition while selecting
 # creep graphics. Stop before the excluded OS import, without substituting it.
 entry=next(x for x in selected if x['type']==131);u=flat_fixture(new());u.mem_write(0x800000,entry['_bytes'])
 for at,pointer in ((0x6CA7A0,0xC20000),(0x6CA7A4,0xC30000),(0x6CA7A8,0xC40000)):w(u,at,pointer)
 w(u,0x6CA7AC,64);w(u,0x6CA7B8,0);w(u,0x6CA7C0,0);w(u,0x6CA7C4,0)
 ex=call(u,0x469460,131,entry['x'],(entry['y'],entry['owner']),stop_before={0x4D7466})
 record('stock_hatchery_creation_creep_import_prefix',{k:v for k,v in entry.items() if k!='_bytes'},[ex['boundary_va'],r(u,0x665B38),r(u,UNIT+0x64,2),r(u,UNIT+0x4C,1)],['0x4d7466',1,131,3],ex,snapshot(u))
 # Allocation/bounds failure reaches real network.tbl lookup and the
 # unchanged owner-message gate. Nonlocal viewer selection excludes UI imports.
 for kind,error_id in (('unit_pool_empty',62),('sprite_pool_empty',62),('unit_limit',61),('outside_map',0)):
  u=flat_fixture(new(unit_count=0 if kind=='unit_pool_empty' else 16,sprite_count=0 if kind=='sprite_pool_empty' else 32))
  if kind=='unit_limit':w(u,0x665B38,1700)
  before=[r(u,0x665AF0),r(u,0x665B38)];ex=call(u,0x469460,0,3000 if kind=='outside_map' else 512,(512,6))
  ids=[c['ecx'] for c in ex['calls'] if c['callee']=='0x43f0c0']
  observed=[ex['return_eax'],r(u,0x665AF0),r(u,0x665B38),r(u,0x5DA398,2),r(u,0x673940+14*4),r(u,0x67393C),ids]
  record('complete_record_failure_'+kind,dict(type=0,owner=6,local_viewer=0,fixture_failure=kind,network_tbl='Hash-verified original rez\\network.tbl'),observed,[0,*before,0,1,advance(12345,1),[error_id] if error_id else []],ex)
 # Stock expansion-only availability flag rejects before allocation/RNG.
 for typ,row in enumerate(tables['units']):
  if not row.staredit_availability_flags&0x200:continue
  u=new();w(u,0x536720,0,1);ex=call(u,0x469460,typ,512,(512,3))
  observed=[ex['return_eax'],r(u,0x665B38),r(u,0x665AF0),r(u,0x67393C),r(u,0x673940+14*4)]
  record('stock_expansion_type_reject_'+str(typ),dict(type=typ,stock_availability_flags=row.staredit_availability_flags,expansion_enabled=False),observed,[0,0,UNIT,12345,0],ex)
 # The complete outer loader rejects malformed byte lengths before allocation.
 for length in list(range(1,73))+[0xFFFFFFFF,0xFFFFFFFE]:
  if length%36==0:continue
  u=new();ex=call(u,0x468F30,args=(0x800000,length,0));record('unit_section_length_reject_'+str(length),dict(bytes=length),ex['return_eax'],0,ex)
 # Start locations are complete original calls. A later same-owner record
 # overwrites the earlier location; this is distinct from unit serial matching.
 u=new();starts=0x800200;u.mem_write(starts,bytes(48));executions=[]
 for x,y in ((64,96),(1664,304),(4095,1)):
  v=bytearray(36);struct.pack_into('<3H',v,4,x,y,214);v[16]=3;u.mem_write(0x800000,bytes(v));w(u,0x508D50,3)
  executions.append(call(u,0x46A280,0x800000,starts))
 record('start_location_last_record_wins',dict(owner=3,locations=[[64,96],[1664,304],[4095,1]],source='Synthetic duplicate marker records'),[r(u,starts+12,2),r(u,starts+14,2),r(u,0x5264B0,2),r(u,0x5264B2,2)],[4095,1,117,0],joined(executions))
 # The original trap predicate distinguishes floor/wall traps from doors.
 # These are complete pure calls over stock type IDs, without graphics loading.
 u=new();disabled_types={203,209,210,211,212,213}
 for typ in range(228):
  w(u,UNIT+0x64,typ,2);ex=call(u,0x429160,UNIT)
  record('special_disabled_type_'+str(typ),dict(stock_type_id=typ),ex['return_eax'],int(typ in disabled_types),ex)
 # Full completion-post entry on an explicit already-visible object fixture.
 # This establishes type-dependent postfields/default order, not construction
 # of the missing special graphics or later trap/door/powerup primary behavior.
 for typ in list(range(203,214))+list(range(215,228)):
  u=flat_fixture(new());row=tables['units'][typ];w(u,UNIT+0x64,typ,2);w(u,UNIT+0x4C,3,1);w(u,UNIT+0xC,SPRITE)
  w(u,UNIT+8,row.hit_points.whole*256+row.hit_points.fraction);w(u,UNIT+0xDC,0x4000000);w(u,UNIT+0xE4,0x12345678);w(u,UNIT+0x91,0x55,1);w(u,UNIT+0x4D,23,1)
  w(u,SPRITE+0x14,512,2);w(u,SPRITE+0x16,512,2);w(u,SPRITE+0x18,IMAGE);w(u,SPRITE+0x1C,0);w(u,SPRITE+0xE,0,1)
  ex=call(u,0x424DE0,UNIT);is_trap=typ in disabled_types
  observed=[bool(r(u,UNIT+0xDC)&1),r(u,UNIT+0xDC)&0x300,r(u,UNIT+0xE4),r(u,UNIT+0x91,1),r(u,UNIT+0x4D,1)]
  expected=[True,0x300 if is_trap else 0,0x80000000 if is_trap else 0x12345678,0 if is_trap else 0x55,row.human_ai_idle]
  record('complete_special_post_'+str(typ),dict(type=typ,source='Actual stock DAT; caller-prepared visible object with empty image list',initial_status=0x4000000,initial_visibility_like_field=0x12345678,initial_byte91=0x55),observed,expected,ex)
 for typ in (0,41):
  for owner in range(12):
   for controller in (1,2,3,4,7):
    controllers=[2]*12;controllers[owner]=controller;u=flat_fixture(new(),controllers)
    ex=call(u,0x469460,typ,512,(512,owner));assert ex['return_eax']==UNIT
    row=tables['units'][typ];ai_expected=(0x14F0000+owner*0x20) if controller==1 and not (r(u,0x521578+typ,1)&2) else 0
    order_expected=161 if controller==3 else 162 if controller==7 else row.comp_ai_idle if controller==1 else row.human_ai_idle
    observed=[r(u,UNIT+0x4C,1),r(u,UNIT+0x4D,1),r(u,UNIT+0x134),r(u,UNIT+8),r(u,UNIT+0xAC,2),bool(r(u,UNIT+0xDC)&1),r(u,0x665B08+owner*4),r(u,0x5C99B0)]
    record(f'complete_record_defaults_{typ}_owner_{owner}_controller_{controller}',dict(type=typ,owner=owner,runtime_controller=controller,source='Stock DAT type with explicit caller runtime-controller table'),observed,[owner,order_expected,ai_expected,row.hit_points.whole*256+row.hit_points.fraction,0,True,UNIT,2],ex)
 # Execute the full native setup-to-runtime copy on all 68 actual OWNR
 # inputs, while keeping every other player-record field explicit synthetic.
 u=new();copy_cases={}
 for maprow in maps:
  setup=bytearray(12*36)
  for owner,controller in enumerate(maprow['ownr']):
   struct.pack_into('<2I',setup,owner*36,owner,0xFFFFFFFF);setup[owner*36+8]=controller;setup[owner*36+9]=owner%3;setup[owner*36+10]=owner%4
  u.mem_write(0x673D48,bytes(setup));u.mem_write(0x6726C8,bytes([0xA5])*len(setup));ex=call(u,0x479BF0)
  observed=bytes(u.mem_read(0x6726C8,len(setup)));assert observed==setup
  label='setup_copy_'+str(len(copy_cases));record(label,dict(map=maprow['path'],map_sha256=maprow['sha256'],stock_ownr=maprow['ownr'],other_player_record_bytes='Declared synthetic ID/storm-ID/race/force fields'),hashlib.sha256(observed).hexdigest(),hashlib.sha256(setup).hexdigest(),ex)
  maprow['setup_copy_case']=len(cases)-1;copy_cases[maprow['path']]=label
 # Complete pure copies and computer-slot producers use unsigned low-byte IDs.
 for owner in range(12):
  u=new();pattern=bytes((owner*19+k*7)&255 for k in range(36));u.mem_write(0x673D48+owner*36,pattern);u.mem_write(0x6726C8,bytes(432))
  ex=call(u,0x479C40,0xABCD0000|owner);record('setup_copy_one_'+str(owner),dict(index_low_byte=owner,upper_bits=0xABCD0000),bytes(u.mem_read(0x6726C8+owner*36,36)).hex(),pattern.hex(),ex)
 u=new();executions=[call(u,0x479C80)]
 for owner in range(12):executions.append(call(u,0x478A20,owner))
 record('computer_slot_native_set_bounds',dict(player_ids=list(range(12))),list(u.mem_read(0x673F08,8)),[1]*8,joined(executions))
 # The original record-admission branch is a bounded LOOP FRAGMENT with real
 # complete predicate callees. It never reaches allocation. Both stock OWNR
 # (still pre-lobby placeholders) and a declared all-human runtime profile are
 # audited, with equal decoder arguments deduplicated across all physical CHKs.
 filter_cases={};u=new();starts=0x800200;frame=STACK+0x2000;valid_neutrals={176,177,178,188,89,90,93,94,95,96}
 def eligibility(entry,controller,mode=0,ums=True,computer=False,neutral_group=True,source='campaign_tuple'):
  assert not (mode and ums), 'Profile byte672949 is also UMS+79; contradictory fixture'
  typ=entry['type'];owner=entry['owner'];marker=(typ==214)
  key=(typ,owner,controller,mode,ums,computer,neutral_group,entry['x'] if marker else 0,entry['y'] if marker else 0)
  if key in filter_cases:return filter_cases[key]
  u.mem_write(0x800000,entry['_bytes']);u.mem_write(starts,bytes(48));w(u,frame+8,starts)
  for player in range(12):w(u,0x6726D0+player*36,controller,1);w(u,0x673F08+player,int(computer and player<8),1)
  w(u,0x672949,mode,1);w(u,0x6728D0+0x75,int(not ums),1);w(u,0x6728D0+0x79,mode,1);w(u,0x6728D0+0x7F,0,1)
  w(u,0x523C40+typ,128 if neutral_group else 0,1);w(u,0x508D50,0)
  ex=call(u,0x468FD3,starts,fragment=True,stop_before={0x469038,0x469085},registers={UC_X86_REG_EBP:frame,UC_X86_REG_ESI:0x800010,UC_X86_REG_EDI:1})
  admitted=not marker and (owner>=8 or controller in (1,2,3,4,7)) and (not mode or owner==11 and typ in valid_neutrals) and (not (ums and computer and owner<8) or neutral_group)
  record('record_admission_'+str(len(filter_cases)),dict(type=typ,owner=owner,runtime_controller=controller,mode=mode,ums=ums,computer_slot=computer,neutral_group_bit=neutral_group,source=source,record_sha256=entry['record_sha256']),ex['boundary_va'],'0x469038' if admitted else '0x469085',ex)
  filter_cases[key]=len(cases)-1;return len(cases)-1
 for maprow in maps:
  rows=[entry for entry in corpus if entry['map']==maprow['path']];refs=[]
  for entry in rows:
   rawcontroller=maprow['ownr'][entry['owner']]
   refs.append(dict(section=entry['section_index'],record=entry['record_index'],record_sha256=entry['record_sha256'],raw_setup_case=eligibility(entry,rawcontroller,neutral_group=bool(tables['units'][entry['type']].staredit_group_flags&128),source='actual OWNR copied without lobby normalization'),prepared_human_case=eligibility(entry,2,neutral_group=bool(tables['units'][entry['type']].staredit_group_flags&128),source='declared all-human runtime profile')))
  maprow['record_admission_case_refs']=refs
 seed=dict(selected[0])
 for typ in (0,89,90,93,94,95,96,176,177,178,188,214):
  for owner in (3,8,11):
   entry=dict(seed);v=bytearray(seed['_bytes']);struct.pack_into('<H',v,8,typ);v[16]=owner;entry.update(type=typ,owner=owner,_bytes=bytes(v),record_sha256=hashlib.sha256(v).hexdigest())
   for controller in range(9):
    for mode,ums,computer,group in ((0,True,False,True),(0,True,True,False),(0,False,True,False),(1,False,False,True)):
     eligibility(entry,controller,mode,ums,computer,group,source='Synthetic discriminating eligibility tuple')
 # Connected native outer processing loop on explicit already-allocated buffers.
 # The duplicate serial is not consumed by either guard-selected relation helper;
 # both records create separate original objects in input order.
 u=flat_fixture(new());start=next(x for x in selected if x['type']==214 and x['owner']==6);marine=next(x for x in selected if x['type']==0)
 v=bytearray(marine['_bytes']);struct.pack_into('<2H',v,4,512,512);block=start['_bytes']+bytes(v)+bytes(v)
 u.mem_write(0x800000,block);u.mem_write(starts,bytes(32));w(u,frame+8,starts);w(u,frame+12,0x800000);w(u,frame-4,0)
 ex=call(u,0x468FC5,starts,fragment=True,stop_before={0x4D0022},registers={UC_X86_REG_EBP:frame,UC_X86_REG_EDI:3,UC_X86_REG_EBX:0})
 observed=[ex['boundary_va'],r(u,0x665B38),r(u,UNIT+0x64,2),r(u,UNIT+0x150+0x64,2),r(u,0x534A00+6*4,2),r(u,0x534A02+6*4,2),r(u,0x5C99B0)]
 record('outer_processing_duplicate_serial',dict(source='Actual stock start + duplicate actual Marine record with explicitly changed XY',serial=marine['serial'],records=3,buffer_preparation='Host-preallocated; original Storm allocation/copy not executed'),observed,['0x4d0022',2,0,0,start['x'],start['y'],4],ex,snapshot(u))
 # Initial Nydus relation pairing occurs before imported node disposal.
 # Original-created objects make pointer writes concrete; the relation-node
 # class itself remains an explicit fixture, not a claimed Nydus allocator.
 third=call(u,0x469460,0,800,(800,6));assert third['return_eax']==UNIT+2*0x150
 record('native_relation_fixture_third_object',dict(type=0,owner=6,source='Actual original creation after the processing-loop fixture'),[third['return_eax'],r(u,0x665B38),r(u,UNIT+2*0x150+0x64,2)],[UNIT+2*0x150,3,0],third)
 for matched in (False,True):
  n1,n2,n3=0x800400,0x800420,0x800440
  for unit in (UNIT,UNIT+0x150,UNIT+2*0x150):w(u,unit+0xD0,0xA5A5A5A5)
  # Both earlier nodes can match; the nearest previous node wins.
  u.mem_write(n1,struct.pack('<5I',42,27,UNIT,n2,0));u.mem_write(n2,struct.pack('<5I',27,42 if matched else 99,UNIT+0x150,n3,n1));u.mem_write(n3,struct.pack('<5I',28,42 if matched else 99,UNIT+2*0x150,0,n2))
  ex=call(u,0x4698E0,n1,stop_before={0x4D0022});observed=[ex['boundary_va'],r(u,UNIT+0xD0),r(u,UNIT+0x150+0xD0),r(u,UNIT+2*0x150+0xD0),r(u,n2+0x10)]
  expected=['0x4d0022',UNIT+0x150 if matched else 0xA5A5A5A5,UNIT if matched else 0xA5A5A5A5,0xA5A5A5A5,0]
  record('nydus_pairing_before_free_'+str(int(matched)),dict(source='Declared three-node relation list over original-created Marines; resolver itself does not inspect unit types',matching_relation=matched,serial=42,duplicate_match_policy='Nearest prior list node'),observed,expected,ex)
 result=dict(target_sha256=local.HASH,probe_script_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),runtime=dict(unicorn=unicorn.__version__,pefile=pefile.__version__,architecture=platform.machine(),python_version=platform.python_version()),retail_manifest_sha256=hashlib.sha256(a.manifest.read_bytes()).hexdigest(),asset_manifest_sha256=hashlib.sha256(a.asset_manifest.read_bytes()).hexdigest(),notification_manifest_sha256=hashlib.sha256(a.notification_manifest.read_bytes()).hexdigest(),network_tbl_sha256=hashlib.sha256(network).hexdigest(),dat_sha256=dat_hashes,source_dependencies_sha256={Path(module.__file__).name:hashlib.sha256(Path(module.__file__).read_bytes()).hexdigest() for module in list(sys.modules.values()) if getattr(module,'__file__',None) and Path(module.__file__).resolve().parent==Path(__file__).resolve().parent and module.__name__!='__main__'},reviewed_code_ranges=[dict(start=hex(lo),end_exclusive=hex(hi)) for lo,hi in sorted(set(ALLOW))],original_code_executed=True,host_imports_executed=False,helper_stubs=[],complete_game_executed=False,complete_cases=sum(c['execution']['complete_return'] for c in cases),prefix_cases=sum(not c['execution']['complete_return'] and not c['execution'].get('fragment_executed') for c in cases),fragment_cases=sum(c['execution'].get('fragment_executed',False) for c in cases),all_passed=all(c['passed'] for c in cases),executed_pc_count=len(all_visited),original_memory_writes=write_count,maps=maps,unit_records_audited=len(corpus),stock_records_with_relation=sum(bool(entry['relation']) for entry in corpus),stock_relation_type_histogram={str(typ):sum(entry['type']==typ and bool(entry['relation']) for entry in corpus) for typ in sorted({entry['type'] for entry in corpus if entry['relation']})},cases=cases)
 a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({k:result[k] for k in ('complete_cases','prefix_cases','fragment_cases','all_passed','unit_records_audited','executed_pc_count')}))
if __name__=='__main__':main()
