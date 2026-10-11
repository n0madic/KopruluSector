#!/usr/bin/env python3
"""Probe original campaign-special order transitions on native-created objects.

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
import inspect_native_special_orders_117 as local
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
ALLOW=list(hidden_source.ALLOW)+list(initialized_source.ALLOW)+list(populated.BLOCKS.values())+list(local.BLOCKS.values())+list(special.BLOCKS.values())+[harvesting.BLOCKS[k] for k in ('cargo_overlay_remove',)]+[death.BLOCKS[k] for k in ('hidden_unit_add','active_unit_remove')]+[geometry.BLOCKS[k] for k in ('image_follow_main_offset',)]+list(movement.ALLOW)+list(mission.BLOCKS.values())+list(properties.ALLOW)+list(creep.BLOCKS.values())+list(creep_source.BLOCKS.values())+[harvesting.BLOCKS[k] for k in ('sprite_replace_images','reapply_unit_status_overlays','resource_creation_sprite_change')]
PLAN=dict(target_sha256=local.HASH,helper_stubs=[],instruction_bound=100000,scope='Original door/trap/powerup/rescue order consumers on native-created stock resource objects',limits='Synthetic pools and owner state; original resource loading/rendering and full-game execution excluded')
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--exe',type=Path,required=True);p.add_argument('--data',type=Path,required=True);p.add_argument('--manifest',type=Path,required=True);p.add_argument('--assets',type=Path,required=True);p.add_argument('--asset-manifest',type=Path,required=True);p.add_argument('--notification-assets',type=Path,required=True);p.add_argument('--notification-manifest',type=Path,required=True);p.add_argument('--pyms',type=Path,required=True);p.add_argument('--out',type=Path);p.add_argument('--execute-original-code',action='store_true');a=p.parse_args()
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
 for typ in (205,206,207,208):
  for handler,order in ((0x428150,169),(0x4281A0,168)):
   for signal in (0,1,2,3,255):
    u=flat_fixture(new());created=call(u,0x469460,typ,768,(768,3));assert created['return_eax']==UNIT
    issued=call(u,0x42A2C0,UNIT,args=(order,));w(u,UNIT+0x4F,signal,1);before=r(u,UNIT+0xDC);elevation=r(u,r(u,UNIT+0xC)+0xD,1)
    ex=call(u,handler,UNIT)
    observed=[r(u,UNIT+0x4F,1),r(u,UNIT+0xDC)&0x200400,r(u,r(u,UNIT+0xC)+0xD,1),r(u,UNIT+0x4D,1),r(u,0x69B560)]
    changed=bool(signal&1);row=tables['units'][typ];opened=handler==0x4281A0
    expected=[signal&254 if changed else signal,(0x200400 if opened else 0) if changed else before&0x200400,1 if changed and opened else row.elevation_level if changed else elevation,row.return_to_idle if changed else order,1 if changed and opened else 0]
    record('door_signal_'+str(typ)+'_'+str(order)+'_'+str(signal),dict(type=typ,order=order,signal=signal,source='Actual469460 creation then original order issuance; private flat spatial terrain'),observed,expected,joined([created,issued,ex]),snapshot(u))
 # Real stock animation produces the signal; no host-written signal is used.
 for typ in (205,206,207,208):
  for handler,source_order,target_order in ((0x428010,173,168),(0x4281F0,172,169)):
   u=flat_fixture(new());created=call(u,0x469460,typ,768,(768,3));issued=call(u,0x42A2C0,UNIT,args=(source_order,));w(u,0x543300,UNIT);w(u,0x543304,UNIT);started=call(u,handler,UNIT)
   assert r(u,UNIT+0x4D,1)==target_order
   animation=[];ticks=0
   while not r(u,UNIT+0x4F,1)&1:
    animation.append(call(u,0x40CF50,UNIT));animation.append(call(u,0x418B20,UNIT));ticks+=1;assert ticks<256,(typ,r(u,UNIT+0x4D,1),r(u,UNIT+0x4F,1),r(u,UNIT+0x21,1),snapshot(u))
   finished=call(u,0x4281A0 if target_order==168 else 0x428150,UNIT)
   expected=[0,0x200400 if target_order==168 else 0,1 if target_order==168 else tables['units'][typ].elevation_level,23]
   observed=[r(u,UNIT+0x4F,1)&1,r(u,UNIT+0xDC)&0x200400,r(u,r(u,UNIT+0xC)+0xD,1),r(u,UNIT+0x4D,1)]
   record('door_stock_animation_signal_'+str(typ)+'_'+str(source_order),dict(type=typ,initial_order=source_order,followup_order=target_order,stock_animation_ticks=ticks,sound_backend_enabled=False,iscript_current_unit_context={"0x543300":UNIT,"0x543304":UNIT}),observed,expected,joined([created,issued,started]+animation+[finished]),snapshot(u))
 # Powerup idle executes the original sorted search/predicate and pickup on a
 # genuine native-created stock SCV, rather than a host-prepared worker record.
 for typ in [128,129]+list(range(215,228)):
  u=flat_fixture(new());worker=call(u,0x469460,7,768,(768,0));powerup=call(u,0x469460,typ,784,(768,11));ex=call(u,0x427290,UNIT+0x150)
  observed=[r(u,UNIT+0xC0),r(u,UNIT+0xE0,1),r(u,UNIT+0x150+0xD4),r(u,UNIT+0x150+0x4D,1),bool(r(u,r(u,UNIT+0x150+0xC)+0xE,1)&32),chain(u,0x665AE0),chain(u,0x665AF8),r(u,0x5D6E48)]
  carried=r(u,r(u,UNIT+0xC)+0x1C)
  observed += [r(u,carried+8,2),bool(r(u,carried+0xC,2)&0x80),list(struct.unpack('<2b',u.mem_read(carried+0xE,2)))]
  expected=[UNIT+0x150,4,UNIT,23,True,[UNIT],[UNIT+0x150],0,958 if typ==128 else 959 if typ==129 else typ+177,True,[16,-8]]
  record('powerup_native_worker_pickup_'+str(typ),dict(type=typ,worker_type=7,worker_owner=0,powerup_owner=11,xy=[[768,768],[784,768]],resource_cargo=0),observed,expected,joined([worker,powerup,ex]),snapshot(u))
 # The predicate is a complete original leaf. Status mutations below are
 # declared caller states, not claims that their gameplay producers execute.
 profiles=[('worker',7,216,0,None,0,1),('incomplete',7,216,0,'incomplete',0,0),
  ('burrowed',7,216,0,'burrowed',0,0),('hallucination',7,216,0,'hallucination',0,0),
  ('occupied',7,216,0,None,UNIT+0x150,0),('nonworker',0,216,0,None,0,0),
  ('same_owner_flag',7,215,11,None,0,0),('different_owner_flag',7,215,0,None,0,1),
  ('same_owner_chrysalis',7,216,11,None,0,1)]
 for name,worker_type,typ,owner,status,cargo,expected in profiles:
  u=flat_fixture(new());one=call(u,0x469460,worker_type,768,(768,owner));two=call(u,0x469460,typ,784,(768,11))
  original=r(u,UNIT+0xDC)
  if status=='incomplete':w(u,UNIT+0xDC,original&~1)
  elif status=='burrowed':w(u,UNIT+0xDC,original|0x10)
  elif status=='hallucination':w(u,UNIT+0xDC,original|0x40000000)
  w(u,UNIT+0xC0,cargo)
  ex=call(u,0x431980,UNIT,UNIT+0x150)
  record('powerup_worker_predicate_'+name,dict(worker_type=worker_type,powerup_type=typ,worker_owner=owner,powerup_owner=11,caller_status=status,caller_cargo=cargo),ex['return_eax'],expected,joined([one,two,ex]))
  if name in ('same_owner_flag','nonworker'):
   u=flat_fixture(new());one=call(u,0x469460,worker_type,768,(768,owner));two=call(u,0x469460,typ,784,(768,11))
   idle=call(u,0x427290,UNIT+0x150)
   observed=[r(u,UNIT+0xC0),r(u,UNIT+0x150+0xD4),r(u,UNIT+0x150+0x4D,1),bool(r(u,r(u,UNIT+0x150+0xC)+0xE,1)&32),r(u,0x5D6E48)]
   record('powerup_idle_reject_'+name,dict(worker_type=worker_type,powerup_type=typ,worker_owner=owner,powerup_owner=11),observed,[0,0,97,False,0],joined([one,two,idle]))
 # Trap initialization and no-enemy retry use the native target search.
 for typ in (203,209,210,211,212,213):
  u=flat_fixture(new());created=call(u,0x469460,typ,768,(768,3));init=call(u,0x428490,UNIT);retry=call(u,0x428490,UNIT)
  expected=[1,15,0x200300,0 if typ in (203,209) else tables['units'][typ].elevation_level,0x80000000,0,0]
  observed=[r(u,UNIT+0x4E,1),r(u,UNIT+0x54,1),r(u,UNIT+0xDC)&0x200300,r(u,r(u,UNIT+0xC)+0xD,1),r(u,UNIT+0xE4),r(u,UNIT+0x91,1),r(u,0x5D6E48)]
  record('trap_initialize_empty_retry_'+str(typ),dict(type=typ,target_candidates='Only genuinely created self; original candidate callback rejects self',initial_order_state=0),observed,expected,joined([created,init,retry]),snapshot(u))
  for handler,order in ((0x4285A0,170),(0x4288A0,171)):
   u=flat_fixture(new());created=call(u,0x469460,typ,768,(768,3));issued=call(u,0x42A2C0,UNIT,args=(order,));w(u,0x543300,UNIT);w(u,0x543304,UNIT);started=call(u,handler,UNIT)
   animation=[];ticks=0
   while not r(u,UNIT+0x4F,1)&1:
    animation.append(call(u,0x40CF50,UNIT));animation.append(call(u,0x40BEE0,UNIT));animation.append(call(u,0x418B20,UNIT));ticks+=1;assert ticks<256,(typ,r(u,UNIT+0x4D,1),r(u,UNIT+0x4F,1),r(u,UNIT+0x21,1),snapshot(u))
   finished=call(u,handler,UNIT)
   observed=[r(u,UNIT+0x4D,1),r(u,UNIT+0x4F,1)&1,r(u,0x5D6E48)]
   record('trap_stock_animation_completion_'+str(typ)+'_'+str(order),dict(type=typ,order=order,stock_animation_ticks=ticks,target_candidates='Self only',iscript_context={"0x543300":UNIT,"0x543304":UNIT}),observed,[167,0,0],joined([created,issued,started]+animation+[finished]),snapshot(u))
 # A native-created rescuer is selected by the real sorted unit-border query.
 for rescue_controller in (3,):
  for human_controller in (1,2,3,7):
   for timer in (0,1,255):
    controllers=[2]*12;controllers[3]=rescue_controller;controllers[0]=human_controller;u=flat_fixture(new(),controllers);w(u,0x508D50,9)
    one=call(u,0x469460,0,768,(768,3));two=call(u,0x469460,0,800,(768,0));w(u,UNIT+0x54,timer,1)
    ex=call(u,0x48D120,UNIT);rescued=timer==0 and human_controller==2
    observed=[r(u,UNIT+0x4C,1),r(u,UNIT+0x54,1),r(u,UNIT+0x4D,1),chain(u,0x665B08,0x6C),chain(u,0x665B08+3*4,0x6C),r(u,0x5D6E48),r(u,r(u,UNIT+0xC)+0xA,1),r(u,0x529604),r(u,0x529604+3*4),r(u,0x52C0C4),r(u,0x52C0C4+3*4)]
    expected=[0 if rescued else 3,1 if timer==0 else timer,tables['units'][0].human_ai_idle if rescued else 161,[UNIT,UNIT+0x150] if rescued else [UNIT+0x150],[] if rescued else [UNIT],0,3,2 if rescued else 1,0 if rescued else 1,2 if rescued else 1,0 if rescued else 1]
    record('rescue_human_selector_'+str(human_controller)+'_'+str(timer),dict(rescue_owner=3,rescue_controller=3,candidate_owner=0,candidate_controller=human_controller,timer=timer,xy=[[768,768],[800,768]],local_viewer=9),observed,expected,joined([one,two,ex]))
 # A genuine completed resource depot transfers its whole owner list.
 u=flat_fixture(new(),[2,2,2,3]+[2]*8);w(u,0x508D50,9)
 depot=call(u,0x469460,106,768,(768,3));other=call(u,0x469460,0,1200,(1200,3));human=call(u,0x469460,0,828,(768,0));human_xy=list(struct.unpack('<2H',u.mem_read(r(u,UNIT+2*0x150+0xC)+0x14,4)))
 ex=call(u,0x48D120,UNIT)
 observed=[r(u,UNIT+0x4C,1),r(u,UNIT+0x150+0x4C,1),chain(u,0x665B08,0x6C),chain(u,0x665B08+3*4,0x6C),r(u,0x5D6E48)]
 expected=[0,0,[UNIT,UNIT+0x150,UNIT+2*0x150],[],0]
 record('rescue_completed_depot_whole_owner_list',dict(depot_type=106,other_type=0,human_type=0,old_owner=3,new_owner=0,human_actual_xy=human_xy,local_viewer=9),observed,expected,joined([depot,other,human,ex]),snapshot(u))
 result=dict(target_sha256=local.HASH,target_version='1.17.0.1',probe_script_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),runtime=dict(unicorn=unicorn.__version__,pefile=pefile.__version__,architecture=platform.machine(),python_version=platform.python_version()),retail_manifest_sha256=hashlib.sha256(a.manifest.read_bytes()).hexdigest(),asset_manifest_sha256=hashlib.sha256(a.asset_manifest.read_bytes()).hexdigest(),notification_manifest_sha256=hashlib.sha256(a.notification_manifest.read_bytes()).hexdigest(),network_tbl_sha256=hashlib.sha256(network).hexdigest(),dat_sha256=dat_hashes,source_dependencies_sha256={Path(module.__file__).name:hashlib.sha256(Path(module.__file__).read_bytes()).hexdigest() for module in list(sys.modules.values()) if getattr(module,'__file__',None) and Path(module.__file__).resolve().parent==Path(__file__).resolve().parent and module.__name__!='__main__'},reviewed_code_ranges=[dict(start=hex(lo),end_exclusive=hex(hi)) for lo,hi in sorted(set(ALLOW))],original_code_executed=True,host_imports_executed=False,helper_stubs=[],complete_game_executed=False,complete_cases=sum(c['execution']['complete_return'] for c in cases),prefix_cases=sum(not c['execution']['complete_return'] and not c['execution'].get('fragment_executed') for c in cases),fragment_cases=sum(c['execution'].get('fragment_executed',False) for c in cases),all_passed=all(c['passed'] for c in cases),executed_pc_count=len(all_visited),original_memory_writes=write_count,maps=maps,unit_records_audited=len(corpus),stock_records_with_relation=sum(bool(entry['relation']) for entry in corpus),stock_relation_type_histogram={str(typ):sum(entry['type']==typ and bool(entry['relation']) for entry in corpus) for typ in sorted({entry['type'] for entry in corpus if entry['relation']})},cases=cases)
 a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({k:result[k] for k in ('complete_cases','prefix_cases','fragment_cases','all_passed','unit_records_audited','executed_pc_count')}))
if __name__=='__main__':main()
