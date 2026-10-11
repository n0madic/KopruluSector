#!/usr/bin/env python3
"""Probe paid Nexus/Probe production and original Protoss construction/warp.

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
import inspect_native_protoss_construction_117 as local
import probe_native_zerg_construction_117 as zerg_source
import probe_native_nydus_117 as nydus_source
import probe_native_towns_117 as town_source
import probe_native_ai_construction_117 as ai_construction_source
import inspect_economy_frame_117 as frame_source
import inspect_native_primary_orders_117 as orders
import inspect_native_training_117 as admission
UNIT=0x5DA3A0;SPRITE=0x803000;IMAGE=0x805000;STACK=0x900000;STOP=0xA00000;SCRIPT=0xD00000;ASSETS=0xE00000
ALLOW=sorted(set(list(zerg_source.ALLOW)+list(local.BLOCKS.values())+list(admission.WITNESSES.values())+[frame_source.BLOCKS[k] for k in ('player_ai_round_robin','town_round_robin')]))
PLAN=dict(target_sha256=local.TARGET_HASH,helper_stubs=[],instruction_bound=1000000,scope='Paid Nexus Probe production, original ProbeBuild31, stock Pylon/Gateway warp48 and maintained power lifecycle',limits='Native paid Probe and autonomous Pylon construction with stock populated graphics. Full selected object callbacks or explicitly listed per-unit phases; clear zero-creep terrain and finite private pools are inputs. No outer accepted frame, renderer, host imports or helper substitutions.')

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--exe',type=Path,required=True);p.add_argument('--data',type=Path,required=True);p.add_argument('--manifest',type=Path,required=True);p.add_argument('--assets',type=Path,required=True);p.add_argument('--asset-manifest',type=Path,required=True);p.add_argument('--string-manifest',type=Path,required=True);p.add_argument('--pyms',type=Path,required=True);p.add_argument('--out',type=Path);p.add_argument('--case-group',choices=('all','build','damage','cancel','failure','ai','power'),default='all');p.add_argument('--execute-original-code',action='store_true');a=p.parse_args()
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
 power_path=Path(__file__).resolve().parents[2]/'docs/research/evidence/native-placement-probes.json';power_evidence=json.loads(power_path.read_text());assert power_evidence['target_sha256']==local.TARGET_HASH;power_profile=power_evidence['original_power_asset_profile']
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
  result.update(resources=[r(u,0x5263d0+12),r(u,0x526400+12)],protoss_counters=protoss_counters(u),owner_chain=chain(u,0x665b08+12,next_offset=0x6c))
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
 def power_mask_fixture(u,executions):
  row,grp=assets[584];assert hashlib.sha256(grp).hexdigest()==power_profile['grp_sha256']=='cd98ce60f5afb8167896cc099f0e9650edf2dc03bcc8c14fda90a667bff3ae08'
  grp_at=0xc72000;u.mem_write(grp_at,grp);frame=grp_at+6;_,_,width,height,data_offset=struct.unpack_from('<4BI',grp,6);w(u,frame+4,grp_at+data_offset)
  half_width=(width+15)//32;half_height=(height+15)//32;mask=[0]*(half_width*half_height*4)
  for y in range(half_height):
   for x in range(half_width):
    ex=call(u,0x40fd50,frame,16+x*32,(16+y*32,));executions.append(ex);v=ex['return_eax'];assert v in(0,1)
    for yy in(y,half_height*2-y-1):
     for xx in(half_width+x,half_width-x-1):mask[yy*half_width*2+xx]=v
  assert mask==[v for row in power_profile['mask']for v in row]
  u.mem_write(0xc71000,bytes(mask));w(u,0x665b50,0xc71000);w(u,0x665b58,half_width*32);w(u,0x665b5c,half_height*32)
 def protoss_world(u,minerals=1000,gas=1000):
  executions=[clear_world(u),call(u,0x40a0e0,0),call(u,0x4b44a0,0)]
  w(u,0x6ca7a0,0xc30000);w(u,0x6ca7ac,128);w(u,0x6ca7b0,128)
  u.mem_write(0xc00000,struct.pack('<I',0x1f700)*16384)
  w(u,0x534914+3*12+3,1,1);w(u,0x534914+4*12+4,1,1);w(u,0x6726d0+4*36,2,1)
  w(u,0x5263d0+12,minerals);w(u,0x526400+12,gas)
  gate_auxiliary_services(u);power_mask_fixture(u,executions)
  return executions
 def unit_state(u,unit):
  sp=r(u,unit+0xc)
  return dict(position=position(u,unit),special_pointer_d0=r(u,unit+0xd0),hp=r(u,unit+8),shield=r(u,unit+0x60),ai=r(u,unit+0x134),build_shield_gain=r(u,unit+0xaa,2),build_time=r(u,unit+0xac,2),build_hp_gain=r(u,unit+0xa8,2),buttons=r(u,unit+0x94,2),order_signal=r(u,unit+0x4f,1),completed=bool(r(u,unit+0xdc)&1),secondary=r(u,unit+0xa6,1),images=[] if not sp else [dict(address=im,id=r(u,im+8,2),animation=r(u,im+0x16,1),draw_method=r(u,im+0xa,1),draw_parameter=r(u,im+0x30,1),draw_timer=r(u,im+0x31,1),pc=r(u,im+0x12,2),wait=r(u,im+0x17,1)) for im in chain(u,sp+0x1c)])
 def protoss_counters(u):
  return dict(live={str(t):r(u,0x529604+(t*12+3)*4)for t in (64,154,156,160)},completed={str(t):r(u,0x52c0c4+(t*12+3)*4)for t in (64,154,156,160)},supply=dict(provided=r(u,0x529544+12),used=r(u,0x529574+12),global_cap=r(u,0x5295a4+12)),active=chain(u,0x665ae0),hidden=chain(u,0x665af8),unit_count=r(u,0x665b38))
 def nexus(u,executions):
  w(u,0x5295a4+12,400);created=call(u,0x424900,154,400,(400,3));assert created['return_eax']==UNIT;executions.extend((created,call(u,0x424da0,UNIT)));assert r(u,0x529544+12)==18
 def paid_probe(u,executions):
  nexus(u,executions)
  executions.append(call(u,0x41b4b0,UNIT,args=(64,)));assert executions[-1]['return_eax']==1
  executions.append(call(u,0x42a1e0,UNIT,args=(38,)));probe=UNIT+0x150;timeline=[]
  for tick in range(500):
   object_step(u,executions);timeline.append(dict(tick=tick+1,probe=unit_state(u,probe)if r(u,probe+0xc)else None,parent=unit_state(u,UNIT),resources=[r(u,0x5263d0+12),r(u,0x526400+12)]))
   if r(u,probe+0xc)and r(u,probe+0x64,2)==64 and r(u,probe+0xdc)&1:break
  assert r(u,probe+0x64,2)==64 and r(u,probe+0xdc)&1,timeline[-1]
  return probe,timeline
 def context(u,unit):
  w(u,0x543300,unit);w(u,0x543304,unit);w(u,0x543308,0)
 def build_admission(u,executions,probe):
  admitted=call(u,0x41b4b0,probe,args=(156,));executions.append(admitted);assert admitted['return_eax']==1
  at=[r(u,0x5263d0+12),r(u,0x526400+12)]
  executions.append(call(u,0x42a340,probe,args=(31,832,528)))
  return at
 def build_rows(u,executions,probe,building,limit=1500,mode='complete'):
  rows=[];event=None
  for tick in range(limit):
   object_step(u,executions)
   rows.append(dict(tick=tick+1,probe=unit_state(u,probe)if r(u,probe+0xc)else None,building=unit_state(u,building)if r(u,building+0xc)else None,resources=[r(u,0x5263d0+12),r(u,0x526400+12)],psi_head=r(u,0x665b54),psi_dirty=r(u,0x665b60)))
   if mode in ('move','death')and event is None and r(u,building+0xc):
    context(u,probe);executions.append(call(u,0x42a340,probe,args=(6,1000,720))if mode=='move'else call(u,0x42a4d0,probe));event=dict(callback=tick+1,probe_before=rows[-1]['probe'],issuer='0x42a340 Move6'if mode=='move'else'0x42a4d0 Death')
   if mode=='late_damage'and event is None and r(u,building+0xc)and r(u,building+0x4e,1)==3:
    before=unit_state(u,building);context(u,building);executions.append(call(u,0x41dfc0,building,args=(400*256,0,1,64,0,4)));event=dict(callback=tick+1,building_before=before,damage_arguments=[400*256,0,1,64,0,4],building_after=unit_state(u,building))
   if r(u,building+0xc)and r(u,building+0x64,2)==156 and r(u,building+0xdc)&1 and r(u,building+0xd0):break
  assert r(u,building+0x64,2)==156 and r(u,building+0xdc)&1 and r(u,building+0xd0),rows[-1]
  return rows,event
 if a.case_group in ('all','build','damage'):
  for mode in ('complete','move','death','late_damage'):
   if a.case_group=='damage'and mode!='late_damage':continue
   u=new();executions=protoss_world(u);probe,production=paid_probe(u,executions);at=build_admission(u,executions,probe);building=UNIT+0x2a0;timeline,event=build_rows(u,executions,probe,building,mode=mode)
   assert r(u,0x665b54)==building and r(u,building+0xf8)==r(u,building+0xfc)==0
   got=[at,r(u,0x5263d0+12),r(u,0x526400+12),r(u,building+0x64,2),bool(r(u,building+0xdc)&1),r(u,building+8),r(u,building+0x60),r(u,0x529544+12),r(u,0x529574+12),r(u,0x5295a4+12),r(u,0x665b38)]
   expected=[[950,1000],850,1000,156,True,200*256+154 if mode=='late_damage'else 300*256,49*7+153 if mode=='late_damage'else 300*256,34,0 if mode=='death'else 2,400,2 if mode=='death'else 3]
   if mode=='death':assert probe in chain(u,0x665af0)and r(u,probe+0xc)==0
   if mode=='move':assert position(u,probe)['pixel']==[1000,720]
   record('paid_probe_to_pylon156_'+mode,dict(original_paid_Nexus_Train38=True,original_paid_probe_queue50=True,original_Pylon_build31=True,no_unit_position_hp_timer_stage_or_signal_writes=True,selected_interruption_or_damage=event,direct_damage_entry_without_shot_producer=mode=='late_damage'),got,expected,joined(executions),dict(production=production,timeline=timeline,final=snapshot(u)))
   print(json.dumps(dict(phase='Protoss construction',mode=mode,callbacks=len(timeline),cases=len(cases))),flush=True)
 if a.case_group in ('all','cancel'):
  for mode in ('created','timer_zero','warp_stage3'):
   u=new();executions=protoss_world(u);probe,production=paid_probe(u,executions);at=build_admission(u,executions,probe);building=UNIT+0x2a0;timeline=[];cancel=None
   for tick in range(1600):
    object_step(u,executions);timeline.append(dict(tick=tick+1,probe=unit_state(u,probe)if r(u,probe+0xc)else None,building=unit_state(u,building)if r(u,building+0xc)else None,resources=[r(u,0x5263d0+12),r(u,0x526400+12)]))
    if cancel is None and r(u,building+0xc)and(mode=='created'or mode=='timer_zero'and r(u,building+0xac,2)==0 or mode=='warp_stage3'and r(u,building+0x4e,1)==3):
     before=unit_state(u,building);context(u,building);executions.append(call(u,0x41a340,building));cancel=dict(callback=tick+1,before=before,after=unit_state(u,building),after_minerals=r(u,0x5263d0+12));assert r(u,0x5263d0+12)==925 and r(u,building+0x4d,1)==0
    if cancel and building in chain(u,0x665af0)and r(u,building+0xc)==0:break
   assert cancel and building in chain(u,0x665af0)and not r(u,0x665ae8)
   record('paid_pylon_cancel_'+mode,dict(paid_Probe_Pylon_producer=True,cancellation=cancel,no_direct_refund_pool_signal_or_progress_writes=True),[r(u,0x5263d0+12),r(u,0x526400+12),r(u,0x665b38),r(u,0x529544+12),r(u,0x529574+12),r(u,0x5295a4+12),r(u,0x665b54)],[925,1000,2,18,2,400,0],joined(executions),dict(production=production,timeline=timeline,final=snapshot(u)))
   print(json.dumps(dict(phase='Protoss cancellation',mode=mode,callbacks=len(timeline),cases=len(cases))),flush=True)
 if a.case_group in ('all','failure'):
  for balance in (49,50):
   u=new();executions=protoss_world(u,minerals=balance);nexus(u,executions);ex=call(u,0x41b4b0,UNIT,args=(64,));executions.append(ex)
   record('paid_probe_queue_balance_'+str(balance),dict(minerals_before_admission=balance,actual_Nexus=True,stock_Probe_cost50=True),[ex['return_eax'],r(u,0x5263d0+12),r(u,UNIT+0x98,2)],[int(balance==50),balance-50 if balance==50 else balance,64 if balance==50 else 228],joined(executions),snapshot(u))
  for balance in (99,100):
   u=new();executions=protoss_world(u,minerals=balance+50);probe,production=paid_probe(u,executions);ex=call(u,0x41b4b0,probe,args=(156,));executions.append(ex)
   record('paid_probe_pylon_queue_balance_'+str(balance),dict(real_paid_Probe=True,mineral_balance_after_Probe=balance,stock_Pylon_cost100=True),[ex['return_eax'],r(u,0x5263d0+12),r(u,probe+0x98,2)],[int(balance==100),balance,156 if balance==100 else 228],joined(executions),dict(production=production,final=snapshot(u)))
  for pool in ('units','sprites'):
   u=new(unit_count=2 if pool=='units'else 16,sprite_count=2 if pool=='sprites'else 64);executions=protoss_world(u);probe,production=paid_probe(u,executions);at=build_admission(u,executions,probe);timeline=[]
   for tick in range(200):
    object_step(u,executions);timeline.append(dict(tick=tick+1,probe=unit_state(u,probe),resources=[r(u,0x5263d0+12),r(u,0x526400+12)],error=r(u,0x5da398,2)))
    if tick>1 and r(u,probe+0x4d,1)in(2,3):break
   assert not any(r(u,UNIT+i*0x150+0xc)and r(u,UNIT+i*0x150+0x64,2)==156 for i in range(16))
   record('pylon_after_payment_'+pool+'_pool_failure',dict(real_paid_Probe=True,declared_unit_pool=2 if pool=='units'else 16,declared_sprite_pool=2 if pool=='sprites'else 64,building_resources_admitted_then_debited_before_allocation=True,command_cap_admission_not_executed=True),[at,r(u,0x5263d0+12),r(u,0x526400+12),r(u,0x665b38),r(u,0x529544+12),r(u,0x529574+12)],[ [950,1000],850,1000,2,18,2],joined(executions),dict(production=production,timeline=timeline,final=snapshot(u)))
  u=new();executions=protoss_world(u,minerals=150);probe,production=paid_probe(u,executions);at=build_admission(u,executions,probe)
  executions.append(call(u,0x41b4b0,UNIT,args=(64,)));assert executions[-1]['return_eax']==1;executions.append(call(u,0x42a1e0,UNIT,args=(38,)));timeline=[]
  for tick in range(200):
   object_step(u,executions);timeline.append(dict(tick=tick+1,probe=unit_state(u,probe),resources=[r(u,0x5263d0+12),r(u,0x526400+12)]))
   if tick>1 and r(u,probe+0x4d,1)in(2,3):break
  assert not any(r(u,UNIT+i*0x150+0xc)and r(u,UNIT+i*0x150+0x64,2)==156 for i in range(16))
  record('pylon_late_resource_recheck_after_real_second_Probe_debit',dict(initial_minerals150=True,first_Probe50_then_Pylon100_admitted=True,actual_Nexus_second_Probe_queue_debits50=True,no_resource_changes_after_commands=True),[at,r(u,0x5263d0+12),r(u,0x526400+12),r(u,probe+0x98,2)],[ [100,1000],50,1000,156],joined(executions),dict(production=production,timeline=timeline,final=snapshot(u)))
 if a.case_group in ('all','ai'):
  for cancelled in(False,True):
   u=new();executions=protoss_world(u);probe,production=paid_probe(u,executions);executions.append(call(u,0x42a340,probe,args=(6,832,528)))
   for tick in range(300):
    object_step(u,executions)
    if r(u,probe+0x4d,1)in(2,3)and position(u,probe)['pixel']==[832,528]:break
   assert position(u,probe)['pixel']==[832,528]
   rng_gate=call(u,0x4781c0,1);executions.append(rng_gate)
   executions.extend((call(u,0x486a20,0),call(u,0x482ee0,3),call(u,0x487030,3,832,(528,0))));town=executions[-1]['return_eax'];worker_ai=r(u,probe+0x134)
   assert town and worker_ai and r(u,worker_ai+8,1)==2 and r(u,worker_ai+0x14)==town
   w(u,0x6726d0+3*36,1,1);workers_before=r(u,town+0x1a,1);executions.append(call(u,0x41b4b0,probe,args=(156,)));assert executions[-1]['return_eax']==1;executions.append(call(u,0x42a340,probe,args=(31,832,528)));building=UNIT+0x2a0;startup=[]
   for tick in range(32):
    w(u,0x52651c,r(u,0x52651c)+1);context(u,probe);executions.extend((call(u,0x43a5f0,probe),call(u,0x43a320,probe),call(u,0x439420,probe),call(u,0x439310,probe),call(u,0x418b20,probe),call(u,0x418990,0)))
    startup.append(dict(tick=tick+1,probe=unit_state(u,probe),building=unit_state(u,building)if r(u,building+0xc)else None))
    if r(u,building+0xc):break
   assert r(u,building+0xc)and r(u,building+0x64,2)==156 and r(u,building+0x4d,1)==48,startup[-1]
   building_ai=r(u,building+0x134);assert building_ai and r(u,building_ai+8,1)==3 and r(u,building_ai+0x14)==town and r(u,probe+0x134)==worker_ai
   timeline=[]
   if cancelled:
    context(u,building);executions.append(call(u,0x41a340,building))
   for tick in range(650):
    w(u,0x52651c,r(u,0x52651c)+1);context(u,building)
    if cancelled and building in chain(u,0x665ae8):
     assert not r(u,building+0xdc)&0x4000;executions.append(call(u,0x418b20,building))
     if not r(u,building+0xc):executions.append(call(u,0x4256b0,building))
     executions.append(call(u,0x418990,0))
    else:
     assert building in chain(u,0x665ae0);executions.extend((call(u,0x43a5f0,building),call(u,0x43a320,building),call(u,0x439420,building),call(u,0x439310,building),call(u,0x418b20,building),call(u,0x418990,0)))
    timeline.append(dict(tick=tick+1,building=unit_state(u,building)if r(u,building+0xc)else None,worker_ai=r(u,probe+0x134),building_ai=r(u,building+0x134),workers=r(u,town+0x1a,1)))
    if cancelled and not r(u,building+0xc)and building in chain(u,0x665af0):break
    if not cancelled and r(u,building+0xdc)&1 and r(u,building+0xd0):break
   assert r(u,probe+0x134)==worker_ai and r(u,town+0x1a,1)==workers_before==1
   if cancelled:assert r(u,building+0x134)==0 and not r(u,building+0xc)
   else:assert r(u,building+0x134)==building_ai and r(u,building+0xdc)&1 and r(u,building+0xd0)
   executions.append(call(u,0x4781c0,rng_gate['return_eax']))
   record('native_town_paid_probe_pylon_'+('cancel_selected_services'if cancelled else'complete_selected_services'),dict(real_paid_human_Probe=True,real_full_object_Move6_before_actual_Town487030_worker_assignment=True,explicit_controller1_after_Town=True,selected_original_Probe_and_building_phases=['0x43a5f0','0x43a320','0x439420','0x439310','0x418b20','0x418990'],whole_computer_town_scheduler_executed=False,actual_native_rng_gate4781c0_enabled_during_selected_services=True,selected_pending_phases_after_actual_removal=['0x418b20','0x4256b0','0x418990']if cancelled else[],no_direct_stage_signal_timer_ai_pointer_or_payment_writes=True),[r(u,0x5263d0+12),r(u,town+0x1a,1),r(u,worker_ai+8,1),r(u,building+0x134)!=0],[925 if cancelled else 850,1,2,not cancelled],joined(executions),dict(production=production,town=town,worker_ai=worker_ai,building_ai=building_ai,startup=startup,timeline=timeline,final=snapshot(u)))
   print(json.dumps(dict(phase='Protoss Town assignment',cancelled=cancelled,callbacks=len(timeline),cases=len(cases))),flush=True)
 if a.case_group in ('all','power'):
  u=new();executions=protoss_world(u);probe,production=paid_probe(u,executions);at=build_admission(u,executions,probe);pylon=UNIT+0x2a0;first,event=build_rows(u,executions,probe,pylon)
  executions.append(call(u,0x41b4b0,probe,args=(160,)));assert executions[-1]['return_eax']==1;executions.append(call(u,0x42a340,probe,args=(31,960,528)));gateway=UNIT+0x3f0;timeline=[]
  for tick in range(1400):
   object_step(u,executions);timeline.append(dict(tick=tick+1,probe=unit_state(u,probe),gateway=unit_state(u,gateway)if r(u,gateway+0xc)else None,psi_head=r(u,0x665b54),psi_dirty=r(u,0x665b60)))
   if r(u,gateway+0xc)and r(u,gateway+0xdc)&1:break
  assert r(u,gateway+0x64,2)==160 and r(u,gateway+0xdc)&1 and not r(u,gateway+0xdc)&0x400,timeline[-1]
  before=unit_state(u,gateway);context(u,pylon);executions.append(call(u,0x42a4d0,pylon));loss=[]
  for tick in range(100):
   object_step(u,executions);loss.append(dict(tick=tick+1,pylon=unit_state(u,pylon)if r(u,pylon+0xc)else None,gateway=unit_state(u,gateway),psi_head=r(u,0x665b54),psi_dirty=r(u,0x665b60),provided=r(u,0x529544+12)))
   if not r(u,pylon+0xc)and pylon in chain(u,0x665af0)and r(u,gateway+0xdc)&0x400:break
  assert not r(u,pylon+0xc)and r(u,gateway+0xdc)&0x400 and r(u,0x665b54)==0,loss[-1]
  outage=unit_state(u,gateway);replacement_unit=r(u,0x665af0);second_at=build_admission(u,executions,probe);replacement,event=build_rows(u,executions,probe,replacement_unit)
  for tick in range(3):object_step(u,executions)
  assert replacement_unit!=pylon and r(u,0x665b54)==replacement_unit
  record('paid_pylon_gateway_native_power_loss_and_replacement',dict(real_paid_Probe_Pylon_and_Gateway=True,Gateway_point=[960,528],original_completed_Pylon_Death=True,original_paid_replacement_Pylon=True,stock_power_mask_explicit_resource_input_from_native_opacity=True,full_mask_malloc_initializer_unexecuted=True,no_status_power_provider_link_supply_or_position_patches=True),[at,second_at,r(u,0x5263d0+12),r(u,gateway+0x64,2),bool(r(u,gateway+0xdc)&1),bool(r(u,gateway+0xdc)&0x400),r(u,0x529544+12),r(u,0x529574+12),r(u,0x5295a4+12)],[ [950,1000],[700,1000],600,160,True,False,34,2,400],joined(executions),dict(production=production,first_pylon=first,gateway_warp=timeline,powered_before=before,power_loss=loss,unpowered=outage,original_removed_pylon=pylon,actual_replacement_allocation=replacement_unit,replacement=replacement,final=snapshot(u)))
 modules={Path(module.__file__).name:hashlib.sha256(Path(module.__file__).read_bytes()).hexdigest() for name,module in sorted(sys.modules.items()) if getattr(module,'__file__',None) and Path(module.__file__).parent.resolve()==Path(__file__).parent.resolve() and name!='__main__'}
 result=dict(target_sha256=local.TARGET_HASH,probe_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),source_dependencies_sha256=modules,engine=dict(unicorn=unicorn.__version__,pefile=pefile.__version__,machine=platform.machine(),python=platform.python_version()),dat_sha256=dat_hashes,asset_manifest_sha256=hashlib.sha256(a.asset_manifest.read_bytes()).hexdigest(),string_asset_manifest_sha256=hashlib.sha256(a.string_manifest.read_bytes()).hexdigest(),network_tbl_sha256=hashlib.sha256(network).hexdigest(),power_mask_prior_original_evidence_sha256=hashlib.sha256(power_path.read_bytes()).hexdigest(),power_mask_input_provenance=dict(grp_sha256=power_profile['grp_sha256'],source='40 complete original opacity calls; host mirrors explicit resource input; equals prior original post-allocation mask output',full_allocator_bootstrap_executed=False),case_group=a.case_group,helper_stubs=[],host_imports_executed=False,outer_accepted_frame_executed=False,instruction_bound=1000000,original_code_executed=True,complete_game_executed=False,complete_original_function_calls=len(executed_stats),visited_original_function_starts=len({v for c in cases for v in c['execution']['visited_functions']}),total_original_instructions_executed=sum(x['instructions'] for x in executed_stats),maximum_single_call_instructions=max(x['instructions'] for x in executed_stats),explicit_world_inputs=dict(map_pixels=[4096,4096],map_tiles=[128,128],map_rectangle=[0,0,4096,4096],tile_flags='0x1f700',zero_current_and_known_creep=True,clear_region_group=1,obstacle_contours=0,creep_record_dimensions=[128,128],creep_record_words=0,original_creep_life_pool_records=1024,owner=3,controller=2,default_initial_resource_balances=[1000,1000],Protoss_supply_triplet_addresses=['0x529544','0x529574','0x5295a4'],Protoss_global_supply_cap400_and_native_Nexus_provided18=True,stock_power_mask_pixels_half_extents=[256,160],stock_power_mask_grid=[16,10],stock_power_mask_bootstrap_malloc_unexecuted=True,initial_ai_region_arrays_zero=True,initial_auxiliary_countdowns=1000,elapsed_game_time5349d8=0,caller_object_step_counter_incremented=True,unit_buildtime_signal_stage_and_hp_advanced_by_original_code=True),reviewed_code_ranges=[dict(start=hex(lo),end_exclusive=hex(hi)) for lo,hi in sorted(set(ALLOW))],complete_cases=len(cases),prefix_cases=0,all_passed=all(x['passed'] for x in cases),cases=cases)
 a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(dict(cases=len(cases),all_passed=result['all_passed'])))
if __name__=='__main__':main()
