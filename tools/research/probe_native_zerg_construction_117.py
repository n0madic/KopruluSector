#!/usr/bin/env python3
"""Probe paid Larva/Drone production and ordinary Zerg building construction.

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
import pefile
import inspect_native_zerg_construction_117 as local
import probe_native_nydus_117 as nydus_source
import probe_native_towns_117 as town_source
import probe_native_ai_construction_117 as ai_construction_source
import inspect_economy_frame_117 as frame_source
import inspect_native_primary_orders_117 as orders
import inspect_native_training_117 as admission
UNIT=0x5DA3A0;SPRITE=0x803000;IMAGE=0x805000;STACK=0x900000;STOP=0xA00000;SCRIPT=0xD00000;ASSETS=0xE00000
ALLOW=sorted(set(list(nydus_source.ALLOW)+list(town_source.ALLOW)+list(ai_construction_source.ALLOW)+list(local.BLOCKS.values())+list(admission.WITNESSES.values())+[frame_source.BLOCKS[k] for k in ('player_ai_round_robin','town_round_robin')]))
PLAN=dict(target_sha256=local.TARGET_HASH,helper_stubs=[],instruction_bound=1000000,scope='Paid Zerg construction and original Larva/Egg/Drone lifecycle',limits='Native-spawned Larva, paid Egg/Drone and same-slot Spawning Pool construction with stock populated graphics. Full selected object callbacks or explicitly listed per-unit phases; existing-creep terrain and finite private pools are inputs. No outer accepted frame, renderer, host imports or helper substitutions.')

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--exe',type=Path,required=True);p.add_argument('--data',type=Path,required=True);p.add_argument('--manifest',type=Path,required=True);p.add_argument('--assets',type=Path,required=True);p.add_argument('--asset-manifest',type=Path,required=True);p.add_argument('--string-manifest',type=Path,required=True);p.add_argument('--pyms',type=Path,required=True);p.add_argument('--out',type=Path);p.add_argument('--case-group',choices=('all','build','drone','cancel','failure','ai'),default='all');p.add_argument('--execute-original-code',action='store_true');a=p.parse_args()
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
 image=pe.get_memory_mapped_image();size=(pe.OPTIONAL_HEADER.SizeOfImage+4095)&~4095;cases=[];cache={};initial_seeds={};executed_stats=[]
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
    if addr==0x43c000:
     un=entry['ecx'];entry['original_drone_before_morph']=dict(hp=r(m,un+8),type=r(m,un+0x64,2),status=r(m,un+0xdc),ai=r(m,un+0x134),resources=[r(m,0x5263d0+12),r(m,0x526400+12)])
    calls.append(entry)
  h=u.hook_add(UC_HOOK_CODE,hook);sp=STACK+0x3000;u.mem_write(sp,struct.pack('<'+'I'*(len(args)+1),STOP,*args));u.reg_write(UC_X86_REG_ESP,sp);u.reg_write(UC_X86_REG_ECX,ecx);u.reg_write(UC_X86_REG_EDX,edx)
  try:u.emu_start(start,STOP+1,count=1000000)
  except Exception as e:
   reg_ebx=u.reg_read(UC_X86_REG_EBX);reg_ecx=u.reg_read(UC_X86_REG_ECX);reg_edx=u.reg_read(UC_X86_REG_EDX)
   extra=dict(ebx=hex(reg_ebx),ecx=hex(reg_ecx),edx=hex(reg_edx),unit_context=hex(r(u,0x543300)))
   if IMAGE<=reg_edx<IMAGE+32*0x40:extra['image']=dict(id=r(u,reg_edx+8,2),grp=hex(r(u,reg_edx+0x2c)),loaded_grp=hex(r(u,0x5436f8+r(u,reg_edx+8,2)*4)))
   raise RuntimeError(f'{e}; diagnostic={extra}; recent={recent[-30:]}; calls={calls[-10:]}') from e
  finally:u.hook_del(h)
  assert boundary and u.reg_read(UC_X86_REG_ESP)==sp+4*(len(args)+1),(hex(start),instructions,boundary,hex(u.reg_read(UC_X86_REG_ESP)),hex(sp),recent[-10:])
  executed_stats.append(dict(entry_va=hex(start),ecx=ecx,edx=edx,stack=list(args),instructions=instructions,complete_return=True))
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
  result.update(resources=[r(u,0x5263d0+12),r(u,0x526400+12)],zerg_counters=zerg_counters(u),owner_chain=chain(u,0x665b08+12,next_offset=0x6c))
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
  return dict(complete_return=all(x['complete_return'] for x in executions),complete_call_count=len(executions),return_eax=executions[-1]['return_eax'],instruction_count=sum(x['instruction_count'] for x in executions),call_entry_counts=dict(Counter(c['callee'] for x in executions for c in x['calls'])),calls=[c for x in executions for c in x['calls'] if int(c['callee'],16) in ({lo for lo,hi in local.BLOCKS.values()}|{0x478200,0x478270,0x41b4b0,0x42a340,0x42a2c0,0x4086b0,0x408650,0x408490,0x41a340,0x484cd0,0x486340,0x487900,0x424900,0x424da0,0x424ad0})],iscript_opcodes=[c for x in executions for c in x['iscript_opcodes']],bullet_initializations=[c for x in executions for c in x['bullet_initializations']],impact_events=[c for x in executions for c in x['impact_events']],damage_events=[c for x in executions for c in x['damage_events']],visited_functions=sorted({c for x in executions for c in x['visited_functions']}))
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
 def clear_world(u):
  normalize=environment(u,255)
  w(u,0xb00000,2,2);u.mem_write(0xb0000c,struct.pack('<H',1)*65536)
  region=0xb00000+0x449fc+64;w(u,region,0,2);w(u,region+2,1,2);w(u,region+0xc,region+0x2c)
  pool=0x80c000;w(u,0x69b574,pool);w(u,0x69b570,pool);w(u,pool,0)
  return normalize
 def gate_auxiliary_services(u):
  for at in (0x6c46d4,0x69b568,0x69b564,0x6663e4):w(u,at,1000)
  w(u,0x6a2e70,0);w(u,0x6a5d68,0);w(u,0x6b1a94,0);w(u,0x6b1a98,1);w(u,0x5432f0,0)
 def position(u,unit):
  sprite=r(u,unit+0xc);pixel=list(struct.unpack('<2H',u.mem_read(unit+0x28,4)));fixed=list(struct.unpack('<2I',u.mem_read(unit+0x2c,8)))
  if sprite:assert pixel==list(struct.unpack('<2H',u.mem_read(sprite+0x14,4))) and pixel==[v>>8 for v in fixed]
  return dict(unit=unit,pixel=pixel,fixed=fixed,sprite=sprite,status=r(u,unit+0xdc),order=r(u,unit+0x4d,1),order_state=r(u,unit+0x4e,1),target=r(u,unit+0x5c),movement_state=r(u,unit+0x97,1),speed=r(u,unit+0x38),heading=r(u,unit+0x21,1),path=r(u,unit+0x100),lifetime=r(u,unit+0x110,2),order_timer=r(u,unit+0x54,1),hangar_pointer_c0=r(u,unit+0xc0),outside_flag_cc=r(u,unit+0xcc,1))
 def object_step(u,executions):
  w(u,0x52651c,r(u,0x52651c)+1);executions.append(call(u,0x445070,0));assert r(u,0x543300)==r(u,0x543304)==r(u,0x543308)==0
 def zerg_world(u):
  executions=[clear_world(u),call(u,0x40a0e0,0),call(u,0x4b44a0,0)]
  w(u,0x6ca7a0,0xc30000);w(u,0x6ca7ac,128);w(u,0x6ca7b0,128)
  u.mem_write(0xc00000,struct.pack('<I',0x5041f700)*16384)
  w(u,0x534914+3*12+3,1,1);w(u,0x534914+4*12+4,1,1);w(u,0x6726d0+4*36,2,1)
  w(u,0x5263d0+12,1000);w(u,0x526400+12,1000)
  gate_auxiliary_services(u)
  return executions
 def unit_state(u,unit):
  sp=r(u,unit+0xc)
  return dict(position=position(u,unit),peer=r(u,unit+0xd0),hp=r(u,unit+8),build_time=r(u,unit+0xac,2),build_hp_gain=r(u,unit+0xa8,2),buttons=r(u,unit+0x94,2),order_signal=r(u,unit+0x4f,1),completed=bool(r(u,unit+0xdc)&1),secondary=r(u,unit+0xa6,1),images=[] if not sp else [dict(address=im,id=r(u,im+8,2),animation=r(u,im+0x16,1),pc=r(u,im+0x12,2),wait=r(u,im+0x17,1)) for im in chain(u,sp+0x1c)])
 def paid_drone(u,executions):
  created=call(u,0x424900,131,400,(400,3));assert created['return_eax']==UNIT;executions.extend((created,call(u,0x424da0,UNIT)))
  hatchery=UNIT;w(u,0x529424+12,400);w(u,0x529484+12,20);executions.append(call(u,0x43d0d0,hatchery));larva=UNIT+0x150
  assert r(u,larva+0x64,2)==35 and r(u,larva+0x80)==hatchery,snapshot(u)
  executions.append(call(u,0x41b4b0,larva,args=(41,)));assert executions[-1]['return_eax']==1
  executions.append(call(u,0x42a2c0,larva,args=(42,)));timeline=[]
  for tick in range(500):
   object_step(u,executions);timeline.append(dict(tick=tick+1,unit=unit_state(u,larva),hatchery=unit_state(u,hatchery),resources=[r(u,0x5263d0+12),r(u,0x526400+12)]))
   if r(u,larva+0x64,2)==41 and r(u,larva+0xdc)&1:break
  assert r(u,larva+0x64,2)==41 and r(u,larva+0xdc)&1,timeline[-1]
  return larva,timeline
 def zerg_counters(u):
  return dict(live={str(t):r(u,0x529604+(t*12+3)*4) for t in (35,36,41,131,142)},completed={str(t):r(u,0x52c0c4+(t*12+3)*4) for t in (35,36,41,131,142)},supply=dict(cap=r(u,0x529424+12),used=r(u,0x529454+12),total=r(u,0x529484+12)),active=chain(u,0x665ae0),hidden=chain(u,0x665af8),unit_count=r(u,0x665b38))
 if a.case_group in ('all','drone'):
  u=new();executions=zerg_world(u);larva,timeline=paid_drone(u,executions)
  record('paid_drone_from_native_spawned_larva',dict(stock_larva_parent_producer=True,original_paid_queue41b4b0=True,no_buildtime_signal_hp_or_stage_override=True),[r(u,larva+0x64,2),r(u,0x5263d0+12),bool(r(u,larva+0xdc)&1)],[41,950,True],joined(executions),dict(timeline=timeline,final=snapshot(u)))
 def start_pool(u,executions,drone,target=(832,528)):
  admitted=call(u,0x41b4b0,drone,args=(142,));executions.append(admitted);assert admitted['return_eax']==1
  admission_state=unit_state(u,drone);resources_at_admission=[r(u,0x5263d0+12),r(u,0x526400+12)]
  executions.append(call(u,0x42a340,drone,args=(25,*target)));timeline=[]
  for tick in range(300):
   object_step(u,executions);timeline.append(dict(tick=tick+1,type=r(u,drone+0x64,2),unit=unit_state(u,drone),resources=[r(u,0x5263d0+12),r(u,0x526400+12)]))
   if r(u,drone+0x64,2)==142:break
  assert r(u,drone+0x64,2)==142,timeline[-1]
  return admission_state,resources_at_admission,timeline
 if a.case_group in ('all','cancel'):
  for mode in ('early','middle','late','fractional_drone_damage','thingy_pool'):
   u=new(thingy_count=0 if mode=='thingy_pool' else 64);executions=zerg_world(u);drone,production=paid_drone(u,executions)
   if mode=='fractional_drone_damage':executions.append(call(u,0x41dfc0,drone,args=(1920,0,1,64,0,4)))
   admission_state,resources_at_admission,travel=start_pool(u,executions,drone)
   for tick in range({'middle':350,'late':1202}.get(mode,0)):object_step(u,executions)
   before=unit_state(u,drone);before_counters=zerg_counters(u);saved_drone_hp=r(u,drone+0xae,2)
   assert not before['completed']
   if mode=='late':assert before['build_time']==0 and before['position']['order_state']==6 and before['order_signal']==0
   executions.append(call(u,0x41a340,drone));after=unit_state(u,drone);after_counters=zerg_counters(u)
   record('paid_spawning_pool_cancel_'+mode,dict(original_paid_drone=True,original_build25_70_26=True,elapsed_build_callbacks={'middle':350,'late':1202}.get(mode,0),original_incoming_damage_raw=1920 if mode=='fractional_drone_damage' else None,finite_thingy_pool0=mode=='thingy_pool',complete_original_cancel41a340=True,no_timer_signal_hp_refund_type_or_ai_pointer_writes=True),[r(u,drone+0x64,2),after['completed'],after['hp'],r(u,0x5263d0+12),r(u,0x526400+12),after['position']['order'],after_counters['live']['41'],after_counters['live']['142'],after_counters['completed']['41'],after_counters['completed']['142'],after_counters['unit_count']==before_counters['unit_count']],[41,True,saved_drone_hp<<8,900,1000,150,1,0,1,0,True],joined(executions),dict(production=production,travel=travel,before=before,after=after,before_counters=before_counters,after_counters=after_counters,saved_integer_drone_hp=saved_drone_hp,final=snapshot(u)))
 if a.case_group in ('all','failure'):
  for stock_initial in (49,50,249,250):
   u=new();executions=zerg_world(u);w(u,0x5263d0+12,stock_initial)
   if stock_initial<50:
    created=call(u,0x424900,131,400,(400,3));executions.extend((created,call(u,0x424da0,UNIT)));w(u,0x529424+12,400);w(u,0x529484+12,20);executions.append(call(u,0x43d0d0,UNIT));drone=UNIT+0x150;executions.append(call(u,0x41b4b0,drone,args=(41,)))
    record('paid_drone_admission_minerals49_rejected',dict(initial_minerals49=True,genuine_native_spawned_larva=True),[executions[-1]['return_eax'],r(u,0x5263d0+12),r(u,drone+0x98,2),r(u,drone+0x64,2)],[0,49,228,35],joined(executions),snapshot(u));continue
   drone,production=paid_drone(u,executions);admitted=call(u,0x41b4b0,drone,args=(142,));executions.append(admitted)
   record('paid_pool_queue_admission_stock_'+str(stock_initial-50),dict(initial_minerals=stock_initial,original_drone_debit50=True,queued_building142=True,no_direct_queue_or_resource_debit=True),[admitted['return_eax'],r(u,0x5263d0+12),r(u,drone+0x98,2),r(u,drone+0x64,2)],[int(stock_initial>=250),stock_initial-50,142 if stock_initial>=250 else 228,41],joined(executions),dict(production=production,final=snapshot(u)))
 if a.case_group in ('all','failure'):
  u=new();executions=zerg_world(u);w(u,0x5263d0+12,250);drone,production=paid_drone(u,executions)
  executions.append(call(u,0x41b4b0,drone,args=(142,)));assert executions[-1]['return_eax']==1 and r(u,0x5263d0+12)==200
  waiting=[];other_larva=UNIT+0x2a0
  for tick in range(1000):
   object_step(u,executions);waiting.append(dict(tick=tick+1,unit_count=r(u,0x665b38),hatchery_spawn_counter=r(u,UNIT+0xca,1)))
   if r(u,other_larva+0xc) and r(u,other_larva+0x64,2)==35:break
  assert r(u,other_larva+0x64,2)==35 and r(u,other_larva+0x80)==UNIT
  executions.append(call(u,0x41b4b0,other_larva,args=(41,)));assert executions[-1]['return_eax']==1 and r(u,0x5263d0+12)==150
  executions.append(call(u,0x42a2c0,other_larva,args=(42,)));executions.append(call(u,0x42a340,drone,args=(25,832,528)));travel=[]
  for tick in range(300):
   object_step(u,executions);travel.append(dict(tick=tick+1,unit=unit_state(u,drone),resources=[r(u,0x5263d0+12),r(u,0x526400+12)]))
   if travel[-1]['unit']['position']['order']==150:break
  checks=[r(u,drone+0x64,2),bool(r(u,drone+0xdc)&1),r(u,drone+0x4d,1),r(u,0x5263d0+12),r(u,0x52c0c4+(142*12+3)*4),any(c['callee']=='0x4086b0' for ex in executions for c in ex['calls']),any(c['callee']=='0x43c000' for ex in executions for c in ex['calls'])]
  record('admitted_pool_later_payment_rejects_after_real_second_drone_debit',dict(initial_minerals250=True,paid_first_drone50=True,admitted_pool_at200_without_debit=True,second_larva_naturally_spawned_in_full_object_callbacks=True,second_paid_queue_debits50_before_build_travel=True,real_DroneStartBuild25=True,no_host_resource_mutation_after_admission=True),checks,[41,True,150,150,0,True,False],joined(executions),dict(production=production,waiting_for_original_larva=waiting,second_larva=other_larva,travel=travel,final=snapshot(u)))
 if a.case_group in ('all','build'):
  for finite_pool in (False,True):
   u=new(unit_count=2 if finite_pool else 16);executions=zerg_world(u);drone,production=paid_drone(u,executions)
   admitted=call(u,0x41b4b0,drone,args=(142,));executions.append(admitted);assert admitted['return_eax']==1
   admission_state=unit_state(u,drone);resources_at_admission=[r(u,0x5263d0+12),r(u,0x526400+12)]
   executions.append(call(u,0x42a340,drone,args=(25,832,528)));timeline=[]
   for tick in range(2000):
    object_step(u,executions);timeline.append(dict(tick=tick+1,type=r(u,drone+0x64,2),unit=unit_state(u,drone),resources=[r(u,0x5263d0+12),r(u,0x526400+12)]))
    if r(u,drone+0x64,2)==142 and r(u,drone+0xdc)&1:break
   assert r(u,drone+0x64,2)==142 and r(u,drone+0xdc)&1,timeline[-1]
   record('paid_drone_to_spawning_pool142_natural_completion'+('_finite_CUnit_pool2' if finite_pool else ''),dict(paid_drone_from_original_larva_producer=True,original_build_queue_admission=True,original_DroneStartBuild25=True,no_unit_type_timer_iscript_signal_or_position_fixture=True,finite_CUnit_pool2=finite_pool),[resources_at_admission,r(u,0x5263d0+12),r(u,0x526400+12),r(u,drone+0x64,2),bool(r(u,drone+0xdc)&1),r(u,drone+8)],[[950,1000],750,1000,142,True,750*256],joined(executions),dict(production=production,admission=admission_state,timeline=timeline,final=snapshot(u)))
   if finite_pool:assert r(u,0x665af0)==0 and r(u,0x665b38)==2
   assert r(u,0x529454+12)==0
 if a.case_group in ('all','ai'):
  for cancelled in (False,True):
   u=new();executions=zerg_world(u);drone,production=paid_drone(u,executions);executions.append(call(u,0x42a340,drone,args=(6,832,528)))
   for tick in range(300):
    object_step(u,executions)
    if r(u,drone+0x4d,1) in (2,3) and position(u,drone)['pixel']==[832,528]:break
   assert position(u,drone)['pixel']==[832,528]
   executions.extend((call(u,0x486a20,0),call(u,0x482ee0,3),call(u,0x487030,3,832,(528,0))))
   town=executions[-1]['return_eax'];worker_ai=r(u,drone+0x134);assert town and worker_ai and r(u,worker_ai+8,1)==2 and r(u,worker_ai+0x14)==town
   workers_before=r(u,town+0x1a,1);w(u,0x6726d0+3*36,1,1)
   executions.append(call(u,0x41b4b0,drone,args=(142,)));assert executions[-1]['return_eax']==1
   executions.append(call(u,0x42a340,drone,args=(26,832,528)));w(u,0x543300,drone);w(u,0x543304,drone);w(u,0x543308,0);executions.append(call(u,0x43b900,drone))
   building_ai=r(u,drone+0x134);assert r(u,drone+0x64,2)==142 and building_ai and r(u,building_ai+8,1)==3 and r(u,building_ai+0x14)==town
   workers_during=r(u,town+0x1a,1);timeline=[]
   if cancelled:
    executions.append(call(u,0x41a340,drone));after_ai=r(u,drone+0x134)
    got=[r(u,drone+0x64,2),bool(r(u,drone+0xdc)&1),r(u,after_ai+8,1),r(u,after_ai+0x14),workers_before,workers_during,r(u,town+0x1a,1),r(u,0x5263d0+12)];expected=[41,True,2,town,1,0,1,900]
   else:
    for tick in range(1300):
     w(u,0x52651c,r(u,0x52651c)+1);w(u,0x543300,drone);w(u,0x543304,drone);w(u,0x543308,0);executions.extend((call(u,0x43a5f0,drone),call(u,0x43a320,drone),call(u,0x439420,drone),call(u,0x439310,drone),call(u,0x418b20,drone),call(u,0x418990,0)))
     timeline.append(dict(tick=tick+1,unit=unit_state(u,drone),ai=r(u,drone+0x134),workers=r(u,town+0x1a,1)))
     if r(u,drone+0xdc)&1:break
    after_ai=r(u,drone+0x134);got=[r(u,drone+0x64,2),bool(r(u,drone+0xdc)&1),r(u,after_ai+8,1),r(u,after_ai+0x14),workers_before,workers_during,r(u,town+0x1a,1),r(u,0x5263d0+12)];expected=[142,True,3,town,1,0,0,750]
   record('native_town_paid_drone_pool_'+('cancel' if cancelled else 'complete_selected_unit_services'),dict(paid_human_drone_producer=True,actual_full_object_Move6_before_ai_assignment=True,actual486a20_487030_town_producer=True,explicit_controller1_input_after_town_assignment=True,selected_same_position_DroneBuild26_consumer=True,whole_object_computer_town_scheduler_executed=False,selected_original_unit_phases=[] if cancelled else ['0x43a5f0','0x43a320','0x439420','0x439310','0x418b20','0x418990'],unit_timer_stage_signal_ai_pointer_and_payment_writes=False),got,expected,joined(executions),dict(production=production,worker_ai=worker_ai,building_ai=building_ai,after_ai=after_ai,town=town,timeline=timeline,final=snapshot(u)))
 modules={Path(module.__file__).name:hashlib.sha256(Path(module.__file__).read_bytes()).hexdigest() for name,module in sorted(sys.modules.items()) if getattr(module,'__file__',None) and Path(module.__file__).parent.resolve()==Path(__file__).parent.resolve() and name!='__main__'}
 result=dict(target_sha256=local.TARGET_HASH,probe_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),source_dependencies_sha256=modules,engine=dict(unicorn=unicorn.__version__,pefile=pefile.__version__,machine=platform.machine(),python=platform.python_version()),dat_sha256=dat_hashes,asset_manifest_sha256=hashlib.sha256(a.asset_manifest.read_bytes()).hexdigest(),string_asset_manifest_sha256=hashlib.sha256(a.string_manifest.read_bytes()).hexdigest(),network_tbl_sha256=hashlib.sha256(network).hexdigest(),case_group=a.case_group,helper_stubs=[],host_imports_executed=False,outer_accepted_frame_executed=False,instruction_bound=1000000,original_code_executed=True,complete_game_executed=False,complete_original_function_calls=len(executed_stats),visited_original_function_starts=len({v for c in cases for v in c['execution']['visited_functions']}),total_original_instructions_executed=sum(x['instructions'] for x in executed_stats),maximum_single_call_instructions=max(x['instructions'] for x in executed_stats),explicit_world_inputs=dict(map_pixels=[4096,4096],map_tiles=[128,128],map_rectangle=[0,0,4096,4096],tile_flags='0x5041f700',current_and_known_creep_masks_fixture=True,clear_region_group=1,obstacle_contours=0,creep_record_dimensions=[128,128],creep_record_words=0,original_creep_life_pool_records=1024,owner=3,controller=2,default_initial_resource_balances=[1000,1000],initial_supply_cap400_and_total20=True,initial_ai_region_arrays_zero=True,initial_auxiliary_countdowns=1000,elapsed_game_time5349d8=0,caller_object_step_counter_incremented=True,unit_buildtime_signal_stage_and_hp_advanced_by_original_code=True),reviewed_code_ranges=[dict(start=hex(lo),end_exclusive=hex(hi)) for lo,hi in sorted(set(ALLOW))],complete_cases=len(cases),prefix_cases=0,all_passed=all(x['passed'] for x in cases),cases=cases)
 a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(dict(cases=len(cases),all_passed=result['all_passed'])))
if __name__=='__main__':main()
