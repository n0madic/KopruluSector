#!/usr/bin/env python3
"""Probe selected natural Nuclear Strike, effects and consuming contracts.

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
import inspect_native_populated_lifecycle_117 as pending_source
import inspect_native_unit_death_117 as death_blocks
import probe_native_unit_death_117 as death_source
import inspect_native_active_iscript_117 as active_blocks
import probe_native_active_iscript_117 as active_source
import inspect_native_projectiles_117 as projectile_blocks
import probe_native_projectiles_117 as projectile_source
import inspect_native_spell_effects_117 as effects_source
import probe_native_spell_fields_117 as fields_source
import probe_native_spell_casting_117 as casting_source
import probe_native_status_effects_117 as status_source
import inspect_native_computer_combat_117 as splash_source
import probe_native_storm_117 as storm_source
import probe_native_support_casts_117 as support_source
import probe_native_hallucination_117 as hallucination_source
import inspect_native_ability_casts_117 as ability_static
import probe_native_ability_casts_117 as ability_source
import probe_native_carrier_117 as carrier_source
import probe_native_bw_casts_117 as bw_source
import probe_native_construction_117 as construction_source
import probe_native_weapon_effects_117 as weapon_source
import inspect_native_nuke_117 as local
import inspect_native_image_geometry_117 as geometry_source
import inspect_native_campaign_attack_117 as campaign_source
import inspect_native_military_arrival_117 as arrival_source
import inspect_native_attack_orders_117 as attack_source
import inspect_native_movement_fsm_117 as movement_source
import inspect_economy_frame_117 as frame_source
import inspect_ai_opcodes_117 as script_source
import inspect_native_radius_recruitment_117 as region_source
import inspect_native_damage_117 as damage_source
import probe_native_trigger_move_images_117 as move_images_source
import probe_native_trigger_grounded_move_117 as move_source
import probe_native_populated_lifecycle_117 as populated_source
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
ALLOW=list(bw_source.ALLOW)+list(local.BLOCKS.values())+list(construction_source.ALLOW)+list(weapon_source.ALLOW)
PLAN=dict(target_sha256=local.TARGET_HASH,helper_stubs=[],instruction_bound=100000,scope='Natural paid Nuclear Missile14 production, genuine Silo108 links and selected Ghost1 Nuclear Strike123..129 contracts',limits='Private stock objects/assets, actual order/effect consumers; declared synthetic terrain/player/pools, no Windows imports/WinMain/host I/O, no full game/campaign')
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--exe',type=Path,required=True);p.add_argument('--data',type=Path,required=True);p.add_argument('--manifest',type=Path,required=True);p.add_argument('--assets',type=Path,required=True);p.add_argument('--asset-manifest',type=Path,required=True);p.add_argument('--pyms',type=Path,required=True);p.add_argument('--string-manifest',type=Path,required=True);p.add_argument('--out',type=Path);p.add_argument('--execute-original-code',action='store_true');p.add_argument('--profiles',choices=('all','strike','cancel','damage'),default='all');a=p.parse_args()
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
 script=stock('scripts\\iscript.bin');orders_raw=stock('arr\\orders.dat');orderrows=orders.stock_orders(orders_raw);tech_raw=stock('arr\\techdata.dat');stat_txt=stock('rez\\stat_txt.tbl');table_raw['orders']=orders_raw
 for row in assetmanifest['images']:
  v=(a.assets/row['path'].replace('\\','/')).read_bytes();assert hashlib.sha256(v).hexdigest()==row['sha256'];assets[row['image_id']]=(row,v)
 for row in assetmanifest['lo_files']:
  v=(a.assets/row['path'].replace('\\','/')).read_bytes();assert hashlib.sha256(v).hexdigest()==row['sha256'];lo_assets.append((row,v))
 import unicorn
 from unicorn import Uc,UC_ARCH_X86,UC_MODE_32,UC_HOOK_CODE,UC_HOOK_MEM_INVALID
 from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_EBX,UC_X86_REG_ECX,UC_X86_REG_EDX,UC_X86_REG_EDI,UC_X86_REG_ESP,UC_X86_REG_ESI
 pe=pefile.PE(data=raw);assert pe.OPTIONAL_HEADER.ImageBase==0x400000 and pe.FILE_HEADER.Machine==0x14C
 image=pe.get_memory_mapped_image();size=(pe.OPTIONAL_HEADER.SizeOfImage+4095)&~4095;cases=[];cache={};initial_seeds={};executed_stats=[]
 def w(u,address,value,width=4):u.mem_write(address,(value&((1<<(8*width))-1)).to_bytes(width,'little'))
 def r(u,address,width=4):return int.from_bytes(u.mem_read(address,width),'little')
 def freeze_guest(uc):
  state=[(lo,bytes(uc.mem_read(lo,hi-lo+1)))for lo,hi,flags in uc.mem_regions()]
  digest=hashlib.sha256(b''.join(struct.pack('<2I',lo,len(blob))+blob for lo,blob in state)).hexdigest()
  return state,digest,initial_seeds[id(uc)]
 def thaw_guest(state):
  uc=Uc(UC_ARCH_X86,UC_MODE_32)
  for lo,blob in state[0]:uc.mem_map(lo,len(blob));uc.mem_write(lo,blob)
  initial_seeds[id(uc)]=state[2]
  return uc
 def new(unit_count=4,sprite_count=8,image_count=32,thingy_count=8,seed=12345,rng_enabled=True):
  u=Uc(UC_ARCH_X86,UC_MODE_32);u.mem_map(0x400000,size);u.mem_write(0x400000,image)
  for addr,length in ((0x800000,0x20000),(STACK,0x4000),(STOP,0x1000),(SCRIPT,0x10000),(ASSETS,0x800000),(0xB00000,0xA0000),(0xC00000,0x80000)):u.mem_map(addr,length)
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
  w(u,0x543230,4096,2);w(u,0x543234,4096,2);w(u,0x5264B4,128,2);w(u,0x5264B6,128,2)
  for base,count in ((0x665B08,12),(0x5C51D0,256),(0x5C55D0,256),(0x673940,50)):u.mem_write(base,b'\0'*(count*4))
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
  h=u.hook_add(UC_HOOK_CODE,hook);sp=STACK+0x3000;u.mem_write(sp,struct.pack('<'+'I'*(len(args)+1),STOP,*args));u.reg_write(UC_X86_REG_ESP,sp);u.reg_write(UC_X86_REG_ECX,ecx);u.reg_write(UC_X86_REG_EDX,edx)
  try:u.emu_start(start,STOP+1,count=bound)
  except Exception as e:raise RuntimeError(f'{e}; recent={recent[-30:]}; calls={calls[-10:]}') from e
  finally:u.hook_del(h);u.hook_del(bad)
  assert boundary and u.reg_read(UC_X86_REG_ESP)==sp+4*(len(args)+1),(hex(start),instructions,bound,recent[-10:])
  executed_stats.append(dict(entry=hex(start),instruction_count=instructions,instruction_bound=bound,visited_functions=[hex(x)for x in sorted(visited)]))
  return dict(complete_return=True,instruction_bound=bound,return_eax=u.reg_read(UC_X86_REG_EAX),instruction_count=instructions,calls=calls,iscript_opcodes=script_ops,bullet_initializations=initializations,impact_events=impacts,visited_functions=[hex(x) for x in sorted(visited)])
 def snapshot(u):
  parent=r(u,0x665B08+3*4);result=dict(owner_head=parent,unit_count=r(u,0x665B38),free_unit_head=r(u,0x665AF0),free_sprite_head=r(u,0x5C51C8),free_image_head=r(u,0x544694),hidden_head=r(u,0x665AF8),rng_state=r(u,0x67393C),pending_delete_head=r(u,0x665AE8),error=r(u,0x5DA398,2),rng_counters={str(k):r(u,0x673940+k*4) for k in (3,6,7,14,15,37)},units=[])
  for j in range(16):
   unit=UNIT+j*0x150
   if r(u,unit+0xC)==0:continue
   sp=r(u,unit+0xC);images=[];at=r(u,sp+0x1C)
   while at:
    images.append(dict(address=at,id=r(u,at+8,2),flags=r(u,at+0xC,2),direction=r(u,at+0xB,1),offset=list(struct.unpack('<2b',u.mem_read(at+0xE,2))),iscript=list(struct.unpack('<3H2B',u.mem_read(at+0x10,8))),base_frame=r(u,at+0x18,2),frame=r(u,at+0x1A,2),bounds=list(struct.unpack('<8h',u.mem_read(at+0x1C,16))),sprite=r(u,at+0x3C),grp=r(u,at+0x2C)))
    at=r(u,at+4);assert len(images)<32
   result['units'].append(dict(address=unit,type=r(u,unit+0x64,2),owner=r(u,unit+0x4C,1),status=r(u,unit+0xDC),timed_life=r(u,unit+0x110,2),buttons=r(u,unit+0x94,2),kill_count=r(u,unit+0x8F,1),hp=r(u,unit+8),shield=r(u,unit+0x60),energy=r(u,unit+0xA2,2),direction=r(u,unit+0x21,1),cooldowns=[r(u,unit+x,1) for x in (0x54,0x55,0x56,0x57)],movement_state=r(u,unit+0x97,1),live_counter=r(u,0x529604+(r(u,unit+0x64,2)*12+r(u,unit+0x4C,1))*4),completed_counter=r(u,0x52C0C4+(r(u,unit+0x64,2)*12+r(u,unit+0x4C,1))*4),wireframe_randomizer=r(u,unit+0xE1,1),generation=r(u,unit+0xA5,1),primary_order=r(u,unit+0x4D,1),secondary_order=r(u,unit+0xA6,1),build_time=r(u,unit+0xAC,2),queue=list(struct.unpack('<5H',u.mem_read(unit+0x98,10))),speed=r(u,unit+0x34),subunit=r(u,unit+0x70),sprite=dict(address=sp,position=[r(u,sp+0x14,2),r(u,sp+0x16,2)],elevation=r(u,sp+0xD,1),id=r(u,sp+8,2),flags=r(u,sp+0xE,1),owner=r(u,sp+0xA,1),visibility=r(u,sp+0xC,1),main=r(u,sp+0x18),images=images)))
  return result
 def record(name,inputs,actual,expected,ex,state=None):
  assert actual==expected,(name,actual,expected)
  counters={str(k):r(u,0x673940+k*4) for k in range(50)};seed=initial_seeds[id(u)];assert r(u,0x67393C)==advance(seed,sum(counters.values()))
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
 def setup(u,typ=0,victim_type=0,positions=((200,200),(280,200)),victim_owner=3):
  environment(u,255);w(u,0xB00000,2,2);u.mem_write(0xB0000c,struct.pack('<H',1)*65536)
  region=0xB00000+0x449fc+64;w(u,region,0,2);w(u,region+2,1,2);w(u,region+0xc,region+0x2c)
  pool=0x80c000;w(u,0x69b574,pool);w(u,0x69b570,pool);w(u,pool,0)
  for owner in range(12):w(u,0x534914+owner*12+owner,1,1)
  executions=[call(u,0x425610,0),call(u,0x40A0E0,0)]
  for at,t,(x,y) in ((UNIT,typ,positions[0]),(UNIT+0x150,victim_type,positions[1])):
   created=call(u,0x424900,t,x,(y,3 if at==UNIT else victim_owner));assert created['return_eax']==at
   executions.extend((created,call(u,0x424DA0,at)))
  w(u,0x543300,UNIT);w(u,0x543304,UNIT);w(u,0x543308,0)
  return executions
 def bullets(u):
  out=[]
  for at in chain(u,0x540750):
   sp=r(u,at+0xC);out.append(dict(address=at,weapon=r(u,at+0x60,1),state=r(u,at+0x4D,1),remaining=r(u,at+0x61,1),flags=r(u,at+0x62,1),signal=r(u,at+0x4F,1),bounces=r(u,at+0x63,1),source=r(u,at+0x64),target=r(u,at+0x5C),position=[r(u,at+0x28,2),r(u,at+0x2A,2)],fixed_position=[r(u,at+0x2C),r(u,at+0x30)],speed=r(u,at+0x38),direction=r(u,at+0x21,1),velocity_direction=r(u,at+0x4A,1),sprite=sp,images=[] if not sp else [dict(address=im,id=r(u,im+8,2),flags=r(u,im+0xC,2),pc=r(u,im+0x12,2),wait=r(u,im+0x17,1),animation=r(u,im+0x16,1)) for im in chain(u,sp+0x1C)]))
  return out
 def image_and_bullet_step(u,executions,update_attacker=True):
  if update_attacker:
   w(u,0x543300,UNIT);w(u,0x543304,UNIT);w(u,0x543308,0);executions.append(call(u,0x418B20,UNIT))
  w(u,0x543300,0);executions.append(call(u,0x40A5F0,0));executions.append(call(u,0x418990,0))
  assert r(u,0x543300)==r(u,0x543304)==r(u,0x543308)==0
 def trace_row(u,tick):
  return dict(tick=tick,victim_hp=r(u,UNIT+0x150+8),bullet_count=r(u,0x53DB8C),bullets=bullets(u),things=chain(u,0x5C7A28),free_bullets=len(chain(u,0x540758)),free_images=len(chain(u,0x544694)),free_sprites=len(chain(u,0x5C51C8)))
 def sprite_position(uc,at):
  sp=r(uc,at+0xC);return [r(uc,sp+0x14,2),r(uc,sp+0x16,2)]
 def effect_snapshot(uc):
  result=[]
  for at in chain(uc,0x5C7A28):
   sp=r(uc,at+0xC);result.append(dict(address=at,sprite=sp,position=[r(uc,sp+0x14,2),r(uc,sp+0x16,2)],elevation=r(uc,sp+0xD,1),images=[dict(id=r(uc,im+8,2),grp=r(uc,im+0x2C),animation=r(uc,im+0x16,1),pc=r(uc,im+0x12,2),wait=r(uc,im+0x17,1))for im in chain(uc,sp+0x1C)]))
  return result
 def image_ids(uc,at):
  sp=r(uc,at+0xC)
  return [r(uc,im+8,2)for im in chain(uc,sp+0x1C)]if sp else []
 def retire_units(uc,addresses):
  tail=[];states=[]
  for at in addresses:
   assert r(uc,at+0x4D,1)==0
   w(uc,0x543300,at);w(uc,0x543304,at);tail.append(call(uc,0x439420,at));updates=0
   while r(uc,at+0xC):
    tail.append(call(uc,0x418B20,at));updates+=1;assert updates<=500
   states.append(dict(address=at,actual_body_image_updates=updates,pending_before_recycle=chain(uc,0x665AE8),effects=effect_snapshot(uc)))
   w(uc,0x543300,0);w(uc,0x543304,0);tail.append(call(uc,0x4256B0,at))
  services=0
  while r(uc,0x5C7A28):
   tail.append(call(uc,0x418990,0));services+=1;assert services<=2000
  return tail,states,services
 u=new(unit_count=16,sprite_count=64,image_count=256,thingy_count=64);executions=setup(u,1,108,positions=((200,200),(500,200)),victim_owner=3);silo=UNIT+0x150
 w(u,0x508D48,0);w(u,0x508D54,1)
 u.mem_write(0x523A78,struct.pack('<228H',*([65535]*228)));u.mem_write(0x52655C,bytes([1])*(228*12));w(u,0x5263D0+3*4,1000);w(u,0x526400+3*4,1000);w(u,0x5294B4+3*4,200)
 before=snapshot(u);ex=call(u,0x4B32A0,14,3,(silo,));executions.append(ex);print('canmake',ex['return_eax'],'error',r(u,0x6BE654),flush=True);assert ex['return_eax']==1
 w(u,0x543300,silo);w(u,0x543304,silo)
 failed=call(u,0x41B4B0,silo,args=(14,));executions.append(failed);assert failed['return_eax']==0
 record('NuclearMissile_stock_requirements_success_does_not_prove_supply_or_payment',dict(native_Silo_and_Ghost_before=before,provided_Terran_supply200=True,global_Terran_cap0=True,stock_requirement_recipe14=True),[ex['return_eax'],failed['return_eax'],r(u,0x5263D0+12),r(u,0x526400+12),list(struct.unpack('<5H',u.mem_read(silo+0x98,10)))],[1,0,1000,1000,[228]*5],joined(executions),snapshot(u));executions=[]
 w(u,0x529514+3*4,400)
 executions.append(call(u,0x41B4B0,silo,args=(14,)));assert executions[-1]['return_eax']==1
 executions.append(call(u,0x42A1E0,silo,args=(38,)));executions.append(call(u,0x439310,silo));missile=r(u,silo+0xEC);assert missile;print('started',hex(missile),'type',r(u,missile+0x64,2),'buildtime',r(u,missile+0xAC,2),'siloorder',r(u,silo+0x4D,1),flush=True)
 assert missile and r(u,missile+0x64,2)==14 and r(u,missile+0xAC,2)==1500
 executions.append(call(u,0x439420,silo));assert r(u,silo+0xD0)==missile and r(u,missile+0x80)==silo and r(u,silo+0xD4)==0
 subsequent=0
 while r(u,silo+0xEC):
  executions.extend((call(u,0x439310,silo),call(u,0x439420,silo)));subsequent+=1;assert subsequent<=1501
 print('finished',subsequent,'ready',r(u,silo+0xD4),'order',r(u,missile+0x4D,1),'flags',hex(r(u,missile+0xDC)),flush=True)
 record('native_paid_NuclearMissile_and_NukeTrain_ready_links',dict(before=before,native_price_and_requirement_gate=True,unit_availability_fixture=True,requirement_cache_lazy=True,provided_Terran_supply_fixture200=True,direct_secondary_then_full_primary_each_service=True,subsequent_services=subsequent,audio_disabled_fixture=True,notification_window_index_mismatch=True),[r(u,missile+0x64,2),r(u,missile+0xDC)&1,r(u,silo+0xD0),r(u,missile+0x80),r(u,silo+0xD4),r(u,0x5263D0+3*4),r(u,0x526400+3*4)],[14,1,missile,silo,1,800,800],joined(executions),snapshot(u))
 saved_ready=freeze_guest(u)
 executions=[];targets=[]
 c=call(u,0x424900,0,600,(240,4));victim=c['return_eax'];executions.extend((c,call(u,0x424DA0,victim),call(u,0x42A2C0,victim,args=(23,))))
 before=snapshot(u);w(u,0x543300,UNIT);w(u,0x543304,UNIT);w(u,0x543308,0)
 executions.append(call(u,0x42A340,UNIT,args=(128,400,240)))
 for address in (0x6c46d4,0x69b568,0x69b564,0x6663e4):w(u,address,10000)
 for player in range(8):
  regional=0x80d000+player*0x400;u.mem_write(regional,bytes(0x400));w(u,0x6b1a74+player*4,regional)
 w(u,0x6a2e70,0);w(u,0x6a5d68,0);w(u,0x6b1a94,0);w(u,0x6b1a98,1);w(u,0x5432f0,0)
 aim_ticks=0
 while r(u,UNIT+0x4D,1)!=129:
  w(u,0x52651c,r(u,0x52651c)+1);executions.append(call(u,0x445070,0));aim_ticks+=1;assert aim_ticks<=100
 print('native aim callbacks',aim_ticks,'facing',r(u,UNIT+0x21,1),flush=True);print('launch committed','ghost',r(u,UNIT+0x4D,1),r(u,UNIT+0x4E,1),'missile',r(u,missile+0x4D,1),r(u,missile+0x4E,1),'links',hex(r(u,UNIT+0x80)),hex(r(u,missile+0x80)),flush=True)
 record('natural_Ghost_NukeGround_consumes_genuine_ready_Silo_missile',dict(before=before,target_point=[400,240],actual_order128=True,actual_object_aim_callbacks=aim_ticks,no_authored_heading_or_missile_connections=True,sound_disabled_fixture=True),[r(u,UNIT+0x4D,1),r(u,missile+0x4D,1),r(u,silo+0xD0),r(u,silo+0xD4),r(u,UNIT+0x80),r(u,missile+0x80),r(u,UNIT+0x94,2)],[129,125,0,0,missile,UNIT,237],joined(executions),snapshot(u))
 executions=[];w(u,0x543300,missile);w(u,0x543304,missile);executions.append(call(u,0x439420,missile));print('missile initial','state',r(u,missile+0x4E,1),'timer',r(u,missile+0x54,1),'move',r(u,missile+0x97,1),'target',r(u,missile+0x10,4),flush=True)
 record('natural_NukeLaunch_state0_begins_actual_upward_motion_and_wait90',dict(original_live_connected_missile=True,original_primary_dispatch=True,sound_disabled_fixture=True),[r(u,missile+0x4E,1),r(u,missile+0x54,1),r(u,missile+0x4D,1)],[1,90,125],joined(executions),snapshot(u))
 executions=[];launch_timeline=[];previous=None;bullet_state=None
 for tick in range(2000):
  w(u,0x52651c,r(u,0x52651c)+1)
  try:executions.append(call(u,0x445070,0))
  except Exception as e:raise RuntimeError(f'Nuke object callback={tick+1}, recent state={launch_timeline[-5:]}; {e}')from e
  row=dict(callback=tick+1,ghost_order=r(u,UNIT+0x4d,1),ghost_state=r(u,UNIT+0x4e,1),ghost_hp=r(u,UNIT+8),ghost_connection=r(u,UNIT+0x80),ghost_dot=r(u,UNIT+0xd0),missile_order=r(u,missile+0x4d,1),missile_state=r(u,missile+0x4e,1),missile_timer=r(u,missile+0x54,1),missile_signal=r(u,missile+0x4f,1),missile_position=list(struct.unpack('<2H',u.mem_read(missile+0x28,4))),missile_sprite=r(u,missile+0xc),missile_kills=r(u,missile+0x8f,1),enemy_Marine_hp=r(u,victim+8),BC_hp=[r(u,at+8)for at in targets],bullet_count=r(u,0x53db8c),effects=effect_snapshot(u))
  if bullet_state is None and r(u,0x540750):bullet_state=freeze_guest(u)
  key=(row['ghost_order'],row['ghost_state'],row['missile_state'],row['missile_order'],row['bullet_count'])
  if key!=previous or executions[-1]['impact_events']:launch_timeline.append(row);print('launch tick',tick+1,'key',key,'missile pos',row['missile_position'],flush=True);previous=key
  if missile in chain(u,0x665af0) and not r(u,0x540750) and not r(u,0x5c7a28):break
 else:raise AssertionError(('Nuke never finished',launch_timeline[-5:]))
 print('strike complete callbacks',tick+1,'enemy recycled',victim in chain(u,0x665af0),'missile kills',max(x['missile_kills']for x in launch_timeline),flush=True)
 assert bullet_state is not None
 assert any(op['offset']==26448 and op['opcode']==39 and op['image_id']==316 and op['unit_context']==missile for ex in executions for op in ex['iscript_opcodes'])
 assert sum(ev['weapon']==31 and ev['source']==missile and ev['target']==missile for ex in executions for ev in ex['impact_events'])==1
 record('complete_stock_Nuke_ascent_channel_descent_impact_and_cleanup',dict(actual_full_object_callbacks=tick+1,timeline=launch_timeline,positive_stock_images_and_IScript=True,stock_castspell_PC26448_opcode39=True,no_authored_order_state_or_signal=True,finite_pool_counts=dict(units=16,sprites=64,images=256,Thingys=64),warning_display_gated_by_window_mismatch=True),[int(missile in chain(u,0x665af0)),r(u,missile+0xc),r(u,0x540750),r(u,0x5c7a28),int(victim in chain(u,0x665af0)),max(x['missile_kills']for x in launch_timeline)],[1,0,0,0,1,1],joined(executions),snapshot(u))
 if a.profiles in ('all','damage'):
  assert bullet_state is not None
  for offset,divisor in ((165,1),(166,2),(229,2),(230,4),(293,4),(294,0)):
   u=thaw_guest(bullet_state);executions=[];bullet=r(u,0x540750)
   c=call(u,0x424900,12,400+offset,(240,4));at=c['return_eax'];executions.extend((c,call(u,0x424DA0,at)))
   ex=call(u,0x409010,bullet,args=(at,));executions.append(ex);assert ex['return_eax']==500*256
   executions.append(call(u,0x409590,at,bullet))
   expected=500*256-(500*256//divisor-3*256)if divisor else 500*256
   record('genuine_Nuke_Bullet_Battlecruiser_radius_'+str(offset),dict(native_Bullet_state_sha256=bullet_state[1],target_created_at=[400+offset,240],source_and_Bullet_stock_created=True,selected_complete_callback409590=True,no_intervening_object_motion=True,rectangle_left_extent37=True),[r(u,at+8),ex['return_eax']],[expected,500*256],joined(executions),snapshot(u))
  for typ,amount in ((106,1000),(154,1000),(48,533)):
   u=thaw_guest(bullet_state);executions=[];bullet=r(u,0x540750)
   c=call(u,0x424900,typ,1400,(1000,4));at=c['return_eax'];executions.extend((c,call(u,0x424DA0,at)))
   first=call(u,0x409010,bullet,args=(at,));executions.append(first)
   executions.append(call(u,0x41E150,at,args=(100*256,missile,3,0)))
   second=call(u,0x409010,bullet,args=(at,));executions.append(second)
   record('Nuke_damage_uses_stock_maximum_life_not_current_'+str(typ),dict(genuine_Bullet_state_sha256=bullet_state[1],stock_target_type=typ,actual_hp_damage_before_second_measurement100=True,direct_complete_damage_amount_helper409010=True,weapon31_DAT_damage600_is_not_used=True),[first['return_eax'],second['return_eax']],[amount*256,amount*256],joined(executions),snapshot(u))
  for mode in ('same_owner','allied_owner','Invulnerable_fixture','source_itself'):
   u=thaw_guest(bullet_state);executions=[];bullet=r(u,0x540750)
   if mode=='source_itself':at=missile
   else:
    owner=3 if mode=='same_owner'else 5 if mode=='allied_owner'else 4
    w(u,0x6726d0+owner*36,2,1);w(u,0x534914+owner*12+owner,1,1)
    if mode=='allied_owner':w(u,0x534914+3*12+5,1,1);w(u,0x534914+5*12+3,1,1)
    c=call(u,0x424900,12,400,(240,owner));at=c['return_eax'];executions.extend((c,call(u,0x424DA0,at)))
    if mode=='Invulnerable_fixture':w(u,at+0xdc,r(u,at+0xdc)|0x4000000)
   hp=r(u,at+8);executions.append(call(u,0x409590,at,bullet))
   record('genuine_Nuke_Bullet_target_gate_'+mode,dict(genuine_Bullet_state_sha256=bullet_state[1],selected_actual_splash_callback=True,invulnerable_status_fixture=mode=='Invulnerable_fixture',native_created_target=True),r(u,at+8),hp if mode in ('Invulnerable_fixture','source_itself')else 3*256,joined(executions),snapshot(u))
  for mode in ('null_NukeUnit','NukeWait_noop'):
   u=thaw_guest(saved_ready);executions=[];w(u,0x543300,UNIT);w(u,0x543304,UNIT)
   executions.append(call(u,0x42A380,UNIT,args=(127,0))if mode=='null_NukeUnit'else call(u,0x42A2C0,UNIT,args=(123,)))
   executions.append(call(u,0x439420,UNIT))
   record('original_'+mode,dict(native_created_Ghost=True,declared_low_level_order_admission=True,genuine_ready_producer_state_sha256=saved_ready[1]),[r(u,UNIT+0x4d,1),r(u,silo+0xd0),r(u,silo+0xd4)],[2 if mode=='null_NukeUnit'else 123,missile,1],joined(executions),snapshot(u))
  for exhausted in (False,True):
   u=new(unit_count=2 if exhausted else 4);executions=setup(u,1,108,positions=((200,200),(500,200)),victim_owner=3);silo=UNIT+0x150
   before=[r(u,0x5263d0+12),r(u,0x526400+12),r(u,silo+0xd0),r(u,silo+0xd4)]
   executions.append(call(u,0x493450,silo));created=r(u,silo+0xd0)
   if exhausted:expected=[0,0,0,0,0]
   else:expected=[14,1,silo,0,0]
   record('original_script_create_nuke_'+('allocation_failure'if exhausted else'no_resource_payment'),dict(parent_created_and_completed=True,initial_resources_and_ready=before,human_controller2=True,computer_child_AI_assignment_not_claimed=True,finite_unit_pool=2 if exhausted else 4),[r(u,created+0x64,2)if created else 0,r(u,silo+0xd4),r(u,created+0x80)if created else 0,r(u,0x5263d0+12),r(u,0x526400+12)],expected,joined(executions),snapshot(u))
 if a.profiles in ('all','cancel'):
  silo=UNIT+0x150;missile=UNIT+0x2a0
  for mode in ('cancel_ascent','cancel_descent','Ghost_death_ascent','Ghost_death_descent','paint_ground','paint_target','empty_Thingy_pool','empty_Bullet_pool'):
   u=thaw_guest(saved_ready);executions=[];timeline=[];interrupted=False
   for address in (0x6c46d4,0x69b568,0x69b564,0x6663e4):w(u,address,10000)
   for player in range(8):
    regional=0x80d000+player*0x400;u.mem_write(regional,bytes(0x400));w(u,0x6b1a74+player*4,regional)
   w(u,0x6a2e70,0);w(u,0x6a5d68,0);w(u,0x6b1a94,0);w(u,0x6b1a98,1);w(u,0x5432f0,0)
   w(u,0x543300,UNIT);w(u,0x543304,UNIT)
   if mode=='empty_Thingy_pool':w(u,0x5c7a20,0);w(u,0x5c7a24,0)
   if mode=='empty_Bullet_pool':w(u,0x540758,0);w(u,0x54075c,0)
   if mode=='paint_target':executions.append(call(u,0x42A380,UNIT,args=(126,silo)))
   else:executions.append(call(u,0x42A340,UNIT,args=(126 if mode=='paint_ground' else 128,400,240)))
   previous=None;launch_observed=False;impact=[];disconnection=None
   for tick in range(1400):
    if mode.startswith(('cancel_','Ghost_death_')) and not interrupted and r(u,missile+0x4d,1)==125 and r(u,missile+0x4e,1)==(1 if mode.endswith('ascent')else 5):
     before_disconnect=[r(u,UNIT+0x80),r(u,missile+0x80),r(u,missile+0x4e,1)]
     if mode.startswith('cancel_'):
      u.mem_write(0x6a5ef0,bytes(12*8*4));w(u,0x508d48,3);w(u,0x6a5ef0+3*12*4,UNIT);executions.append(call(u,0x4724f0,0))
      assert r(u,UNIT+0x80)==r(u,missile+0x80)==0
     else:executions.append(call(u,0x42a4d0,UNIT))
     disconnection=dict(callback=tick+1,before=before_disconnect,after=[r(u,UNIT+0x80),r(u,missile+0x80)],entry='0x4724f0'if mode.startswith('cancel_')else'0x42a4d0')
     interrupted=True
    w(u,0x52651c,r(u,0x52651c)+1)
    try:ex=call(u,0x445070,0);executions.append(ex)
    except Exception as e:raise RuntimeError(f'{mode} callback={tick+1}; timeline={timeline[-4:]}; {e}')from e
    impact+=ex['impact_events'];launch_observed|=r(u,missile+0x4d,1)==125
    row=dict(callback=tick+1,Ghost_order=r(u,UNIT+0x4d,1),Ghost_state=r(u,UNIT+0x4e,1),missile_order=r(u,missile+0x4d,1),missile_state=r(u,missile+0x4e,1),missile_connection=r(u,missile+0x80),missile_position=list(struct.unpack('<2H',u.mem_read(missile+0x28,4))),signal=r(u,missile+0x4f,1),timer=r(u,missile+0x54,1),effects=effect_snapshot(u))
    key=(row['Ghost_order'],row['Ghost_state'],row['missile_order'],row['missile_state'])
    if key!=previous or ex['impact_events']:timeline.append(row);previous=key
    if launch_observed and missile in chain(u,0x665af0) and not r(u,0x540750)and not r(u,0x5c7a28):break
   else:raise AssertionError((mode,'incomplete lifecycle',timeline[-4:]))
   expected_impact=0 if mode.endswith('ascent')or mode=='empty_Bullet_pool'else 1
   print('profile',mode,'callbacks',tick+1,'impacts',len(impact),'disconnection',disconnection,flush=True)
   record('native_Nuclear_Strike_'+mode,dict(genuine_ready_producer_state_sha256=saved_ready[1],snapshot_clone_of_original_produced_state=True,actual_full_object_callbacks=tick+1,selection_pointer_fixture=mode.startswith('cancel_'),empty_pool_fixture=mode if mode.startswith('empty_')else None,disconnection=disconnection,timeline=timeline),[int(missile in chain(u,0x665af0)),r(u,missile+0xc),len([x for x in impact if x['weapon']==31]),r(u,silo+0xd0),r(u,silo+0xd4)],[1,0,expected_impact,0,0],joined(executions),snapshot(u))
 modules={Path(module.__file__).name:hashlib.sha256(Path(module.__file__).read_bytes()).hexdigest() for name,module in sorted(sys.modules.items()) if getattr(module,'__file__',None) and Path(module.__file__).parent.resolve()==Path(__file__).parent.resolve() and name!='__main__'}
 assert modules==dependency_hashes_at_start and hashlib.sha256(Path(__file__).read_bytes()).hexdigest()==script_hash_at_start,'Probe sources changed during execution'
 result=dict(target_sha256=local.TARGET_HASH,target_version='1.17.0.1',probe_script_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),source_dependencies_sha256=modules,case_profile=a.profiles,engine=dict(unicorn=unicorn.__version__,pefile=pefile.__version__,capstone=local.capstone.__version__,machine=platform.machine(),python=platform.python_version(),platform=platform.system()),dat_sha256=dat_hashes,asset_manifest_sha256=hashlib.sha256(a.asset_manifest.read_bytes()).hexdigest(),string_asset_manifest_sha256=hashlib.sha256(a.string_manifest.read_bytes()).hexdigest(),network_tbl_sha256=hashlib.sha256(network).hexdigest(),helper_stubs=[],original_code_executed=True,complete_game_executed=False,stock_nuke_contract=dict(missile_buildtime=tables['units'][14].build_time,missile_mineral_cost=tables['units'][14].mineral_cost,missile_gas_cost=tables['units'][14].vespene_cost,weapon31=vars(tables['weapons'][31])),instruction_bound_default=100000,complete_original_function_calls=len(executed_stats),max_single_call_instructions=max(x['instruction_count']for x in executed_stats),total_original_instructions_executed=sum(x['instruction_count']for x in executed_stats),visited_functions=sorted({f for x in executed_stats for f in x['visited_functions']}),reviewed_code_ranges=[dict(start=hex(lo),end_exclusive=hex(hi)) for lo,hi in sorted(set(ALLOW))],complete_cases=len(cases),prefix_cases=0,all_passed=all(x['passed']for x in cases),cases=cases)
 a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(dict(cases=len(cases),all_passed=result['all_passed'])))
if __name__=='__main__':main()
