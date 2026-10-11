#!/usr/bin/env python3
"""Probe successful unit creation using stock DAT, GRP and IScript in private memory.

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
import inspect_native_populated_lifecycle_117 as local
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
ALLOW=list(hidden_source.ALLOW)+list(initialized_source.ALLOW)+list(local.BLOCKS.values())
PLAN=dict(target_sha256=local.TARGET_HASH,helper_stubs=[],instruction_bound=100000,scope='Connected stock populated child allocation, Train completion/Show/rally and deferred CUnit recycling',limits='Synthetic pools and owner state; original resource loading/rendering and full-game execution excluded')
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--exe',type=Path,required=True);p.add_argument('--data',type=Path,required=True);p.add_argument('--manifest',type=Path,required=True);p.add_argument('--assets',type=Path,required=True);p.add_argument('--asset-manifest',type=Path,required=True);p.add_argument('--pyms',type=Path,required=True);p.add_argument('--out',type=Path);p.add_argument('--case-group',choices=('all','train','recycle','vision','vm'),default='all');p.add_argument('--execute-original-code',action='store_true');a=p.parse_args()
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
 tables={};table_raw={}
 for key,cls in (('units',UnitsDAT),('flingy',FlingyDAT),('sprites',SpritesDAT),('images',ImagesDAT)):
  v=stock('arr\\'+key+'.dat');table_raw[key]=v;table=cls();table.load_data(v);tables[key]=table.entries
 script=stock('scripts\\iscript.bin');orderrows=orders.stock_orders(stock('arr\\orders.dat'))
 for row in assetmanifest['images']:
  v=(a.assets/row['path'].replace('\\','/')).read_bytes();assert hashlib.sha256(v).hexdigest()==row['sha256'];assets[row['image_id']]=(row,v)
 for row in assetmanifest['lo_files']:
  v=(a.assets/row['path'].replace('\\','/')).read_bytes();assert hashlib.sha256(v).hexdigest()==row['sha256'];lo_assets.append((row,v))
 import unicorn
 from unicorn import Uc,UC_ARCH_X86,UC_MODE_32,UC_HOOK_CODE
 from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_EDX,UC_X86_REG_ESP,UC_X86_REG_ESI
 pe=pefile.PE(data=raw);assert pe.OPTIONAL_HEADER.ImageBase==0x400000 and pe.FILE_HEADER.Machine==0x14C
 image=pe.get_memory_mapped_image();size=(pe.OPTIONAL_HEADER.SizeOfImage+4095)&~4095;cases=[];cache={}
 def w(u,address,value,width=4):u.mem_write(address,(value&((1<<(8*width))-1)).to_bytes(width,'little'))
 def r(u,address,width=4):return int.from_bytes(u.mem_read(address,width),'little')
 def new(unit_count=4,sprite_count=8,image_count=32,seed=12345,rng_enabled=True):
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
  for key,base in (('units',0x5014F0),('flingy',0x501148),('sprites',0x501408),('images',0x5011A8)):
   v=table_raw[key];offset=0
   for prop_index in range({'units':54,'flingy':7,'sprites':6,'images':14}[key]):
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
 def call(u,start,ecx,edx=0,args=()):
  visited=set();calls=[];script_ops=[];instructions=0;boundary=False;recent=[];entries={lo for lo,hi in ALLOW}
  def hook(m,addr,width,_):
   nonlocal instructions,boundary
   if addr==STOP:boundary=True;m.emu_stop();return
   key=(addr,width)
   if key not in cache:cache[key]=tuple(lo for lo,hi in ALLOW if lo<=addr and addr+width<=hi)
   if not cache[key]:raise RuntimeError(f'Outside reviewed original code {addr:#x}; recent={recent[-15:]}')
   visited.update(cache[key]);instructions+=1;recent.append(hex(addr))
   if addr==0x4104C4:
    pc=m.reg_read(UC_X86_REG_ESI);script_ops.append(dict(offset=pc-SCRIPT,opcode=r(m,pc,1)))
   if addr in entries:
    sp=m.reg_read(UC_X86_REG_ESP);calls.append(dict(callee=hex(addr),ecx=m.reg_read(UC_X86_REG_ECX),edx=m.reg_read(UC_X86_REG_EDX),stack=[r(m,sp+4+i*4) for i in range(6)]))
  h=u.hook_add(UC_HOOK_CODE,hook);sp=STACK+0x3000;u.mem_write(sp,struct.pack('<'+'I'*(len(args)+1),STOP,*args));u.reg_write(UC_X86_REG_ESP,sp);u.reg_write(UC_X86_REG_ECX,ecx);u.reg_write(UC_X86_REG_EDX,edx)
  try:u.emu_start(start,STOP+1,count=100000)
  except Exception as e:raise RuntimeError(f'{e}; recent={recent[-30:]}; calls={calls[-10:]}') from e
  finally:u.hook_del(h)
  assert boundary and u.reg_read(UC_X86_REG_ESP)==sp+4*(len(args)+1)
  return dict(complete_return=True,return_eax=u.reg_read(UC_X86_REG_EAX),instruction_count=instructions,calls=calls,iscript_opcodes=script_ops,visited_functions=[hex(x) for x in sorted(visited)])
 def snapshot(u):
  parent=r(u,0x665B08+3*4);result=dict(owner_head=parent,unit_count=r(u,0x665B38),free_unit_head=r(u,0x665AF0),free_sprite_head=r(u,0x5C51C8),free_image_head=r(u,0x544694),hidden_head=r(u,0x665AF8),rng_state=r(u,0x67393C),pending_delete_head=r(u,0x665AE8),error=r(u,0x5DA398,2),rng_counters={str(k):r(u,0x673940+k*4) for k in (3,6,7,14,15,37)},units=[])
  for j in range(4):
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
 def prepare(typ,producer_id,rally='none',computer=False,vision=False):
  u=new(unit_count=4,sprite_count=8,image_count=32)
  producer=UNIT+8*0x150;producer_sprite=0x808000;target=UNIT+9*0x150;target_sprite=0x808100
  w(u,producer+0xC,producer_sprite);w(u,producer+0x64,producer_id,2);w(u,producer+0xDC,3);w(u,producer+0x4C,3,1);w(u,producer+0xA6,38,1);w(u,producer+0xE2,1,1)
  w(u,producer_sprite+0x14,200,2);w(u,producer_sprite+0x16,200,2);w(u,producer_sprite+0x1C,0)
  w(u,target+0xC,target_sprite);w(u,target+0x64,7,2);w(u,target+0x4C,4 if rally=='other_owner' else 3,1);w(u,target+0xDC,1)
  w(u,target_sprite+0x14,300,2);w(u,target_sprite+0x16,300,2);w(u,target_sprite+0x18,0x808200);w(u,target_sprite+0x20,0x808200)
  u.mem_write(producer+0x98,struct.pack('<5H',typ,228,228,228,228));w(u,0x5263D0+12,1000);w(u,0x526400+12,1000)
  for total,maximum in ((0x529484,0x529424),(0x529514,0x5294B4),(0x5295A4,0x529544)):w(u,total+12,100);w(u,maximum+12,400)
  w(u,0x69B584,0xB00000);w(u,0xB00000,4,2);w(u,0xB44A0C,200*256);w(u,0xB44A10,200*256);w(u,0xB97A1C,0x80A000)
  for addr in (0x666454,0x666458,0x66696C,0x6CE650,0x66644C):w(u,addr,0xC00000+(0,0x10000,0x20000,0x40000,0x50000)[(0x666454,0x666458,0x66696C,0x6CE650,0x66644C).index(addr)])
  w(u,0x54323C,0xC10000);u.mem_write(0xC00000,struct.pack('<I',0x1FFFF)*16384)
  w(u,0x67CF60,0xC60000)
  w(u,0x6D319C,2);w(u,0x5C99B0,0);w(u,0x5D6E40,256);w(u,0x5D6E44,160)
  w(u,0x665AE0,producer);w(u,0x665AE4,producer);w(u,0x665B08+12,producer)
  for off in (0x10,0x12,0x1C,0x1E,0x28,0x2A):w(u,producer+off,200,2)
  w(u,producer+0x2C,200*256);w(u,producer+0x30,200*256)
  for off in (0x13C,0x140,0x144,0x148):w(u,producer+off,0xFFFFFFFF)
  w(u,0x5264CC+12,8);w(u,0x672668,1);w(u,0x67266C,1)
  w(u,0x5AEF00,0x80B000);w(u,0x5AEF04,0x80B000+31*20);w(u,0x5AEF08,0)
  for i in range(32):w(u,0x80B000+i*20,0x80B000+(i-1)*20 if i else 0);w(u,0x80B000+i*20+4,0x80B000+(i+1)*20 if i<31 else 0)
  if computer:
   ai=0x801000;guard=0x801100;town=0x803000;area=0x810000;region=0x802000
   w(u,0x6726D0+3*36,1,1);w(u,producer+0x134,ai);w(u,ai+8,3,1);w(u,ai+9,2,1);w(u,ai+0x18,guard);w(u,guard+0x16,321,2);w(u,guard+0x18,654,2)
   w(u,ai+0x14,town);w(u,town+8,area);w(u,town+0x10,area);w(u,town+0x18,3,1);w(u,area+0x5DC0,0x809000)
   w(u,region+0x2C,area);w(u,0x6B1A74+12,region);w(u,0x6BE610+24,area);w(u,0x67D280+3*0x4E8,0x20,2)
  if rally!='none':
   w(u,producer+0xF8,0 if rally=='zero_x' else 321,2);w(u,producer+0xFA,654,2)
   w(u,producer+0xFC,target if rally in ('follow','other_owner') else producer if rally=='self' else 0)
  call(u,0x4195A0,producer)
  return u,producer,target
 def initialize_vision(u,sight):
  descriptor=0x508A50+sight*28;grid=0xB60000;records=0xB64000
  inner=r(u,descriptor+8)*r(u,descriptor+12);w(u,descriptor+0x10,inner)
  grid_ex=call(u,0x466990,descriptor,grid);total=grid_ex['return_eax']
  w(u,descriptor+0x14,total-inner);w(u,descriptor+0x18,records);u.mem_write(records,bytes(total*20))
  records_ex=call(u,0x466BD0,descriptor,grid,(128,));output=[]
  for i in range(total):
   row=records+i*20;dx,dy=struct.unpack('<bb',u.mem_read(row+16,2));n=r(u,row+18,1)
   dependencies=[(r(u,row+k*4)-records)//20 for k in range(n)]
   assert n<=2 and all(0<=x<i for x in dependencies) and r(u,row+8)==((dx+dy*128)&0xFFFFFFFF)
   output.append(dict(dx=dx,dy=dy,dependency_indices=dependencies))
  return dict(sight=sight,inner_count=inner,outer_count=total-inner,records=output,execution=joined([grid_ex,records_ex]))
 # This selected composition additionally advances the real hidden image path
 # between Train calls. It is not the complete active/movement/hidden-order step.
 for typ,producer_id in (((0,111),(8,114)) if a.case_group in ('all','vision') else ()):
  for mask in (0,8,9):
   u,producer,target=prepare(typ,producer_id);shape=initialize_vision(u,tables['units'][typ].sight_range)
   w(u,0x5264CC+12,mask);executions=[shape['execution'],call(u,0x439310,producer)];timer=r(u,UNIT+0xAC,2);iterations=0;image_updates=0
   progress_instructions=0;progress_calls={};progress_visits=set();progress_ops={};last=None;progress_digest=hashlib.sha256()
   while True:
    if r(u,0x665AF8)==UNIT:
     w(u,0x543300,UNIT);w(u,0x543304,UNIT);image_ex=call(u,0x418B20,UNIT);image_updates+=1
     progress_instructions+=image_ex['instruction_count'];progress_visits.update(image_ex['visited_functions'])
     for op in image_ex['iscript_opcodes']:progress_ops[str(op['opcode'])]=progress_ops.get(str(op['opcode']),0)+1
     for entry in image_ex['calls']:progress_calls[entry['callee']]=progress_calls.get(entry['callee'],0)+1
    last=call(u,0x439310,producer);iterations+=1;assert iterations<2000
    progress_instructions+=last['instruction_count'];progress_visits.update(last['visited_functions'])
    for entry in last['calls']:progress_calls[entry['callee']]=progress_calls.get(entry['callee'],0)+1
    progress_digest.update(struct.pack('<5I',iterations,r(u,UNIT+0xAC,2),r(u,UNIT+8),r(u,0x67393C),r(u,IMAGE+0x14)))
    if r(u,producer+0xE2,1)!=2:break
   w(u,0x543300,0);w(u,0x543304,0)
   ex=joined([*executions,last]);ex.update(complete_call_count=3+iterations+image_updates,instruction_count=sum(x['instruction_count'] for x in executions)+progress_instructions,visited_functions=sorted(set(ex['visited_functions'])|progress_visits),progress_call_histogram=progress_calls,progress_trace_sha256=progress_digest.hexdigest(),progress_iscript_opcodes=progress_ops)
   x,y=r(u,SPRITE+0x14,2),r(u,SPRITE+0x16,2);cx,cy=x//32,y//32;flying=bool(r(u,UNIT+0xDC)&4)
   nodes=shape['records'][:shape['outer_count']] if flying else shape['records'];expected_changes=[]
   if mask:
    for node in nodes:
     tx,ty=cx+node['dx'],cy+node['dy']
     if 0<=tx<128 and 0<=ty<128:expected_changes.append([ty*128+tx,0x1FFFF&~(mask|mask<<8)])
   changed=[[i,r(u,0xC00000+i*4)] for i in range(16384) if r(u,0xC00000+i*4)!=0x1FFFF]
   actual=[r(u,producer+0xEC),r(u,producer+0xA4,1),r(u,UNIT+0xAC,2),r(u,SPRITE+0xE,1),r(u,SPRITE+0xC,1),changed,iterations,image_updates]
   record(f'populated_train_native_vision_{typ}_mask_{mask}',dict(type=typ,producer_type=producer_id,shared_vision_mask=mask,native_generated_sight=shape['sight'],native_records_used=len(nodes),image_update_contract='Actual 418b20 before every remaining hidden-child Train progress call; no active primary/movement or hidden-order dispatcher execution',initial_timer=timer),actual,[0,1,0,0,mask,sorted(expected_changes),timer+1,timer+1],ex,snapshot(u))
   native_rng_calls=sum(r(u,0x673940+k*4) for k in range(50));assert r(u,0x67393C)==advance(12345,native_rng_calls)
   cases[-1]['rng_sequence_check']=dict(initial_state=12345,total_original_rng_calls=native_rng_calls,actual_state=r(u,0x67393C),expected_state=advance(12345,native_rng_calls))
   cases[-1]['placed_position']=[x,y];cases[-1]['vision_shape']=shape
 for typ,producer_id in (((0,111),(7,106),(8,114),(65,160),(2,113),(3,113),(5,113)) if a.case_group in ('all','train') else ()):
  for computer in (False,True):
   for rally in ('none','move','follow','other_owner','self','zero_x'):
    u,producer,target=prepare(typ,producer_id,rally,computer);executions=[call(u,0x439310,producer)]
    assert r(u,producer+0xEC)==UNIT and r(u,producer+0xE2,1)==2
    initial_timer=r(u,UNIT+0xAC,2);iterations=0;progress_instructions=0;progress_visits=set();progress_calls={};progress_digest=hashlib.sha256();last=None
    while r(u,producer+0xE2,1)==2:
     last=call(u,0x439310,producer);iterations+=1;assert iterations<2000
     progress_instructions+=last['instruction_count'];progress_visits.update(last['visited_functions'])
     for entry in last['calls']:progress_calls[entry['callee']]=progress_calls.get(entry['callee'],0)+1
     progress_digest.update(struct.pack('<6I',iterations,r(u,UNIT+0xAC,2),r(u,UNIT+8),r(u,UNIT+0x60),r(u,producer+0xE2,1),r(u,producer+0xEC)))
    ex=joined([executions[0],last]);ex.update(complete_call_count=iterations+1,instruction_count=executions[0]['instruction_count']+progress_instructions,visited_functions=sorted(set(executions[0]['visited_functions'])|progress_visits),progress_call_histogram=progress_calls,progress_trace_sha256=progress_digest.hexdigest(),retained_full_call_traces='Allocation and final completion calls; all progress returns and stack cleanup asserted, per-progress state digest and callee counts retained')
    state=snapshot(u);idle=tables['units'][typ].comp_ai_idle if computer else tables['units'][typ].human_ai_idle
    expected_order=49 if rally=='follow' else 6 if rally in ('move','other_owner') else idle
    actual=[r(u,producer+0xE2,1),r(u,producer+0xEC),r(u,producer+0xA4,1),r(u,producer+0x98,2),r(u,UNIT+0xAC,2),r(u,UNIT+8),r(u,SPRITE+0xE,1),r(u,UNIT+0x4D,1),r(u,UNIT+0x5C),r(u,0x5C99B0),r(u,0x665AF8),r(u,0x5263D0+12),r(u,0x526400+12),iterations,r(u,UNIT+0x134)]
    expected=[0,0,1,228,0,tables['units'][typ].hit_points.whole*256+tables['units'][typ].hit_points.fraction,0,expected_order,target if rally=='follow' else 0,4,0,1000,1000,initial_timer+1,(0x809000 if typ==7 else 0x801100) if computer else 0]
    record(f'populated_train_{typ}_computer_{computer}_rally_{rally}',dict(type=typ,producer_type=producer_id,computer=computer,rally=rally,initial_timer=initial_timer,producer='Synthetic initialized existing producer with empty image list',child='Created by actual 439310→41bc30→424900 with stock images and IScript',terrain='Single region, flat walkable terrain, empty terrain edge arrays',vision_records=0),actual,expected,ex,state)
    print(json.dumps(dict(case=cases[-1]['case'],progress_calls=iterations)),flush=True)
 # Actual successful parent allocation, child failure and complete object-order step.
 for typ in ((3,5) if a.case_group in ('all','recycle') else ()):
  for reason in ('unit_pool','sprite_pool','image_pool_1','image_pool_2','unit_cap'):
   profile=dict(unit_count=1 if reason=='unit_pool' else 4,sprite_count=1 if reason=='sprite_pool' else 8,image_count=int(reason[-1]) if reason.startswith('image_pool') else 32)
   u=new(**profile)
   if reason=='unit_cap':w(u,0x665B38,1699)
   allocation=call(u,0x424900,typ,200,(200,3));assert allocation['return_eax']==0 and r(u,0x665AE8)==UNIT
   before=dict(count=r(u,0x665B38),unused=chain(u,0x665AF0),pending=chain(u,0x665AE8))
   w(u,0x5DA390,1);w(u,0x5DA394,1);w(u,0x6663EC,0,1);w(u,0x6751F4,0);w(u,0x6BE650,0,1);w(u,0x665D40,0);w(u,0x665B60,0)
   step=call(u,0x438EF0,0);actual=[r(u,0x665B38),chain(u,0x665AF0),chain(u,0x665AE8),r(u,UNIT+0xC),r(u,0x543300),r(u,0x543304),r(u,0x5DA390),r(u,0x5DA394)]
   old=before['unused'];restored=[UNIT] if not old else [old[0],UNIT,*old[1:]]
   record(f'rollback_then_complete_step_{typ}_{reason}',dict(type=typ,failure=reason,pools=profile,before_step=before,active_hidden_special_cloak_lists='Empty after original rollback',ui_placement_gates=False),actual,[before['count']-1,restored,[],0,0,0,150,300],joined([allocation,step]))
 for typ in ((0,2,8,65) if a.case_group in ('all','recycle') else ()):
  for queued in (0,2):
   u=new();executions=[call(u,0x424900,typ,200,(200,3))]
   w(u,0x5AEF00,0x80B000);w(u,0x5AEF04,0x80B000+7*20);w(u,0x5AEF08,0)
   for i in range(8):w(u,0x80B000+i*20,0x80B000+(i-1)*20 if i else 0);w(u,0x80B000+i*20+4,0x80B000+(i+1)*20 if i<7 else 0)
   for i in range(queued):executions.append(call(u,0x4295D0,UNIT,args=(6,321|(654<<16),0,228,0)))
   queued_head=r(u,UNIT+0x74);assert bool(queued_head)==bool(queued)
   executions.append(call(u,0x425050,UNIT));assert r(u,0x665AE8)==UNIT and r(u,UNIT+0xC)==SPRITE and r(u,SPRITE+0xE,1)&32
   before=dict(count=r(u,0x665B38),pending=chain(u,0x665AE8),sprite=r(u,UNIT+0xC),queued_head=r(u,UNIT+0x74),free_images=len(chain(u,0x544694)),free_sprites=len(chain(u,0x5C51C8)))
   w(u,0x5DA390,2);w(u,0x5DA394,2);w(u,0x6663EC,0,1);w(u,0x6751F4,0);w(u,0x6BE650,0,1);w(u,0x665D40,0);w(u,0x665B60,0)
   executions.append(call(u,0x438EF0,0));actual=[r(u,0x665B38),chain(u,0x665AF0),chain(u,0x665AE8),r(u,UNIT+0xC),r(u,UNIT+0x74),r(u,UNIT+0x78),r(u,0x5AEF08),len(chain(u,0x544694)),len(chain(u,0x5C51C8)),r(u,0x543300),r(u,0x543304)]
   record(f'hidden_remove_then_complete_step_{typ}_queue_{queued}',dict(type=typ,queued_orders=queued,before_step=before,removal_entry='0x425050 complete call; sprite remains hidden and allocated until original object step'),actual,[0,[UNIT+0x150,UNIT,UNIT+0x2A0,UNIT+0x3F0],[],0,0,0,0,32,8,0,0],joined(executions))
 for timer in ((1,2,3,0xFFFFFFFF) if a.case_group in ('all','recycle') else ()):
  u=new();executions=[call(u,0x424900,0,200,(200,3)),call(u,0x425050,UNIT)]
  w(u,UNIT+0xDC,r(u,UNIT+0xDC)|0x4000);w(u,0x6C46D8,timer);w(u,0x5DA390,2);w(u,0x5DA394,2);w(u,0x6751F4,0);w(u,0x6BE650,0,1);w(u,0x665D40,0);w(u,0x665B60,0)
  executions.append(call(u,0x438EF0,0))
  record(f'pending_status_4000_timer_{timer}',dict(type=0,synthetic_status_addition='0x4000 after original hidden removal',initial_shared_timer=timer,creep_producer='Timer-positive path only; 4b47f0 not entered'),[r(u,0x6C46D8),r(u,0x665B38),chain(u,0x665AE8),r(u,UNIT+0xC),len(chain(u,0x544694)),len(chain(u,0x5C51C8))],[(timer-1)&0xFFFFFFFF,1,[UNIT],0,32,8],joined(executions))
 for pool in ((2,4) if a.case_group in ('all','recycle') else ()):
  u=new(unit_count=pool);executions=[]
  for i in range(2):executions.append(call(u,0x424900,0,200+i*32,(200,3)))
  for i in range(2):executions.append(call(u,0x425050,UNIT+i*0x150))
  assert chain(u,0x665AE8)==[UNIT,UNIT+0x150]
  w(u,0x5DA390,2);w(u,0x5DA394,2);w(u,0x6751F4,0);w(u,0x6BE650,0,1);w(u,0x665D40,0);w(u,0x665B60,0)
  executions.append(call(u,0x438EF0,0))
  expected_free=[UNIT,UNIT+0x150] if pool==2 else [UNIT+0x2A0,UNIT+0x150,UNIT,UNIT+0x3F0]
  record(f'two_pending_complete_step_pool_{pool}',dict(actual_created_and_removed_units=2,initial_unit_pool=pool,pending_order=[UNIT,UNIT+0x150]),[r(u,0x665B38),chain(u,0x665AE8),chain(u,0x665AF0),len(chain(u,0x5C51C8)),len(chain(u,0x544694))],[0,[],expected_free,8,32],joined(executions))
 for typ in ((0,8) if a.case_group in ('all','recycle') else ()):
  u=new(unit_count=1);executions=[call(u,0x424900,typ,200,(200,3)),call(u,0x425050,UNIT)]
  assert r(u,UNIT+0xA5,1)==1
  w(u,0x5DA390,2);w(u,0x5DA394,2);w(u,0x6751F4,0);w(u,0x6BE650,0,1);w(u,0x665D40,0);w(u,0x665B60,0)
  executions.append(call(u,0x438EF0,0));assert r(u,0x665AF0)==UNIT and r(u,0x665B38)==0 and r(u,UNIT+0xA5,1)==1
  executions.append(call(u,0x424900,typ,300,(300,3)))
  record(f'actual_recycle_and_reallocate_{typ}',dict(type=typ,unit_pool=1,first_position=[200,200],second_position=[300,300],initial_seed=12345),[executions[-1]['return_eax'],r(u,0x665B38),r(u,0x665AF0),r(u,UNIT+0xA5,1),r(u,0x665AE8),r(u,0x67393C),[r(u,0x673940+k*4) for k in (3,14,15)],len(chain(u,0x544694)),len(chain(u,0x5C51C8))],[UNIT,1,0,2,0,advance(12345,6),[2,2,2],30,7],joined(executions),snapshot(u))
 # Synthetic bytecode operand probes are separate from stock Init traces.
 # Actual PE instructions, allocator, sprite/image objects and callees remain real.
 def vm_fixture(code,output_low,enabled=True):
  u=new();created=call(u,0x424900,0,200,(200,3));u.mem_map(0xD10000,0x1000);u.mem_write(0xD10000,code);w(u,0x54330C,0xD10000)
  w(u,IMAGE+0x12,1,2);w(u,IMAGE+0x17,0,1);w(u,0x543300,UNIT);w(u,0x543304,UNIT)
  inverse=pow(22695477,-1,1<<32);desired=output_low<<16;seed=((desired-1)*inverse)&0xFFFFFFFF
  w(u,0x67393C,seed);w(u,0x673930,int(enabled));u.mem_write(0x673940,bytes(50*4))
  return u,created,seed,desired
 for threshold in ((0,1,127,128,254,255) if a.case_group in ('all','vm') else ()):
  for value in (0,1,127,128,254,255):
   code=bytes([0,30,threshold,7,0,5,2,5,4]);u,created,seed,desired=vm_fixture(code,value);executed=call(u,0x410480,IMAGE,args=(IMAGE+0x10,0,0));jump=value<=threshold
   record(f'vm_randcondjmp_threshold_{threshold}_value_{value}',dict(stock_created_type=0,synthetic_bytecode_hex=code.hex(),threshold=threshold,forced_first_rng_result=value,seed_reset_after_creation=seed),[r(u,IMAGE+0x12,2),r(u,IMAGE+0x17,1),r(u,0x67393C),r(u,0x673940+7*4)],[9 if jump else 7,3 if jump else 1,desired,1],joined([created,executed]))
 for threshold in ((0,128,255) if a.case_group in ('all','vm') else ()):
  code=bytes([0,30,threshold,7,0,5,2,5,4]);u,created,seed,desired=vm_fixture(code,255,False);executed=call(u,0x410480,IMAGE,args=(IMAGE+0x10,0,0))
  record(f'vm_randcondjmp_disabled_threshold_{threshold}',dict(stock_created_type=0,synthetic_bytecode_hex=code.hex(),rng_enabled=False,seed_reset_after_creation=seed),[r(u,IMAGE+0x12,2),r(u,IMAGE+0x17,1),r(u,0x67393C),r(u,0x673940+7*4)],[9,3,seed,0],joined([created,executed]))
 for argument in ((0,1,8,31,255) if a.case_group in ('all','vm') else ()):
  for value in (0,1,2,3):
   code=bytes([0,34,argument,5,2]);u,created,seed,desired=vm_fixture(code,value);executed=call(u,0x410480,IMAGE,args=(IMAGE+0x10,0,0))
   direction=((-1 if value==1 else 1)*8*argument)&255;quant=(direction+4)>>3;mirror=quant>16;image_direction=32-quant if mirror else quant
   record(f'vm_turnrand_arg_{argument}_value_{value}',dict(stock_created_type=0,synthetic_bytecode_hex=code.hex(),turn_argument=argument,forced_first_rng_result=value,seed_reset_after_creation=seed),[r(u,UNIT+0x21,1),r(u,UNIT+0x23,1),r(u,UNIT+0x4A,1),r(u,IMAGE+0xB,1),r(u,IMAGE+0xC,2)&2,r(u,0x67393C),r(u,0x673940+6*4)],[direction,direction,direction,image_direction,2 if mirror else 0,desired,1],joined([created,executed]))
 modules={};pending=[local,image_source,initialized_source,hidden_source,lifecycle,creation,orders,admission,ownership,actions]
 while pending:
  module=pending.pop();path=Path(module.__file__)
  if path.name in modules:continue
  modules[path.name]=hashlib.sha256(path.read_bytes()).hexdigest()
  pending.extend(x for x in vars(module).values() if isinstance(x,types.ModuleType) and getattr(x,'__file__',None) and Path(x.__file__).resolve().parent==Path(__file__).resolve().parent)
 result=dict(target_sha256=local.TARGET_HASH,probe_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),source_dependencies_sha256=modules,engine=dict(unicorn=unicorn.__version__,pefile=pefile.__version__,machine=platform.machine()),dat_sha256=dat_hashes,asset_manifest_sha256=hashlib.sha256(a.asset_manifest.read_bytes()).hexdigest(),case_group=a.case_group,helper_stubs=[],complete_cases=len(cases),prefix_cases=0,all_passed=all(x['passed'] for x in cases),cases=cases)
 a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(dict(complete_cases=len(cases),prefix_cases=0,all_passed=True)))

if __name__=='__main__':main()
