#!/usr/bin/env python3
"""Probe paid Assimilator157 conversion, warp and gas.

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
import inspect_native_assimilator_117 as local
import probe_native_protoss_construction_117 as protoss_source
import probe_native_gas_harvesting_117 as gas_source
import probe_native_mission_units_117 as mission_source
import probe_native_carried_construction_117 as carry_source
import inspect_native_powerup_goals_117 as goals_source
import probe_native_zerg_construction_117 as zerg_source
import probe_native_nydus_117 as nydus_source
import probe_native_towns_117 as town_source
import probe_native_ai_construction_117 as ai_construction_source
import inspect_economy_frame_117 as frame_source
import inspect_native_primary_orders_117 as orders
import inspect_native_training_117 as admission
UNIT=0x5DA3A0;SPRITE=0x803000;IMAGE=0x805000;STACK=0x900000;STOP=0xA00000;SCRIPT=0xD00000;ASSETS=0xE00000
ALLOW=sorted(set(list(protoss_source.ALLOW)+list(gas_source.ALLOW)+list(mission_source.ALLOW)+list(carry_source.ALLOW)+list(local.BLOCKS.values())+list(admission.WITNESSES.values())+[frame_source.BLOCKS[k] for k in ('player_ai_round_robin','town_round_robin')]))
PLAN=dict(target_sha256=local.TARGET_HASH,helper_stubs=[],instruction_bound=1000000,scope='Paid Probe Assimilator157 on authentic Geyser188, conversion/warp/cancel, physical gas and carry contracts',limits='Native paid Probe and original Assimilator conversion/warp with stock populated graphics. Complete selected object callbacks and direct original commands; clear zero-creep terrain and finite private pools are inputs. No outer accepted frame, renderer, host imports or helper substitutions.')

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--exe',type=Path,required=True);p.add_argument('--data',type=Path,required=True);p.add_argument('--manifest',type=Path,required=True);p.add_argument('--assets',type=Path,required=True);p.add_argument('--asset-manifest',type=Path,required=True);p.add_argument('--string-manifest',type=Path,required=True);p.add_argument('--pyms',type=Path,required=True);p.add_argument('--out',type=Path);p.add_argument('--case-group',choices=('all','build','cancel','failure','carry'),default='all');p.add_argument('--execute-original-code',action='store_true');a=p.parse_args()
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
    if addr in (0x4237a0,0x4235c0):
     un=entry['ecx'];target=r(m,un+0x5c);entry['gas_eligibility_input']=dict(object_step_counter=r(m,0x52651c),worker=un,carry_flags=r(m,un+0xe0,1),target=target,target_type=r(m,target+0x64,2)if target else None,target_status=r(m,target+0xdc)if target else None)
    if addr==0x41b3f0:entry['original_refinery_conversion_input']=dict(object_step_counter=r(m,0x52651c),worker=entry['ecx'],requested_type=entry['stack'][0],free_unit_head=r(m,0x665af0),free_sprite_head=r(m,0x5c51c8),resources=[r(m,0x5263d0+12),r(m,0x526400+12)])
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
  return dict(complete_return=all(x['complete_return'] for x in executions),complete_call_count=len(executions),return_eax=executions[-1]['return_eax'],instruction_count=sum(x['instruction_count'] for x in executions),call_entry_counts=dict(Counter(c['callee'] for x in executions for c in x['calls'])),calls=[c for x in executions for c in x['calls'] if int(c['callee'],16) in ({lo for lo,hi in local.BLOCKS.values()}|{0x4237a0,0x4235c0,0x4272c0,0x478200,0x478270,0x41b4b0,0x42a340,0x42a2c0,0x4086b0,0x408650,0x408490,0x41a340,0x484cd0,0x486340,0x487900,0x424900,0x424da0,0x424ad0})],iscript_opcodes=[c for x in executions for c in x['iscript_opcodes']],bullet_initializations=[c for x in executions for c in x['bullet_initializations']],impact_events=[c for x in executions for c in x['impact_events']],damage_events=[c for x in executions for c in x['damage_events']],visited_functions=sorted({c for x in executions for c in x['visited_functions']}))
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
  return dict(live={str(t):r(u,0x529604+(t*12+3)*4)for t in (64,154,157,188)},completed={str(t):r(u,0x52c0c4+(t*12+3)*4)for t in (64,154,157,188)},supply=dict(provided=r(u,0x529544+12),used=r(u,0x529574+12),global_cap=r(u,0x5295a4+12)),active=chain(u,0x665ae0),hidden=chain(u,0x665af8),unit_count=r(u,0x665b38))
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
 def sections(blob):
  at=0
  while at<len(blob):
   tag=blob[at:at+4];n=struct.unpack_from('<I',blob,at+4)[0];at+=8;assert at+n<=len(blob);yield tag,blob[at:at+n];at+=n
 def geyser_record():
  row=next(v for v in manifest['campaign_found'] if v['path'].lower()=='campaign\\zerg\\zerg02\\staredit\\scenario.chk');blob=(a.data/row['path'].replace('\\','/')).read_bytes();assert hashlib.sha256(blob).hexdigest()==row['sha256']
  for section,payload in sections(blob):
   if section!=b'UNIT':continue
   for i in range(len(payload)//36):
    raw=payload[i*36:(i+1)*36]
    if struct.unpack_from('<H',raw,8)[0]==188:return dict(map=row['path'],map_sha256=row['sha256'],record_index=i,record_sha256=hashlib.sha256(raw).hexdigest(),raw=raw,x=struct.unpack_from('<H',raw,4)[0],y=struct.unpack_from('<H',raw,6)[0],owner=raw[16],amount=struct.unpack_from('<I',raw,20)[0])
  raise AssertionError('No original Zerg02 geyser')
 g=geyser_record()
 def make_geyser(u,executions):
  ex=call(u,0x469460,188,g['x'],(g['y'],g['owner']));executions.append(ex);at=ex['return_eax'];assert at
  u.mem_write(0x80f200,g['raw']);executions.append(call(u,0x46a420,at,0x80f20c));assert r(u,at+0xd0,2)==(g['amount']&65535)
  return at
 def gas_state(u,at):
  state=unit_state(u,at);state.update(type=r(u,at+0x64,2),owner=r(u,at+0x4c,1),amount=r(u,at+0xd0,2),waiting_head=r(u,at+0xd4),union_d8_byte=r(u,at+0xd8,1));return state
 if a.case_group in ('all','build'):
  u=new();executions=protoss_world(u);probe,production=paid_probe(u,executions);geyser=make_geyser(u,executions);before=gas_state(u,geyser)
  executions.append(call(u,0x41b4b0,probe,args=(157,)));assert executions[-1]['return_eax']==1;admission_resources=[r(u,0x5263d0+12),r(u,0x526400+12)]
  executions.append(call(u,0x42a340,probe,args=(31,g['x'],g['y'])));timeline=[];last=None;converted=None;completed=None;deposited=None
  for tick in range(2400):
   object_step(u,executions);building=gas_state(u,geyser);pstate=unit_state(u,probe);key=(bool(r(u,r(u,probe+0xc)+0xe,1)&32),r(u,probe+0x54,1)if r(u,probe+0x4d,1)==83 else 0,building['type'],building['position']['order'],building['position']['order_state'],building['completed'],pstate['position']['order'],pstate['position']['order_state'],r(u,probe+0xe0,1),r(u,0x526400+12))
   if key!=last:timeline.append(dict(callback=tick+1,building=building,probe=pstate,resources=[r(u,0x5263d0+12),r(u,0x526400+12)],probe_cargo=r(u,probe+0xcf,1),probe_carry_flags=r(u,probe+0xe0,1),probe_hidden=bool(r(u,r(u,probe+0xc)+0xe,1)&32),native_spatial_boundaries=r(u,0x5c99b0),gas_timer=r(u,probe+0x54,1)));last=key
   if building['type']==157 and converted is None:converted=tick+1;print('CONVERTED',converted,flush=True)
   if converted and building['completed']and completed is None:
    completed=tick+1;print('COMPLETED',completed,flush=True);executions.append(call(u,0x42a380,probe,args=(79,geyser)))
   if completed and r(u,0x526400+12)>1000:deposited=tick+1;break
   if tick>64 and not converted and r(u,probe+0x4d,1)in(2,3):break
  assert converted is not None and completed is not None and deposited is not None,(timeline[-12:],snapshot(u))
  hidden_rows=[row for row in timeline if row.get('probe_hidden')];assert len(hidden_rows)==36 and [row['gas_timer']for row in hidden_rows]==list(range(36,0,-1)),hidden_rows
  gas_entries=[c for x in executions for c in x['calls'] if c['callee']=='0x4237a0'];assert any(c['gas_eligibility_input']['target_type']==157 and not(c['gas_eligibility_input']['target_status']&1)for c in gas_entries)
  actual=[admission_resources,r(u,0x5263d0+12),r(u,0x526400+12),r(u,geyser+0x64,2),r(u,geyser+0x4c,1),r(u,geyser+0xd0,2),r(u,0x665b38),r(u,probe+0xcf,1),r(u,probe+0xe0,1),r(u,0x526430+12)]
  expected=[[950,1000],850,1008,157,3,g['amount']-8,3,0,0,8]
  record('paid_probe_assimilator_conversion_warp_and_natural_gas_deposit',dict(authentic_geyser_record={k:v for k,v in g.items()if k!='raw'},actual_paid_Probe=True,real_Build31=True,converted_callback=converted,completed_callback=completed,actual_Harvest79_issued_after_completion=True,gas_deposit_callback=deposited,no_post_creation_type_amount_signal_position_queue_or_payment_patch=True),actual,expected,joined(executions),dict(production=production,geyser_before=before,timeline=timeline,final=snapshot(u)))
  print(json.dumps(dict(phase='Assimilator',callbacks=tick+1,cases=len(cases))),flush=True)
 if a.case_group in ('all','cancel'):
  for mode in ('created','timer_zero','warp_stage3','completed_death'):
   u=new();executions=protoss_world(u);probe,production=paid_probe(u,executions);geyser=make_geyser(u,executions);before=gas_state(u,geyser)
   executions.append(call(u,0x41b4b0,probe,args=(157,)));assert executions[-1]['return_eax']==1;executions.append(call(u,0x42a340,probe,args=(31,g['x'],g['y'])));timeline=[];last=None;event=None;converted=None;restored=None
   for tick in range(1400):
    object_step(u,executions);b=gas_state(u,geyser);pstate=unit_state(u,probe);key=(b['type'],b['position']['order'],b['position']['order_state'],b['completed'],pstate['position']['order'],r(u,0x5263d0+12))
    if key!=last:timeline.append(dict(callback=tick+1,building=b,probe=pstate,resources=[r(u,0x5263d0+12),r(u,0x526400+12)]));last=key
    if b['type']==157 and converted is None:converted=tick+1
    due=converted is not None and(event is None)and(mode=='created'or mode=='timer_zero'and b['build_time']==0 or mode=='warp_stage3'and b['position']['order_state']==3 or mode=='completed_death'and b['completed'])
    if due:
     context(u,geyser);executions.append(call(u,0x42a4d0 if mode=='completed_death'else 0x41a340,geyser));event=dict(callback=tick+1,before=b,after=gas_state(u,geyser),issuer='0x42a4d0'if mode=='completed_death'else'0x41a340')
    if event and r(u,geyser+0x64,2)==188 and r(u,geyser+0xdc)&1 and r(u,geyser+0x4c,1)==11:restored=tick+1;break
   assert event and restored and r(u,geyser+0xc),(timeline[-10:],snapshot(u))
   actual=[r(u,0x5263d0+12),r(u,0x526400+12),r(u,geyser+0x64,2),r(u,geyser+0x4c,1),r(u,geyser+0xd0,2),r(u,geyser+8),r(u,0x665b38),r(u,0x529544+12),r(u,0x529574+12)]
   record('paid_assimilator_restore_geyser_'+mode,dict(authentic_geyser_record={k:v for k,v in g.items()if k!='raw'},converted_callback=converted,selected_original_cancel_or_Death=event,restored_callback=restored,no_progress_refund_amount_owner_type_or_count_substitution=True),actual,[850 if mode=='completed_death'else 925,1000,188,11,g['amount'],(tables['units'][188].hit_points.whole*256+tables['units'][188].hit_points.fraction),3,18,2],joined(executions),dict(production=production,geyser_before=before,timeline=timeline,final=snapshot(u)))
   print(json.dumps(dict(phase='Assimilator restoration',mode=mode,callbacks=tick+1,cases=len(cases))),flush=True)
 if a.case_group in ('all','failure'):
  for balance in (99,100):
   u=new();executions=protoss_world(u,minerals=balance+50);probe,production=paid_probe(u,executions);geyser=make_geyser(u,executions);ex=call(u,0x41b4b0,probe,args=(157,));executions.append(ex)
   record('paid_assimilator_admission_'+str(balance),dict(real_paid_Probe=True,authentic_geyser_record=True,minerals_after_Probe=balance),[ex['return_eax'],r(u,0x5263d0+12),r(u,probe+0x98,2)],[int(balance==100),balance,157 if balance==100 else 228],joined(executions),dict(production=production,final=snapshot(u)))
  for mode in ('finite_CUnit3','finite_Sprite3','late_cost'):
   u=new(unit_count=3 if mode=='finite_CUnit3'else 16,sprite_count=3 if mode=='finite_Sprite3'else 64);executions=protoss_world(u,minerals=150 if mode=='late_cost'else 1000);probe,production=paid_probe(u,executions);geyser=make_geyser(u,executions)
   executions.append(call(u,0x41b4b0,probe,args=(157,)));assert executions[-1]['return_eax']==1;admitted=r(u,0x5263d0+12);executions.append(call(u,0x42a340,probe,args=(31,g['x'],g['y'])))
   if mode=='late_cost':executions.append(call(u,0x41b4b0,UNIT,args=(64,)));assert executions[-1]['return_eax']==1;executions.append(call(u,0x42a1e0,UNIT,args=(38,)))
   timeline=[];converted=None;last=None
   for tick in range(1400):
    object_step(u,executions);b=gas_state(u,geyser);pstate=unit_state(u,probe);key=(b['type'],b['completed'],pstate['position']['order'],pstate['position']['order_state'],r(u,0x5263d0+12))
    if key!=last:timeline.append(dict(callback=tick+1,building=b,probe=pstate,resources=[r(u,0x5263d0+12),r(u,0x526400+12)]));last=key
    if b['type']==157 and converted is None:converted=tick+1
    if converted and b['completed']:break
    if not converted and tick>64 and r(u,probe+0x4d,1)in(2,3):break
   actual=[admitted,r(u,0x5263d0+12),r(u,geyser+0x64,2),r(u,geyser+0x4c,1),r(u,geyser+0xd0,2),r(u,probe+0x98,2),r(u,0x665b38)]
   expected=[100,50,188,11,g['amount'],157,4]if mode=='late_cost'else[950,850,157,3,g['amount'],228,3]
   record('paid_assimilator_'+mode,dict(real_paid_Probe_Geyser=True,unit_pool=3 if mode=='finite_CUnit3'else 16,sprite_pool=3 if mode=='finite_Sprite3'else 64,late_actual_second_Probe_payment50=mode=='late_cost',converted_callback=converted,no_direct_resource_or_unit_substitution=True),actual,expected,joined(executions),dict(production=production,timeline=timeline,final=snapshot(u)))
   print(json.dumps(dict(phase='Assimilator failure profile',mode=mode,callbacks=tick+1,cases=len(cases))),flush=True)
 if a.case_group in ('all','failure'):
  u=new();executions=protoss_world(u);probe,production=paid_probe(u,executions);geyser=make_geyser(u,executions);executions.append(call(u,0x41b4b0,probe,args=(157,)));assert executions[-1]['return_eax']==1;executions.append(call(u,0x42a340,probe,args=(31,g['x'],g['y'])));first=[]
  for tick in range(400):
   object_step(u,executions)
   if r(u,geyser+0x64,2)==157:break
  assert r(u,geyser+0x64,2)==157
  executions.append(call(u,0x41b4b0,UNIT,args=(64,)));assert executions[-1]['return_eax']==1;executions.append(call(u,0x42a1e0,UNIT,args=(38,)));second=UNIT+0x3f0
  for tick in range(500):
   object_step(u,executions)
   if r(u,second+0xc)and r(u,second+0x64,2)==64 and r(u,second+0xdc)&1:break
  assert r(u,second+0xdc)&1;occupied_before=gas_state(u,geyser);executions.append(call(u,0x41b4b0,second,args=(157,)));assert executions[-1]['return_eax']==1;executions.append(call(u,0x42a340,second,args=(31,g['x'],g['y'])));timeline=[]
  for tick in range(400):
   object_step(u,executions);timeline.append(dict(callback=tick+1,second_probe=unit_state(u,second),geyser=gas_state(u,geyser),minerals=r(u,0x5263d0+12),placement_error=r(u,0x5da398,2)))
   if tick>64 and r(u,second+0x4d,1)in(2,3):break
  assert r(u,second+0x4d,1)in(2,3),timeline[-1]
  record('paid_second_probe_occupied_Assimilator_rejection',dict(genuine_first_Assimilator_conversion=True,genuine_paid_second_Probe=True,raw_build_admission157=True,occupied_before=occupied_before,no_host_geyser_type_or_resource_changes=True),[r(u,0x5263d0+12),r(u,geyser+0x64,2),r(u,geyser+0xd0,2),r(u,second+0x98,2),r(u,0x665b38)],[800,157,g['amount'],157,4],joined(executions),dict(first_probe_production=production,timeline=timeline,final=snapshot(u)))
  print(json.dumps(dict(phase='Occupied Assimilator',callbacks=tick+1,cases=len(cases))),flush=True)
 if a.case_group in ('all','carry'):
  inventory=goals_source.campaign_inputs(a.data,manifest);goal=next(row for row in inventory['goal_conditions']if row['type']==216);blob=(a.data/goal['map'].replace('\\','/')).read_bytes();assert hashlib.sha256(blob).hexdigest()==goal['map_sha256'];parts=dict(sections(blob));mrgn=parts[b'MRGN'];left,top,right,bottom,_,_=struct.unpack_from('<4I2H',mrgn,(goal['location']-1)*20);origin=((left+right)//2,(top+bottom)//2)
  u=new();executions=protoss_world(u);u.mem_write(0x534f40,mrgn);w(u,0x6ca754,3);u.mem_write(0x5349e0,bytes(8));w(u,0x672664,0,1);w(u,0x67294d,0,1)
  for player in range(8):w(u,0x6726d2+player*36,parts[b'FORC'][player]+1,1)
  rawcond=parts[b'TRIG'][goal['trigger_index']*2400+goal['index']*20:goal['trigger_index']*2400+(goal['index']+1)*20];assert hashlib.sha256(rawcond).hexdigest()==goal['record_sha256'];u.mem_write(0x80f100,rawcond)
  probe,production=paid_probe(u,executions)
  def walk(point):
   executions.append(call(u,0x42a340,probe,args=(6,*point)));moves=[];reissued=0
   for tick in range(2400):
    object_step(u,executions)
    if tick%16==0 or position(u,probe)['pixel']==list(point):moves.append(dict(callback=tick+1,probe=unit_state(u,probe)))
    if position(u,probe)['pixel']==list(point)and r(u,probe+0x4d,1)in(2,3):break
    if tick>0 and r(u,probe+0x4d,1)in(2,3)and position(u,probe)['pixel']!=list(point):
     reissued+=1;assert reissued<=8;executions.append(call(u,0x42a340,probe,args=(6,*point)));moves.append(dict(callback=tick+1,real_Move6_reissued_after_native_stop=True,position=position(u,probe),requested_point=list(point)))
   assert position(u,probe)['pixel']==list(point),moves[-1]
   return moves
  def bring():
   executions.append(call(u,0x4b9820,0));ex=call(u,0x4b8f20,0x80f100);executions.append(ex);return ex['return_eax']
  to_goal=walk(origin);p0=position(u,probe)['pixel'];ex=call(u,0x469460,216,p0[0]+8,(p0[1],11));executions.append(ex);item=ex['return_eax'];assert item
  for tick in range(32):
   object_step(u,executions)
   if r(u,probe+0xc0)==item:break
  assert r(u,probe+0xc0)==item and r(u,probe+0xe0,1)==4 and r(u,item+0xd4)==probe
  assert bring()==1;carry_before=dict(item=item,probe=unit_state(u,probe),item_state=unit_state(u,item),carrier=r(u,item+0xd4),item_hidden=bool(r(u,r(u,item+0xc)+0xe,1)&32))
  to_resource=walk((800,256));geyser=make_geyser(u,executions);executions.append(call(u,0x41b4b0,probe,args=(157,)));assert executions[-1]['return_eax']==1;executions.append(call(u,0x42a340,probe,args=(31,g['x'],g['y'])));timeline=[];last=None;converted=None
  for tick in range(1400):
   object_step(u,executions);b=gas_state(u,geyser);key=(b['type'],b['position']['order'],b['position']['order_state'],b['completed'],r(u,probe+0x4d,1),r(u,probe+0xc0),r(u,probe+0xe0,1),r(u,item+0xd4))
   if key!=last:timeline.append(dict(callback=tick+1,building=b,probe=unit_state(u,probe),item=unit_state(u,item),probe_item=r(u,probe+0xc0),carry_flags=r(u,probe+0xe0,1),item_carrier=r(u,item+0xd4),item_hidden=bool(r(u,r(u,item+0xc)+0xe,1)&32)));last=key
   if b['type']==157 and converted is None:converted=tick+1
   if converted and b['completed']:break
  assert converted and r(u,geyser+0xdc)&1 and r(u,probe+0xc0)==item and r(u,item+0xd4)==probe,(timeline[-8:],snapshot(u))
  departed=bring();returned=walk(origin);goal_after=bring();ex=joined(executions)
  actual=[r(u,0x5263d0+12),r(u,geyser+0x64,2),r(u,geyser+0xd0,2),r(u,probe+0xc0),r(u,probe+0xe0,1),r(u,item+0xd4),int(bool(r(u,r(u,item+0xc)+0xe,1)&32)),departed,goal_after,ex['call_entry_counts'].get('0x4272c0',0),r(u,0x665b38)]
  record('paid_carried_Probe_Assimilator_and_authentic_Chrysalis_Bring',dict(actual_raw_condition=goal,goal_rectangle=[left,top,right,bottom],real_paid_Probe=True,original_stock_item216_created_at_actual_Probe_position=True,actual_Move6_goal_and_resource_routes=True,converted_callback=converted,completed_callback=tick+1,no_host_carry_type_position_signal_or_amount_writes=True),actual,[850,157,g['amount'],item,4,probe,1,0,1,0,4],ex,dict(production=production,natural_to_goal=to_goal,carry_before=carry_before,natural_to_resource=to_resource,timeline=timeline,natural_return=returned,final=snapshot(u)))
  print(json.dumps(dict(phase='Carried Probe Assimilator',callbacks=tick+1,cases=len(cases))),flush=True)
 modules={Path(module.__file__).name:hashlib.sha256(Path(module.__file__).read_bytes()).hexdigest() for name,module in sorted(sys.modules.items()) if getattr(module,'__file__',None) and Path(module.__file__).parent.resolve()==Path(__file__).parent.resolve() and name!='__main__'}
 result=dict(target_sha256=local.TARGET_HASH,probe_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),source_dependencies_sha256=modules,engine=dict(unicorn=unicorn.__version__,pefile=pefile.__version__,machine=platform.machine(),python=platform.python_version()),dat_sha256=dat_hashes,asset_manifest_sha256=hashlib.sha256(a.asset_manifest.read_bytes()).hexdigest(),string_asset_manifest_sha256=hashlib.sha256(a.string_manifest.read_bytes()).hexdigest(),network_tbl_sha256=hashlib.sha256(network).hexdigest(),power_mask_prior_original_evidence_sha256=hashlib.sha256(power_path.read_bytes()).hexdigest(),power_mask_input_provenance=dict(grp_sha256=power_profile['grp_sha256'],source='40 complete original opacity calls; host mirrors explicit resource input; equals prior original post-allocation mask output',full_allocator_bootstrap_executed=False),case_group=a.case_group,helper_stubs=[],host_imports_executed=False,outer_accepted_frame_executed=False,instruction_bound=1000000,original_code_executed=True,complete_game_executed=False,complete_original_function_calls=len(executed_stats),visited_original_function_starts=len({v for c in cases for v in c['execution']['visited_functions']}),total_original_instructions_executed=sum(x['instructions'] for x in executed_stats),maximum_single_call_instructions=max(x['instructions'] for x in executed_stats),explicit_world_inputs=dict(map_pixels=[4096,4096],map_tiles=[128,128],map_rectangle=[0,0,4096,4096],tile_flags='0x1f700',zero_current_and_known_creep=True,clear_region_group=1,obstacle_contours=0,creep_record_dimensions=[128,128],creep_record_words=0,original_creep_life_pool_records=1024,owner=3,controller=2,default_initial_resource_balances=[1000,1000],Protoss_supply_triplet_addresses=['0x529544','0x529574','0x5295a4'],Protoss_global_supply_cap400_and_native_Nexus_provided18=True,stock_power_mask_pixels_half_extents=[256,160],stock_power_mask_grid=[16,10],stock_power_mask_bootstrap_malloc_unexecuted=True,initial_ai_region_arrays_zero=True,initial_auxiliary_countdowns=1000,elapsed_game_time5349d8=0,caller_object_step_counter_incremented=True,unit_buildtime_signal_stage_and_hp_advanced_by_original_code=True),reviewed_code_ranges=[dict(start=hex(lo),end_exclusive=hex(hi)) for lo,hi in sorted(set(ALLOW))],complete_cases=len(cases),prefix_cases=0,all_passed=all(x['passed'] for x in cases),cases=cases)
 a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(dict(cases=len(cases),all_passed=result['all_passed'])))
if __name__=='__main__':main()
