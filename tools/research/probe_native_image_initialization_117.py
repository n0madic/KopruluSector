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
import pefile
import inspect_native_image_initialization_117 as local
import inspect_native_training_lifecycle_117 as lifecycle
import inspect_native_unit_creation_117 as creation
import inspect_native_primary_orders_117 as orders
import inspect_native_training_117 as admission
import probe_native_trigger_ownership_117 as ownership
import probe_native_trigger_unit_actions_117 as actions
UNIT=0x5DA3A0;SPRITE=0x804000;IMAGE=0x805000;STACK=0x900000;STOP=0xA00000;SCRIPT=0xD00000;ASSETS=0xE00000
SELECT=('live_unit_add','live_unit_account','unit_group_upgrade_effects','speed_from_flingy','acceleration_from_flingy','turn_from_flingy','speed_status_modifier','enable_speed_upgrade','iscript_speed_control_zero_guard','upgrade_level_get')
DEATH=('drop_powerup_zero_carry','clear_unit_ai','remove_military_ai_null','remove_town_ai_null','clear_town_resource_refs','clear_guard_ai_null','clear_queen_selection')
ALLOW=[actions.BLOCKS[x] for x in DEATH]+list(ownership.BLOCKS.values())+list(local.BLOCKS.values())+list(lifecycle.BLOCKS.values())+list(creation.BLOCKS.values())+list(orders.BLOCKS.values())+list(admission.WITNESSES.values())+[ownership.BLOCKS[x] for x in SELECT]
PLAN=dict(target_sha256=local.TARGET_HASH,helper_stubs=[],instruction_bound=100000,scope='Successful unit allocation, stock image and IScript initialization',limits='Synthetic pools and owner state; original resource loading/rendering and full-game execution excluded')
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
  for addr,length in ((0x800000,0x20000),(STACK,0x4000),(STOP,0x1000),(SCRIPT,0x10000),(ASSETS,0x600000)):u.mem_map(addr,length)
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
  parent=r(u,0x665B08+3*4);result=dict(owner_head=parent,unit_count=r(u,0x665B38),free_unit_head=r(u,0x665AF0),free_sprite_head=r(u,0x5C51C8),free_image_head=r(u,0x544694),hidden_head=r(u,0x665AF8),rng_state=r(u,0x67393C),pending_delete_head=r(u,0x665AE8),error=r(u,0x5DA398,2),rng_counters={str(k):r(u,0x673940+k*4) for k in (3,14,15)},units=[])
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
 # Original preprocessing of stock damage-overlay LO point records.
 u=new();ex=call(u,0x40FBE0,0);expected=[]
 for row,v in lo_assets:
  if row['kind']!=1:continue
  offset=struct.unpack_from('<I',v,8)[0];count=sum(tuple(struct.unpack_from('<2b',v,offset+j*2))!=(127,127) for j in range(22));expected.append([row['image_id'],count*2])
 actual=[[ident,r(u,0x543310+ident,1)] for ident,count in expected]
 assert expected and any(count for ident,count in expected)
 record('stock_damage_overlay_preprocessing',dict(stock_image_ids=[ident for ident,count in expected],unprepared_other_LO_slots='Explicit null pointer fixture',sampled_positions_per_first_frame=22),actual,expected,ex)
 init={37:(54,55,85,1440,0),41:(17,18,0,1280,0),43:(38,39,0,1707,1),0:(239,240,68,1024,1),7:(247,248,0,1280,0),8:(243,244,0,1707,1),65:(151,152,85,1024,0),2:(256,257,0,1707,0),3:(234,236,119,1200,0),5:(250,252,0,1024,0)}
 seeds=(0,1,12345,0xFFFFFFFF,0x12345678,0x80000000)
 for typ in init:
  for seed in seeds:
   for enabled in (False,True):
    u=new(seed=seed,rng_enabled=enabled);ex=call(u,0x424900,typ,200,(200,3));state=snapshot(u)
    row=tables['units'][typ];n=2 if row.subunit1!=228 else 1;main,shadow,baseframe,speed,rand=init[typ]
    actual=[ex['return_eax'],state['unit_count'],state['free_unit_head'],state['free_sprite_head'],state['free_image_head'],state['owner_head'],state['hidden_head'],state['rng_state'],state['rng_counters'],[[x['type'],x['owner'],x['hp'],x['shield'],x['direction'],x['speed'],x['sprite']['flags'],x['wireframe_randomizer'],x['generation'],x['primary_order'],x['secondary_order'],x['queue'],[i['id'] for i in x['sprite']['images']]] for x in state['units']]]
    units=[[typ,3,(row.hit_points.whole*256+row.hit_points.fraction)//10,row.shield_amount*256,96 if typ==5 else 0,speed,32,((advance(seed,rand+2)>>16)&255) if enabled else 0,1,23,23,[228]*5,[main,shadow]]]
    if n==2:
     child=tables['units'][row.subunit1];units.append([row.subunit1,3,1,child.shield_amount*256,96 if typ==5 else 0,0 if typ==5 else 1,48,((advance(seed,3)>>16)&255) if enabled else 0,1,23,23,[228]*5,[235 if typ==3 else 251]])
    expected=[UNIT,n,UNIT+n*0x150,SPRITE+n*0x24,IMAGE+(3 if n==2 else 2)*0x40,UNIT+(n-1)*0x150,UNIT,advance(seed,1+n+rand) if enabled else seed,{'3':rand if enabled else 0,'14':int(enabled),'15':n if enabled else 0},units]
    if typ==0:
     second=advance(seed,2);low=((second>>16)&0x7FFF)&255 if enabled else 0
     actual.append(state['units'][0]['sprite']['images'][0]['iscript'][4]);expected.append(61+low%13)
    record(f'stock_create_{typ}_seed_{seed}_rng_{enabled}',dict(type=typ,x=200,y=200,owner=3,unit_pool=4,sprite_pool=8,image_pool=32,seed=seed,rng_enabled=enabled),actual,expected,ex,state)
 for old in (0,1,30,31,32,255):
  u=new();w(u,UNIT+0xA5,old,1);ex=call(u,0x424900,0,200,(200,3))
  record(f'creation_generation_{old}',dict(previous_generation_byte=old),r(u,UNIT+0xA5,1),(old+1)&31,ex)
 # A failed optional shadow overlay does not reject main-image success.
 for typ in (0,7,8,65,2):
  for images in (1,2,3):
   u=new(image_count=images);ex=call(u,0x424900,typ,200,(200,3));state=snapshot(u)
   record(f'optional_shadow_pool_{typ}_{images}',dict(type=typ,image_pool=images),[ex['return_eax'],state['unit_count'],len(state['units'][0]['sprite']['images']),state['free_image_head']],[UNIT,1,min(images,2),IMAGE+0x80 if images>2 else 0],ex,state)
 # Real subunit failure tears down the already successful parent.
 for typ in (3,5):
  for reason in ('unit_pool','sprite_pool','image_pool_1','image_pool_2','unit_cap'):
   profile=dict(unit_count=1 if reason=='unit_pool' else 4,sprite_count=1 if reason=='sprite_pool' else 8,image_count=int(reason[-1]) if reason.startswith('image_pool') else 32)
   u=new(**profile)
   if reason=='unit_cap':w(u,0x665B38,1699)
   ex=call(u,0x424900,typ,200,(200,3));state=snapshot(u)
   actual=[ex['return_eax'],state['owner_head'],state['hidden_head'],state['pending_delete_head'],state['unit_count'],state['free_unit_head'],r(u,UNIT+0xC),state['rng_counters'],state['error'],len(chain(u,0x5C51C8)),len(chain(u,0x544694)),chain(u,0x665AE8)]
   expected=[0,0,0,UNIT,1700 if reason=='unit_cap' else 1,0 if reason=='unit_pool' else UNIT+0x150,0,{'3':0,'14':1,'15':1},61 if reason=='unit_cap' else 62,profile['sprite_count'],profile['image_count'],[UNIT]]
   record(f'subunit_failure_{typ}_{reason}',dict(type=typ,reason=reason,initial_unit_count=1699 if reason=='unit_cap' else 0,**profile),actual,expected,ex,state)
 # Fully populated subunits fit exactly in the smallest successful pools.
 for typ in (3,5):
  for units,sprites,images in ((2,2,3),(2,8,3),(4,2,3)):
   u=new(unit_count=units,sprite_count=sprites,image_count=images);ex=call(u,0x424900,typ,200,(200,3));state=snapshot(u)
   actual=[ex['return_eax'],state['unit_count'],len(state['units']),state['free_unit_head'],state['free_sprite_head'],state['free_image_head'],r(u,UNIT+0x70),r(u,UNIT+0x150+0x70)]
   record(f'subunit_exact_pools_{typ}_{units}_{sprites}_{images}',dict(type=typ,unit_pool=units,sprite_pool=sprites,image_pool=images),actual,[UNIT,2,2,UNIT+0x2A0 if units>2 else 0,SPRITE+0x48 if sprites>2 else 0,0,UNIT+0x150,UNIT],ex,state)
 # Successful main-image requirement; overlay failure above is optional.
 for typ in init:
  for sprites in (1,8):
   u=new(sprite_count=sprites,image_count=0);ex=call(u,0x424900,typ,200,(200,3));state=snapshot(u)
   actual=[ex['return_eax'],state['unit_count'],state['owner_head'],state['free_unit_head'],chain(u,0x5C51C8),state['rng_counters'],state['error']]
   initial=[SPRITE+i*0x24 for i in range(sprites)];restored=initial if sprites==1 else [initial[1],initial[0],*initial[2:]]
   record(f'stock_main_image_pool_failure_{typ}_{sprites}',dict(type=typ,sprite_pool=sprites,image_pool=0),actual,[0,0,0,UNIT,restored,{'3':0,'14':1,'15':0},62],ex,state)
 # Owner insertion is at front; hidden-unit insertion is after current head.
 u=new();executions=[]
 for typ in (0,65,3):executions.append(call(u,0x424900,typ,200,(200,3)))
 state=snapshot(u);actual=[state['unit_count'],state['free_unit_head'],chain(u,0x665B08+12,0x6C),chain(u,0x665AF8),state['free_sprite_head'],state['free_image_head'],state['rng_counters'],state['rng_state']]
 record('repeated_create_owner_hidden_lists',dict(types=[0,65,3],seed=12345),actual,[4,0,[UNIT+0x3F0,UNIT+0x2A0,UNIT+0x150,UNIT],[UNIT,UNIT+0x2A0,UNIT+0x150],SPRITE+0x90,IMAGE+0x1C0,{'3':1,'14':3,'15':4},advance(12345,8)],dict(complete_return=True,return_eax=executions[-1]['return_eax'],instruction_count=sum(x['instruction_count'] for x in executions),calls=[c for x in executions for c in x['calls']],iscript_opcodes=[c for x in executions for c in x['iscript_opcodes']],visited_functions=sorted({c for x in executions for c in x['visited_functions']}),connected_calls=executions),state)
 # Already initialized stock image helpers keep mirror and frame semantics.
 for direction in (0,4,7,8,15,64,124,128,132,192,248,252,255):
  u=new();call(u,0x424900,0,200,(200,3));ex=call(u,0x417190,SPRITE,args=(direction,))
  quant=(direction+4)>>3;mirror=quant>16;index=32-quant if mirror else quant
  actual=[r(u,IMAGE+0xB,1),r(u,IMAGE+0xC,2)&2,r(u,IMAGE+0x1A,2),r(u,IMAGE+0x40+0xB,1),r(u,IMAGE+0x40+0x1A,2)]
  record(f'sprite_direction_{direction}',dict(stock_type=0,flingy_direction=direction),actual,[index,2 if mirror else 0,68+index,index,68+index],ex)
 # Connected secondary Train admission with synthetic producer, real child images.
 for typ,producer_id in ((0,111),(7,106),(8,114),(65,160),(2,113),(3,113),(5,113)):
  for state_id in (0,1):
   u=new();producer=UNIT+6*0x150;producer_sprite=0x808000;w(u,producer+0xC,producer_sprite);w(u,producer+0x64,producer_id,2);w(u,producer+0xDC,3);w(u,producer+0x4C,3,1);w(u,producer+0xA6,38,1);w(u,producer+0xE2,state_id,1)
   w(u,producer_sprite+0x14,200,2);w(u,producer_sprite+0x16,200,2);w(u,producer_sprite+0x1C,0)
   u.mem_write(producer+0x98,struct.pack('<5H',typ,228,228,228,228));w(u,0x5263D0+12,1000);w(u,0x526400+12,1000)
   for total,maximum in ((0x529484,0x529424),(0x529514,0x5294B4),(0x5295A4,0x529544)):w(u,total+12,100);w(u,maximum+12,400)
   ex=call(u,0x439310,producer);state=snapshot(u);n=2 if tables['units'][typ].subunit1!=228 else 1
   actual=[r(u,producer+0xE2,1),r(u,producer+0xEC),r(u,producer+0xA6,1),r(u,producer+0xA4,1),r(u,producer+0x98,2),state['unit_count'],r(u,0x5263D0+12),r(u,0x526400+12),[len(x['sprite']['images']) for x in state['units']]]
   record(f'train_allocate_stock_{typ}_state_{state_id}',dict(type=typ,producer_type=producer_id,producer_fixture='Synthetic existing producer, empty producer image list; child uses stock image/IScript path',initial_secondary_state=state_id,already_paid_stocks=[1000,1000]),actual,[2,UNIT,38,0,typ,n,1000,1000,[2,1] if n==2 else [2]],ex,state)
 # Real GRP frame-header geometry under camera clipping and mirroring.
 for typ in (0,8,65):
  for mirror in (False,True):
   for x,y,camera_x,camera_y in ((200,200,0,0),(5,5,0,0),(630,395,0,0),(900,800,1000,900),(0,200,0,0),(200,200,210,210)):
    u=new();call(u,0x424900,typ,200,(200,3));w(u,SPRITE+0x14,x,2);w(u,SPRITE+0x16,y,2);w(u,0x543218,camera_x);w(u,0x54321C,camera_y)
    flags=r(u,IMAGE+0xC,2);w(u,IMAGE+0xC,(flags|2) if mirror else (flags&~2),2);ex=call(u,0x40EE70,IMAGE)
    ident=r(u,IMAGE+8,2);frame=r(u,IMAGE+0x1A,2);raw_grp=assets[ident][1];gw,gh=struct.unpack_from('<2H',raw_grp,2);fx,fy,fw,fh=struct.unpack_from('<4B',raw_grp,6+frame*8);dx,dy=struct.unpack('<2b',u.mem_read(IMAGE+0xE,2))
    wx=x+dx+(gw//2-fw-fx if mirror else fx-gw//2);wy=y+dy+fy-gh//2;screen_x=wx-camera_x;screen_y=wy-camera_y;crop_x=max(-screen_x,0);crop_y=max(-screen_y,0);visible_width=min(fw+min(screen_x,0),640-max(screen_x,0));visible_height=min(fh+min(screen_y,0),400-max(screen_y,0))
    expected=[wx,wy,max(screen_x,0),max(screen_y,0),crop_x,crop_y,visible_width,visible_height]
    actual=list(struct.unpack('<8h',u.mem_read(IMAGE+0x1C,16)))
    record(f'stock_grp_bounds_{typ}_{mirror}_{x}_{y}_{camera_x}_{camera_y}',dict(type=typ,mirror=mirror,sprite_position=[x,y],camera=[camera_x,camera_y],stock_frame=frame),actual,expected,ex)
 dependencies={Path(module.__file__).name:hashlib.sha256(Path(module.__file__).read_bytes()).hexdigest() for module in (local,lifecycle,creation,orders,ownership,admission,actions)}
 result=dict(target_sha256=local.TARGET_HASH,probe_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),source_dependencies_sha256=dependencies,engine=dict(unicorn=unicorn.__version__,host_architecture=platform.machine(),guest='x86-32'),dat_sha256=dat_hashes,asset_manifest_sha256=hashlib.sha256(a.asset_manifest.read_bytes()).hexdigest(),helper_stubs=[],complete_cases=len(cases),prefix_cases=0,all_passed=True,cases=cases)
 a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(dict(cases=len(cases),all_passed=True)))
if __name__=='__main__':main()
