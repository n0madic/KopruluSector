#!/usr/bin/env python3
"""Probe an authentic campaign attack fragment and original connected wave consumers.

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
import inspect_native_ai_wave_117 as local
import probe_native_ai_nuke_117 as ai_nuke_source
import probe_native_ai_construction_117 as town_source
import probe_native_automatic_requests_117 as automatic_source
import probe_native_combat_ai_117 as strength_source
import inspect_native_resource_areas_117 as resource_area_source
import inspect_native_primary_orders_117 as orders
import inspect_native_training_117 as training_source
UNIT=0x5DA3A0;SPRITE=0x804000;IMAGE=0x805000;STACK=0x900000;STOP=0xA00000;SCRIPT=0xD00000;ASSETS=0xE00000
ALLOW=sorted(set(list(ai_nuke_source.ALLOW)+list(town_source.ALLOW)+list(automatic_source.ALLOW)+list(strength_source.ALLOW)+list(resource_area_source.BLOCKS.values())+list(local.BLOCKS.values())))
PLAN=dict(target_sha256=local.TARGET_HASH,helper_stubs=[],instruction_bound=100000,scope='Authentic Ter5 attack fragment to original periodic Guard recruitment and regional wave dispatch',limits='Private stock objects/assets, actual order/effect consumers; declared synthetic terrain/player/pools, no Windows imports/WinMain/host I/O, no full game/campaign')
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--exe',type=Path,required=True);p.add_argument('--data',type=Path,required=True);p.add_argument('--manifest',type=Path,required=True);p.add_argument('--assets',type=Path,required=True);p.add_argument('--asset-manifest',type=Path,required=True);p.add_argument('--pyms',type=Path,required=True);p.add_argument('--string-manifest',type=Path,required=True);p.add_argument('--out',type=Path);p.add_argument('--execute-original-code',action='store_true');p.add_argument('--profiles',choices=('all','firebat','mixed','missing'),default='all');a=p.parse_args()
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
 image=pe.get_memory_mapped_image();size=(pe.OPTIONAL_HEADER.SizeOfImage+4095)&~4095;cases=[];cache={};initial_seeds={};executed_stats=[]
 def w(u,address,value,width=4):u.mem_write(address,(value&((1<<(8*width))-1)).to_bytes(width,'little'))
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
  h=u.hook_add(UC_HOOK_CODE,hook);sp=STACK+0x3f000;u.mem_write(sp,struct.pack('<'+'I'*(len(args)+1),STOP,*args));u.reg_write(UC_X86_REG_ESP,sp);u.reg_write(UC_X86_REG_ECX,ecx);u.reg_write(UC_X86_REG_EDX,edx)
  try:u.emu_start(start,STOP+1,count=bound)
  except Exception as e:raise RuntimeError(f'{e}; recent={recent[-30:]}; calls={calls[-10:]}') from e
  finally:u.hook_del(h);u.hook_del(bad)
  assert boundary and u.reg_read(UC_X86_REG_ESP)==sp+4*(len(args)+1),(hex(start),instructions,bound,recent[-10:])
  executed_stats.append(dict(entry=hex(start),instruction_count=instructions,instruction_bound=bound,visited_functions=[hex(x)for x in sorted(visited)]))
  return dict(complete_return=True,instruction_bound=bound,return_eax=u.reg_read(UC_X86_REG_EAX),instruction_count=instructions,calls=calls,iscript_opcodes=script_ops,bullet_initializations=initializations,impact_events=impacts,visited_functions=[hex(x) for x in sorted(visited)])
 def record(name,inputs,actual,expected,ex,state=None):
  assert actual==expected,(name,actual,expected)
  counters={str(k):r(u,0x673940+k*4) for k in range(256)};seed=initial_seeds[id(u)];assert r(u,0x673938)==sum(counters.values());assert r(u,0x67393C)==advance(seed,sum(counters.values()))
  ex['rng_validation']=dict(initial_seed=seed,total_calls=sum(counters.values()),source_counters=counters,final_state=r(u,0x67393C),independent_integer_LCG_matches=True)
  cases.append(dict(case=name,inputs=inputs,actual=actual,expected=expected,passed=True,execution=ex,native_state=state))
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
 def sprite_position(uc,at):
  sp=r(uc,at+0xC);return [r(uc,sp+0x14,2),r(uc,sp+0x16,2)]
 def create(u,typ,x,y,owner=3):
  c=call(u,0x424900,typ,x,(y,owner));at=c['return_eax'];assert at
  return at,[c,call(u,0x424da0,at)]
 def initialize_wave_world(u,types):
  environment(u,255);w(u,0xB00000,4,2)
  for supplied,used,cap in training_source.SUPPLY_TABLES:w(u,cap+12,400)
  path_pool=0x81b000;w(u,0x69b574,path_pool);w(u,0x69b570,path_pool)
  for j in range(32):w(u,path_pool+j*128,j+2 if j<31 else 0);w(u,path_pool+j*128+4,j+1)
  u.mem_write(0xB0000c,b''.join(struct.pack('<H',1 if x<10 else 3 if x<20 else 2)for y in range(256)for x in range(256)))
  graph={1:[2,3],2:[1,3],3:[1,2]}
  for idx,center,bounds in ((1,(128,128),(0,0,320,4096)),(2,(768,128),(640,0,4096,4096)),(3,(512,128),(320,0,640,4096))):
   at=0xB00000+0x449fc+idx*64;w(u,at,0,2);w(u,at+2,1,2);w(u,at+4,100,2);w(u,at+6,2,1);w(u,at+7,2,1)
   neighbors=0xB60000+idx*8;u.mem_write(neighbors,struct.pack('<2H',*graph[idx]));w(u,at+0xc,neighbors)
   w(u,at+0x10,center[0]*256);w(u,at+0x14,center[1]*256);u.mem_write(at+0x18,struct.pack('<4H',*bounds))
  for owner in range(12):w(u,0x534914+owner*12+owner,1,1)
  w(u,0x6726d0+3*36,1,1);w(u,0x6726d1+3*36,1,1);w(u,0x6726d0+4*36,2,1)
  ex=[call(u,0x4a98e0,0,bound=100000),call(u,0x425610,0),call(u,0x40a0e0,0),call(u,0x486a20,0),call(u,0x4b2640,0),call(u,0x4a9a90,0x6acc50)]
  for player in range(8):
   regional=0x80d000+player*0x400;u.mem_write(regional,bytes(0x400));w(u,0x6b1a74+player*4,regional)
   for idx in (1,2,3):w(u,regional+idx*0x34,idx,2);w(u,regional+idx*0x34+4,player,1);w(u,regional+idx*0x34+0x2c,0x6acc50)
  own,more=create(u,106,128,128,3);ex+=more;enemy,more=create(u,106,768,128,4);ex+=more;troops=[]
  for j,typ in enumerate(types):
   at,more=create(u,typ,224+(j%2)*32,96+(j//2)*48,3);ex+=more;ex.append(call(u,0x4b1fe0,at));assert r(u,at+0x134);troops.append(at)
  ex.append(call(u,0x493330,0x6a1a18));w(u,0x6a2e6c,0x6a1a18);w(u,0x6a2e70,0)
  program=0x80f000;rect=0x80f100;u.mem_write(program,bytes([56,3,35,34,0,36]));w(u,0x6a2e74,program);w(u,0x6a2e78,program);u.mem_write(rect,struct.pack('<4I',64,64,288,224))
  c=call(u,0x494d90,3,0,(rect,0));ex.append(c);ex.append(call(u,0x493a60,c['return_eax']))
  # Genuine periodic AI cadence begins from declared counters; no later patches.
  for address in (0x6c46d4,0x69b568,0x6663e4):w(u,address,10000)
  w(u,0x69b564,0);w(u,0x67f7a8,2);w(u,0x685574,2);w(u,0x6b1a94,3);w(u,0x6b1a98,1)
  w(u,0x6a5d68,0);w(u,0x5432f0,0);w(u,0x5349d8,100)
  w(u,0x67517c,0);w(u,0x6ca750,0,2);w(u,0x6ca744,0,2);w(u,0x5349d4,0);w(u,0x5349ec,0,1)
  # Declared hidden countdown widget makes the actual HUD helper return normally.
  root_widget=0x81a000;child_widget=0x81a100;w(u,0x6ab3f4,root_widget);w(u,root_widget+0x42,child_widget);w(u,child_widget+0x20,0xfff6,2)
  w(u,0x508d48,0);w(u,0x508d54,1)
  return own,enemy,troops,ex
 def wave_state(u,sc,troops):
  base=0x67d068+3*0x4e8;regional=0x80d000+3*0x400
  return dict(script_PC=r(u,sc+8),script_wait=r(u,sc+0xc),elapsed=r(u,0x5349d8),slow_counter=r(u,0x69b564),slow_player=r(u,0x67f7a8),regional_cursor=[r(u,0x6b1a94),r(u,0x6b1a98)],grouping=r(u,base+0x21e,2),last_attack=r(u,base+0x228),roster=list(struct.unpack('<64H',u.mem_read(base+0x230,128))),heap_count=r(u,base+0x210,1),heap_entries=[list(struct.unpack('<BBHI',u.mem_read(base+0x18+i*8,8)))for i in range(r(u,base+0x210,1))],demand_and_available=[r(u,base+x)for x in(0,4,8,12,16,20)],guard_remnants=[dict(address=0x6b6908+i*32,parent=r(u,0x6b6908+i*32+0xc),type=r(u,0x6b6908+i*32+0x10,2),death_count=r(u,0x6b6908+i*32+9,1),previous_update=r(u,0x6b6908+i*32+0x1c))for i in range(len(troops))],regions=[dict(id=i,state=r(u,regional+i*0x34+5,1),destination=r(u,regional+i*0x34+2,2),ground_count=r(u,regional+i*0x34+0xa,2),military_head=r(u,regional+i*0x34+0x30))for i in(1,2,3)],troops=[dict(address=at,type=r(u,at+0x64,2),AI=r(u,at+0x134),AI_kind=r(u,r(u,at+0x134)+8,1)if r(u,at+0x134)else None,assigned_region=r(u,r(u,at+0x134)+0x10)if r(u,at+0x134)and r(u,r(u,at+0x134)+8,1)==4 else None,order=r(u,at+0x4d,1),target=r(u,at+0x5c),position=sprite_position(u,at)if r(u,at+0xc)else None,hp=r(u,at+8),cached_strength=[r(u,at+0x138,2),r(u,at+0x13a,2)])for at in troops])
 for profile,types,entry,expected_PC,expected_next_wait in (('firebat',(32,32,32,32),0x1c8e,0x1c9b,4500),('mixed',(0,0,0,0,32,32),0x1c9b,0x1cac,3000),('missing',(32,32,32),0x1c8e,0x1c9b,4500)):
  if a.profiles not in ('all',profile):continue
  u=new(unit_count=16,sprite_count=64,image_count=256,thingy_count=64);own,enemy,troops,executions=initialize_wave_world(u,types)
  u.mem_write(0x2300000,ai_bin);u.mem_write(0x2310000,bw_bin);w(u,0x6a2e74,0x2300000);w(u,0x6a2e78,0x2310000)
  assert ai_bin[0x1c8e:0x1c98]==bytes.fromhex('0c0420000d02f4010e0b')
  rectangle=0x80f100;c=call(u,0x494d90,3,entry,(rectangle,0));executions.append(c);sc=c['return_eax'];executions.append(call(u,0x493a60,sc,bound=500000))
  initial=wave_state(u,sc,troops);assert all(t['AI_kind']==1 for t in initial['troops']);print('Wave initial',initial,flush=True)
  timeline=[];previous=None;retry_visits=[];launch_events=[];enemy_hp_before=r(u,enemy+8)
  for tick in range(4000):
   w(u,0x52651c,r(u,0x52651c)+1)
   try:
    step=call(u,0x445070,0,bound=8000000);executions.append(step);executions.append(call(u,0x4be410,42))
   except Exception as error:raise RuntimeError(f'Wave callback={tick+1}; state={wave_state(u,sc,troops)}; {error}')from error
   current=wave_state(u,sc,troops)
   if current['script_PC'] in (0x1c96,0x1ca7)and current['script_wait']==30 and any(c['callee']=='0x493a60'and c['ecx']==sc for c in step['calls']):retry_visits.append(tick+1)
   for event in step['calls']:
    if event['callee']=='0x4b15d0':launch_events.append(dict(callback=tick+1,arguments=event))
   key=(current['script_PC'],current['grouping'],tuple(x['state']for x in current['regions']),tuple((x['AI_kind'],x['assigned_region'],x['order'])for x in current['troops']))
   if key!=previous:
    timeline.append(dict(callback=tick+1,state=current,selected_calls=[c for c in step['calls']if c['callee']in('0x483400','0x4af930','0x4ab040','0x4ae960','0x4b0ce0','0x4b15d0','0x4ae760','0x4af300')]))
    print('Wave',tick+1,current,flush=True);previous=key
   if current['script_PC']==expected_PC and current['script_wait']==expected_next_wait:break
  else:raise AssertionError(('authentic wave did not continue',timeline[-3:]))
  assert launch_events and current['regions'][1]['ground_count']==len(troops)
  assert all(t['AI_kind']==4 and t['assigned_region']==0x80d000+3*0x400+2*0x34 for t in current['troops'])
  assert current['elapsed']==100+1+tick//16
  if profile!='missing':assert tick+1==501 and r(u,enemy+8)<enemy_hp_before
  else:assert retry_visits and tick+1>501
  record('authentic_Ter5_'+profile+'_fragment_original_periodic_recruitment_and_launch',dict(entry_pc=entry,AIScript_sha256=hashlib.sha256(ai_bin).hexdigest(),initial=initial,timeline=timeline,native_launch_events=launch_events,attack_do_retry_callbacks=retry_visits,enemy_hp_before=enemy_hp_before,enemy_hp_after=r(u,enemy+8),physical_final_regions=[r(u,0xb0000c+2*((sprite_position(u,t)[1]>>5)*256+(sprite_position(u,t)[0]>>5)),2)for t in troops],actual_object_callbacks=tick+1,actual_timer_services=tick+1,declared_initial_racial_supply_caps=[400,400,400],no_host_AI_order_timer_position_or_signal_progress=True),[current['script_PC'],current['script_wait'],current['grouping'],current['last_attack'],any(current['roster'])],[expected_PC,expected_next_wait,0,0,False],joined(executions),current)
 modules={Path(module.__file__).name:hashlib.sha256(Path(module.__file__).read_bytes()).hexdigest()for name,module in sorted(sys.modules.items())if getattr(module,'__file__',None)and Path(module.__file__).parent.resolve()==Path(__file__).parent.resolve()and name!='__main__'}
 assert modules==dependency_hashes_at_start and hashlib.sha256(Path(__file__).read_bytes()).hexdigest()==script_hash_at_start,'Probe sources changed during execution'
 result=dict(target_sha256=local.TARGET_HASH,target_version='1.17.0.1',probe_script_sha256=script_hash_at_start,source_dependencies_sha256=modules,case_profile=a.profiles,engine=dict(unicorn=unicorn.__version__,pefile=pefile.__version__,capstone=local.capstone.__version__,machine=platform.machine(),python=platform.python_version(),platform=platform.system()),dat_sha256=dat_hashes,asset_manifest_sha256=hashlib.sha256(a.asset_manifest.read_bytes()).hexdigest(),string_asset_manifest_sha256=hashlib.sha256(a.string_manifest.read_bytes()).hexdigest(),network_tbl_sha256=hashlib.sha256(network).hexdigest(),helper_stubs=[],original_code_executed=True,complete_game_executed=False,instruction_bound_default=100000,instruction_bound_object_callback=8000000,RNG_source_slots_validated=256,complete_original_function_calls=len(executed_stats),max_single_call_instructions=max(x['instruction_count']for x in executed_stats),total_original_instructions_executed=sum(x['instruction_count']for x in executed_stats),visited_functions=sorted({f for x in executed_stats for f in x['visited_functions']}),reviewed_code_ranges=[dict(start=hex(lo),end_exclusive=hex(hi))for lo,hi in ALLOW],complete_cases=len(cases),prefix_cases=0,all_passed=all(x['passed']for x in cases),cases=cases)
 a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(dict(cases=len(cases),all_passed=result['all_passed'])))
if __name__=='__main__':main()
