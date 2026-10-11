#!/usr/bin/env python3
"""Probe original AI paid prerequisite construction and Firebat production.

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
from collections import deque
import sys
import pefile
import inspect_native_ai_tech_build_117 as local
import probe_native_ai_wave_117 as wave_source
import probe_native_ai_nuke_117 as ai_nuke_source
import probe_native_ai_construction_117 as town_source
import probe_native_automatic_requests_117 as automatic_source
import probe_native_combat_ai_117 as strength_source
import inspect_native_resource_areas_117 as resource_area_source
import inspect_native_primary_orders_117 as orders
import inspect_native_training_117 as training_source
UNIT=0x5DA3A0;SPRITE=0x804000;IMAGE=0x805000;STACK=0x900000;STOP=0xA00000;SCRIPT=0xD00000;ASSETS=0xE00000
ALLOW=sorted(set(list(ai_nuke_source.ALLOW)+list(town_source.ALLOW)+list(automatic_source.ALLOW)+list(strength_source.ALLOW)+list(resource_area_source.BLOCKS.values())+list(wave_source.ALLOW)+list(local.BLOCKS.values())))
PLAN=dict(target_sha256=local.TARGET_HASH,helper_stubs=[],instruction_bound=100000,scope='Declared stock-opcode Town macro from genuine CC/SCV to paid Depot, Barracks, Academy and Firebat',limits='Private stock objects/assets, actual order/effect consumers; declared synthetic terrain/player/pools, no Windows imports/WinMain/host I/O, no full game/campaign')
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--exe',type=Path,required=True);p.add_argument('--data',type=Path,required=True);p.add_argument('--manifest',type=Path,required=True);p.add_argument('--assets',type=Path,required=True);p.add_argument('--asset-manifest',type=Path,required=True);p.add_argument('--pyms',type=Path,required=True);p.add_argument('--string-manifest',type=Path,required=True);p.add_argument('--out',type=Path);p.add_argument('--execute-original-code',action='store_true');p.add_argument('--profiles',choices=('all','paid','cash399','academy_unavailable'),default='all');a=p.parse_args()
 raw=a.exe.read_bytes();assert hashlib.sha256(raw).hexdigest()==local.TARGET_HASH
 if not a.execute_original_code:print(json.dumps(PLAN,indent=2));return
 assert a.out and platform.machine().lower() in ('x86_64','amd64')
 script_hash_at_start=hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
 dependency_hashes_at_start={Path(module.__file__).name:hashlib.sha256(Path(module.__file__).read_bytes()).hexdigest()for name,module in sorted(sys.modules.items())if getattr(module,'__file__',None)and Path(module.__file__).parent.resolve()==Path(__file__).parent.resolve()and name!='__main__'}
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
 ai_bin=stock('scripts\\aiscript.bin');bw_bin=stock('scripts\\bwscript.bin');script=stock('scripts\\iscript.bin');orders_raw=stock('arr\\orders.dat');orderrows=orders.stock_orders(orders_raw);tech_raw=stock('arr\\techdata.dat');stat_txt=stock('rez\\stat_txt.tbl');table_raw['orders']=orders_raw
 for row in assetmanifest['images']:
  v=(a.assets/row['path'].replace('\\','/')).read_bytes();assert hashlib.sha256(v).hexdigest()==row['sha256'];assets[row['image_id']]=(row,v)
 for row in assetmanifest['lo_files']:
  v=(a.assets/row['path'].replace('\\','/')).read_bytes();assert hashlib.sha256(v).hexdigest()==row['sha256'];lo_assets.append((row,v))
 import unicorn
 from unicorn import Uc,UC_ARCH_X86,UC_MODE_32,UC_HOOK_CODE,UC_HOOK_MEM_INVALID
 from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_EBX,UC_X86_REG_ECX,UC_X86_REG_EDX,UC_X86_REG_EDI,UC_X86_REG_ESP,UC_X86_REG_ESI,UC_X86_REG_FPCW,UC_X86_REG_MXCSR
 pe=pefile.PE(data=raw);assert pe.OPTIONAL_HEADER.ImageBase==0x400000 and pe.FILE_HEADER.Machine==0x14C
 gate_range=local.requirements.WITNESSES['unit_requirements_gate_and_interpreter'];gate_dis=local.capstone.Cs(local.capstone.CS_ARCH_X86,local.capstone.CS_MODE_32);gate_return_sites={i.address for i in gate_dis.disasm(pe.get_data(gate_range[0]-0x400000,gate_range[1]-gate_range[0]),gate_range[0])if i.mnemonic=='ret'}
 image=pe.get_memory_mapped_image();size=(pe.OPTIONAL_HEADER.SizeOfImage+4095)&~4095;cases=[];cache={};initial_seeds={};executed_stats=[]
 progress_started=False
 def w(u,address,value,width=4):
  assert not progress_started or(address==0x52651c and width==4),('Unauthorized host progress write',hex(address))
  u.mem_write(address,(value&((1<<(8*width))-1)).to_bytes(width,'little'))
 def r(u,address,width=4):return int.from_bytes(u.mem_read(address,width),'little')
 def new(unit_count=4,sprite_count=8,image_count=32,thingy_count=8,seed=12345,rng_enabled=True):
  u=Uc(UC_ARCH_X86,UC_MODE_32);u.reg_write(UC_X86_REG_FPCW,0x37f);u.reg_write(UC_X86_REG_MXCSR,0x1f80);u.mem_map(0x400000,size);u.mem_write(0x400000,image)
  for addr,length in ((0x800000,0x20000),(STACK,0x40000),(STOP,0x1000),(SCRIPT,0x10000),(ASSETS,0x800000),(0xB00000,0xA0000),(0xC00000,0x80000),(0x2300000,0x20000)):u.mem_map(addr,length)
  u.mem_write(SCRIPT,script);w(u,0x54330C,SCRIPT)
  at=ASSETS;allocated={}
  for ident,(row,v) in assets.items():
   if row['path'] not in allocated:u.mem_write(at,v);allocated[row['path']]=at;at+=(len(v)+4095)&~4095
   w(u,0x5436F8+ident*4,allocated[row['path']])
  for row,v in lo_assets:
   if row['path'] not in allocated:u.mem_write(at,v);allocated[row['path']]=at;at+=(len(v)+4095)&~4095
   if row['kind']<5:w(u,0x596400+(row['kind']*999+row['image_id'])*4,allocated[row['path']])
  assert at-ASSETS<0x780000
  u.mem_write(ASSETS+0x780000,stat_txt);w(u,0x6AB4F8,ASSETS+0x780000);assert len(network)<0x10000;u.mem_write(ASSETS+0x790000,network);w(u,0x665DE0,ASSETS+0x790000)
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
  u.mem_write(0x543228,struct.pack('<4H',0,0,4096,4096));w(u,0x543230,4096,2);w(u,0x543234,4096,2);w(u,0x5264B4,128,2);w(u,0x5264B6,128,2)
  for base,count in ((0x665B08,12),(0x5C51D0,256),(0x5C55D0,256),(0x673940,256)):u.mem_write(base,b'\0'*(count*4))
  for base in (0x665AF8,0x665AFC,0x665AE0,0x665AE4,0x665B38):w(u,base,0)
  w(u,0x673930,int(rng_enabled));w(u,0x67393C,seed);w(u,0x543300,0);w(u,0x543304,0)
  for i in range(thingy_count):w(u,0x809000+i*16,0x809000+(i-1)*16 if i else 0);w(u,0x809000+i*16+4,0x809000+(i+1)*16 if i+1<thingy_count else 0)
  w(u,0x5C7A20,0x809000 if thingy_count else 0);w(u,0x5C7A24,0x809000+(thingy_count-1)*16 if thingy_count else 0);w(u,0x5C7A28,0);w(u,0x5C7A2C,0)
  w(u,0x6D3140,0)
  w(u,0x508D50,0);w(u,0x67CC90,0);w(u,0x6726D0+3*36,2,1);w(u,0x6726D0+4*36,2,1)
  initial_seeds[id(u)]=seed
  return u
 def call(u,start,ecx,edx=0,args=(),bound=100000):
  visited=set();calls=[];script_ops=[];impacts=[];initializations=[];instructions=0;boundary=False;recent=deque(maxlen=32);requirements_stack=[];requirement_returns=[];cancellations=[];entries={lo for lo,hi in ALLOW}
  def hook(m,addr,width,_):
   nonlocal instructions,boundary
   if addr==STOP:boundary=True;m.emu_stop();return
   key=(addr,width)
   if key not in cache:cache[key]=tuple(lo for lo,hi in ALLOW if lo<=addr and addr+width<=hi)
   if not cache[key]:raise RuntimeError(f'Outside reviewed original code {addr:#x}; recent={list(recent)[-15:]}')
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
   if addr==0x4b32a0:
    sp=m.reg_read(UC_X86_REG_ESP);requested=m.reg_read(UC_X86_REG_ECX)&65535;player=m.reg_read(UC_X86_REG_EDX);producer=r(m,sp+4)
    requirements_stack.append(dict(requested=requested,player=player,producer=producer,producer_type=r(m,producer+0x64,2),completed_Academy=r(m,0x52c0c4+(112*12+player)*4),completed_Barracks=r(m,0x52c0c4+(111*12+player)*4)))
   if addr in gate_return_sites:
    row=requirements_stack.pop();row.update(return_eax=m.reg_read(UC_X86_REG_EAX),error=r(m,0x6be654));requirement_returns.append(row)
   if addr==0x41b810:
    sp=m.reg_read(UC_X86_REG_ESP);producer=m.reg_read(UC_X86_REG_ECX);owner=r(m,producer+0x4c,1)
    cancellations.append(dict(producer=producer,relative_slot=r(m,sp+4),queue=list(struct.unpack('<5H',m.mem_read(producer+0x98,10))),cursor=r(m,producer+0xa4,1),stocks_before=[r(m,0x5263d0+owner*4),r(m,0x526400+owner*4)]))
   if addr in entries:
    sp=m.reg_read(UC_X86_REG_ESP);calls.append(dict(callee=hex(addr),return_to=r(m,sp),ecx=m.reg_read(UC_X86_REG_ECX),edx=m.reg_read(UC_X86_REG_EDX),stack=[r(m,sp+4+i*4) for i in range(6)]))
  def invalid(m,access,address,width,value,_):
   raise RuntimeError(f'Invalid native memory access={access} address={address:#x} size={width}; EAX={m.reg_read(UC_X86_REG_EAX):#x} ECX={m.reg_read(UC_X86_REG_ECX):#x} EDX={m.reg_read(UC_X86_REG_EDX):#x}')
  bad=u.hook_add(UC_HOOK_MEM_INVALID,invalid)
  h=u.hook_add(UC_HOOK_CODE,hook);sp=STACK+0x3f000;u.mem_write(sp,struct.pack('<'+'I'*(len(args)+1),STOP,*args));u.reg_write(UC_X86_REG_ESP,sp);u.reg_write(UC_X86_REG_ECX,ecx);u.reg_write(UC_X86_REG_EDX,edx)
  try:u.emu_start(start,STOP+1,count=bound)
  except Exception as e:raise RuntimeError(f'{e}; recent={list(recent)[-30:]}; calls={calls[-10:]}') from e
  finally:u.hook_del(h);u.hook_del(bad)
  assert not requirements_stack
  assert boundary and u.reg_read(UC_X86_REG_ESP)==sp+4*(len(args)+1),(hex(start),instructions,bound,list(recent)[-10:])
  executed_stats.append(dict(entry=hex(start),instruction_count=instructions,instruction_bound=bound,visited_functions=[hex(x)for x in sorted(visited)]))
  return dict(unit_requirement_returns=requirement_returns,training_cancellations=cancellations,complete_return=True,instruction_bound=bound,return_eax=u.reg_read(UC_X86_REG_EAX),instruction_count=instructions,calls=calls,iscript_opcodes=script_ops,bullet_initializations=initializations,impact_events=impacts,visited_functions=[hex(x) for x in sorted(visited)])
 def record(name,inputs,actual,expected,ex,state=None):
  assert actual==expected,(name,actual,expected)
  counters={str(k):r(u,0x673940+k*4) for k in range(256)};seed=initial_seeds[id(u)];assert r(u,0x673938)==sum(counters.values());assert r(u,0x67393C)==advance(seed,sum(counters.values()))
  ex['rng_validation']=dict(initial_seed=seed,total_calls=sum(counters.values()),source_counters=counters,final_state=r(u,0x67393C),independent_integer_LCG_matches=True)
  cases.append(dict(case=name,inputs=inputs,actual=actual,expected=expected,passed=True,execution=ex,native_state=state))
 def advance(seed,n):
  for i in range(n):seed=(seed*22695477+1)&0xFFFFFFFF
  return seed
 def joined(executions):
  return dict(unit_requirement_returns=[c for x in executions for c in x['unit_requirement_returns']],training_cancellations=[c for x in executions for c in x['training_cancellations']],complete_return=all(x['complete_return'] for x in executions),instruction_bound=max(x['instruction_bound']for x in executions),complete_call_count=len(executions),return_eax=executions[-1]['return_eax'],instruction_count=sum(x['instruction_count'] for x in executions),calls=[c for x in executions for c in x['calls']],iscript_opcodes=[c for x in executions for c in x['iscript_opcodes']],bullet_initializations=[c for x in executions for c in x['bullet_initializations']],impact_events=[c for x in executions for c in x['impact_events']],visited_functions=sorted({c for x in executions for c in x['visited_functions']}))
 def environment(u,visibility=0):
  w(u,0x69B584,0xB00000);w(u,0xB00000,4,2);w(u,0xB44A0C,200*256);w(u,0xB44A10,200*256);w(u,0xB97A1C,0x80A000)
  for address,offset in ((0x666454,0),(0x666458,0x10000),(0x66696C,0x20000),(0x6CE650,0x40000),(0x66644C,0x50000)):w(u,address,0xC00000+offset)
  w(u,0x54323C,0xC10000);u.mem_write(0xC00000,struct.pack('<I',0x1FFFF if not visibility else 0x1FF00)*16384);w(u,0x67CF60,0xC60000)
  w(u,0x6D319C,2);w(u,0x5C99B0,0);w(u,0x5D6E40,256);w(u,0x5D6E44,160)
  w(u,0x5264CC+12,8);w(u,0x672668,1);w(u,0x67266C,1)
  w(u,0x5AEF00,0x80B000);w(u,0x5AEF04,0x80B000+31*20);w(u,0x5AEF08,0)
  for i in range(32):w(u,0x80B000+i*20,0x80B000+(i-1)*20 if i else 0);w(u,0x80B000+i*20+4,0x80B000+(i+1)*20 if i<31 else 0)
  w(u,0x5DA390,2);w(u,0x5DA394,2);w(u,0x6663EC,0,1);w(u,0x6751F4,0);w(u,0x6BE650,0,1);w(u,0x665D40,0);w(u,0x665B60,0)
 def sprite_position(uc,at):
  sp=r(uc,at+0xC);return [r(uc,sp+0x14,2),r(uc,sp+0x16,2)]
 def create(u,typ,x,y,owner=3):
  c=call(u,0x424900,typ,x,(y,owner));at=c['return_eax'];assert at
  return at,[c,call(u,0x424da0,at)]
 def initialize_tech_world(u,profile):
  environment(u,255);w(u,0xB00000,2,2)
  for supplied,used,cap in training_source.SUPPLY_TABLES:w(u,cap+12,400)
  w(u,0x5263d0+12,399 if profile=='cash399' else 450);w(u,0x526400+12,25)
  for typ in(7,109,111,112,32):w(u,0x52655c+3*228+typ,int(not(profile=='academy_unavailable'and typ==112)),1)
  path_pool=0x81b000;w(u,0x69b574,path_pool);w(u,0x69b570,path_pool)
  for j in range(32):w(u,path_pool+j*128,j+2 if j<31 else 0);w(u,path_pool+j*128+4,j+1)
  u.mem_write(0xB0000c,struct.pack('<H',1)*65536)
  region=0xB00000+0x449fc+64;w(u,region,0,2);w(u,region+2,1,2);w(u,region+0xc,region+0x2c)
  w(u,region+0x10,768*256);w(u,region+0x14,512*256);u.mem_write(region+0x18,struct.pack('<4H',0,0,4096,4096))
  for owner in range(12):w(u,0x534914+owner*12+owner,1,1)
  w(u,0x6726d0+3*36,1,1);w(u,0x6726d1+3*36,1,1)
  ex=[call(u,0x4a98e0,0),call(u,0x425610,0),call(u,0x40a0e0,0),call(u,0x486a20,0),call(u,0x4b2640,0),call(u,0x4a9a90,0x6acc50)]
  for player in range(8):
   regional=0x80d000+player*0x400;u.mem_write(regional,bytes(0x400));w(u,0x6b1a74+player*4,regional)
   w(u,regional+0x34,1,2);w(u,regional+0x38,player,1);w(u,regional+0x34+0x2c,0x6acc50)
  worker,more=create(u,7,512,512,3);ex+=more;main,more=create(u,106,768,512,3);ex+=more
  ex.append(call(u,0x493330,0x6a1a18));w(u,0x6a2e6c,0x6a1a18);w(u,0x6a2e70,0)
  program=0x80f000;rectangle=0x80f100;u.mem_write(program,local.PROGRAM);w(u,0x6a2e74,program);w(u,0x6a2e78,program);u.mem_write(rectangle,struct.pack('<4I',400,400,900,800))
  c=call(u,0x494d90,3,0,(rectangle,0));ex.append(c);sc=c['return_eax'];ex.append(call(u,0x493a60,sc))
  for address in (0x6c46d4,0x69b568,0x6663e4):w(u,address,10000)
  w(u,0x69b564,0);w(u,0x67f7a8,2);w(u,0x685574,2);w(u,0x6b1a94,3);w(u,0x6b1a98,1)
  w(u,0x6a5d68,0);w(u,0x5432f0,0);w(u,0x5349d8,100)
  w(u,0x67517c,0);w(u,0x6ca750,0,2);w(u,0x6ca744,0,2);w(u,0x5349d4,0);w(u,0x5349ec,0,1)
  root_widget=0x81a000;child_widget=0x81a100;w(u,0x6ab3f4,root_widget);w(u,root_widget+0x42,child_widget);w(u,child_widget+0x20,0xfff6,2)
  w(u,0x508d48,0);w(u,0x508d54,1)
  return worker,main,sc,ex
 def ai_list(u,head):
  result=[];at=r(u,head)
  while at:
   result.append(dict(address=at,next=r(u,at),previous=r(u,at+4),kind=r(u,at+8,1),unit=r(u,at+0x10),town=r(u,at+0x14)))
   at=r(u,at);assert len(result)<1024
  return result
 def tech_state(u,sc,worker,main):
  player=0x67d068+3*0x4e8;town=r(u,sc+0x2c);units=[]
  for j in range(16):
   at=UNIT+j*0x150
   if not r(u,at+0xc):continue
   ai=r(u,at+0x134)
   units.append(dict(address=at,type=r(u,at+0x64,2),owner=r(u,at+0x4c,1),order=r(u,at+0x4d,1),order_state=r(u,at+0x4e,1),secondary=r(u,at+0xa6,1),secondary_state=r(u,at+0xe2,1),target=r(u,at+0x5c),goal=[r(u,at+0x58,2),r(u,at+0x5a,2)],position=sprite_position(u,at),time=r(u,at+0xac,2),hp=r(u,at+8),status=r(u,at+0xdc),completed=bool(r(u,at+0xdc)&1),queue=list(struct.unpack('<5H',u.mem_read(at+0x98,10))),cursor=r(u,at+0xa4,1),child=r(u,at+0xec),AI=ai,AI_kind=r(u,ai+8,1)if ai else None,AI_town=r(u,ai+0x14)if ai and r(u,ai+8,1)in(2,3)else None,queue_tags=list(u.mem_read(ai+9,5))if ai and r(u,ai+8,1)==3 else None,queue_contexts=[r(u,ai+0x18+k*4)for k in range(5)]if ai and r(u,ai+8,1)==3 else None,military_region=r(u,ai+0x10)if ai and r(u,ai+8,1)==4 else None,assigned_region=r(u,r(u,ai+0x10),2)if ai and r(u,ai+8,1)==4 else None))
  regional=0x80d000+3*0x400+0x34
  return dict(native_region=dict(address=regional,state=r(u,regional+5,1),ground_count=r(u,regional+0xa,2),military_head=r(u,regional+0x30)),script_PC=r(u,sc+8),script_wait=r(u,sc+0xc),script_active=r(u,0x6a2e70),elapsed=r(u,0x5349d8),stocks=[r(u,0x5263d0+12),r(u,0x526400+12)],supply=[r(u,at+12)for at in training_source.SUPPLY_TABLES[1]],completed_counts={str(typ):r(u,0x52c0c4+(typ*12+3)*4)for typ in(7,106,109,111,112,32)},raw_counts={str(typ):r(u,0x529604+(typ*12+3)*4)for typ in(7,106,109,111,112,32)},explicit_training_word=r(u,player+0x220,2),heap_count=r(u,player+0x210,1),heap_entries=[list(struct.unpack('<BBHI',u.mem_read(player+0x18+i*8,8)))for i in range(r(u,player+0x210,1))],demand_and_available=[r(u,player+x)for x in(0,4,8,12,16,20)],town=dict(address=town,main=r(u,town+0x24),builder=r(u,town+0x28),worker_count=r(u,town+0x1a,1),workers=ai_list(u,town+0xc),buildings=ai_list(u,town+0x14),requests=[list(u.mem_read(town+0x3c+i*4,4))for i in range(4)]),units=units)
 for profile in ('paid','cash399','academy_unavailable'):
  if a.profiles not in ('all',profile):continue
  progress_started=False
  u=new(unit_count=16,sprite_count=64,image_count=256,thingy_count=64);worker,main,sc,executions=initialize_tech_world(u,profile)
  initial=tech_state(u,sc,worker,main);assert initial['completed_counts']=={'7':1,'106':1,'109':0,'111':0,'112':0,'32':0}
  assert initial['town']['worker_count']==1 and initial['town']['main']==main
  timeline=[];previous=None;services=[];requirements=[];completion_callbacks={};payment_callbacks=[];service_first=None
  print('Tech build initial',initial,flush=True)
  progress_started=True
  for tick in range(5000 if profile=='paid'else 3500):
   w(u,0x52651c,r(u,0x52651c)+1)
   try:
    step=call(u,0x445070,0,bound=8000000);executions.append(step);executions.append(call(u,0x4be410,42))
   except Exception as error:raise RuntimeError(f'Tech build callback={tick+1}; state={tech_state(u,sc,worker,main)}; {error}')from error
   current=tech_state(u,sc,worker,main)
   interesting=[c for c in step['calls']if c['callee']in('0x487580','0x487110','0x487de0','0x41b550','0x41b060','0x48d320','0x487990','0x483400','0x4865b0','0x4872f0','0x4b32a0','0x4b2010','0x486200','0x486340','0x439420','0x48cd70','0x421f90')]
   if interesting:services.append(dict(callback=tick+1,calls=interesting))
   if service_first is None and any(c['callee']=='0x487580'and c['ecx']==worker for c in step['calls']):service_first=dict(callback=tick+1,actual_arguments=next(c for c in step['calls']if c['callee']=='0x487580'and c['ecx']==worker))
   for typ in(109,111,112,32):
    if current['completed_counts'][str(typ)]and typ not in completion_callbacks:completion_callbacks[typ]=tick+1
   if step['unit_requirement_returns']:requirements.append(dict(callback=tick+1,returns=step['unit_requirement_returns']))
   key=(current['script_PC'],tuple(current['stocks']),tuple(current['completed_counts'].values()),tuple((x['type'],x['order'],x['order_state'],x['secondary'],x['secondary_state'],x['completed'],tuple(x['queue']),x['AI_kind'])for x in current['units']))
   if key!=previous:
    timeline.append(dict(callback=tick+1,state=current,selected_calls=interesting));previous=key
    print('Tech build',profile,tick+1,'pc',current['script_PC'],'stocks',current['stocks'],'counts',current['completed_counts'],'units',[(x['type'],x['order'],x['order_state'],x['time'],x['position'],x['AI_kind'])for x in current['units']],flush=True)
   if profile=='paid'and current['completed_counts']['32']==1 and current['script_active']==0:break
   if profile!='paid'and current['completed_counts']['111']==1 and tick+1-completion_callbacks[111]>=95:break
  else:raise AssertionError(('tech chain did not reach bounded result',profile,timeline[-2:]))
  expected_counts={'7':1,'106':1,'109':1,'111':1,'112':1 if profile=='paid'else 0,'32':1 if profile=='paid'else 0}
  assert current['completed_counts']==expected_counts
  assert current['stocks']==([0,0]if profile=='paid'else[149,25]if profile=='cash399'else[200,25])
  assert service_first and current['town']['worker_count']==1
  for building in current['units']:
   if building['type']in(106,109,111,112):assert building['AI_kind']==3 and building['AI_town']==current['town']['address']
  if profile=='paid':
   assert all(current['completed_counts'][str(typ)]==1 for typ in(109,111,112,32))
   assert current['script_PC']==len(local.PROGRAM) and current['script_wait']==0xffffffff
   child=next(x for x in current['units']if x['type']==32);assert child['AI_kind']==4 and child['assigned_region']==1 and child['military_region']==current['native_region']['address'] and current['native_region']['ground_count']==1,(child,current['native_region'])
   assert r(u,child['AI']+0xc)==child['address'] and current['native_region']['military_head']==child['AI']
   assert any(row['requested']==32 and row['producer_type']==111 and row['return_eax']==1 for entry in requirements for row in entry['returns'])
  if profile!='paid':
   barracks=next(x['address']for x in current['units']if x['type']==111)
   gate=call(u,0x4b32a0,32,3,(barracks,));executions.append(gate)
   assert gate['return_eax']==0xffffffff and r(u,0x6be654)==8
   requirements.append(dict(callback='after bounded object window',returns=gate['unit_requirement_returns']))
  assert current['elapsed']==100+1+tick//16
  record('original_stock_opcode_paid_tech_chain_'+profile,dict(declared_macro=local.PROGRAM.hex(),macro_is_synthetic_composition=True,genuine_initial_unit_types=[7,106],no_host_prerequisite_or_completion_counts=True,no_host_AI_order_queue_resource_or_position_progress=True,only_host_progress_input='logical simulation frame 0x52651c',normal_construction_and_training=True,initial=initial,actual_object_callbacks=tick+1,actual_timer_services=tick+1,first_natural_worker_service=service_first,completion_callbacks=completion_callbacks,timeline=timeline,native_service_history=services,requirement_return_history=requirements),[current['completed_counts'],current['stocks'],current['town']['worker_count']],[expected_counts,[0,0]if profile=='paid'else[149,25]if profile=='cash399'else[200,25],1],joined(executions),current)
 modules={Path(module.__file__).name:hashlib.sha256(Path(module.__file__).read_bytes()).hexdigest()for name,module in sorted(sys.modules.items())if getattr(module,'__file__',None)and Path(module.__file__).parent.resolve()==Path(__file__).parent.resolve()and name!='__main__'}
 assert modules==dependency_hashes_at_start and hashlib.sha256(Path(__file__).read_bytes()).hexdigest()==script_hash_at_start,'Probe sources changed during execution'
 result=dict(target_sha256=local.TARGET_HASH,target_version='1.17.0.1',probe_script_sha256=script_hash_at_start,source_dependencies_sha256=modules,case_profile=a.profiles,engine=dict(unicorn=unicorn.__version__,pefile=pefile.__version__,capstone=local.capstone.__version__,machine=platform.machine(),python=platform.python_version(),platform=platform.system()),dat_sha256=dat_hashes,asset_manifest_sha256=hashlib.sha256(a.asset_manifest.read_bytes()).hexdigest(),string_asset_manifest_sha256=hashlib.sha256(a.string_manifest.read_bytes()).hexdigest(),network_tbl_sha256=hashlib.sha256(network).hexdigest(),helper_stubs=[],original_code_executed=True,complete_game_executed=False,instruction_bound_default=100000,instruction_bound_object_callback=8000000,RNG_source_slots_validated=256,complete_original_function_calls=len(executed_stats),max_single_call_instructions=max(x['instruction_count']for x in executed_stats),total_original_instructions_executed=sum(x['instruction_count']for x in executed_stats),visited_functions=sorted({f for x in executed_stats for f in x['visited_functions']}),reviewed_code_ranges=[dict(start=hex(lo),end_exclusive=hex(hi))for lo,hi in ALLOW],complete_cases=len(cases),prefix_cases=0,all_passed=all(x['passed']for x in cases),cases=cases)
 a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(dict(cases=len(cases),all_passed=result['all_passed'])))
if __name__=='__main__':main()
