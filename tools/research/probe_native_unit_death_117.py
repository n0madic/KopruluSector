#!/usr/bin/env python3
"""Probe visible Death and complete reclamation with stock GRP and IScript.

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
import inspect_native_unit_death_117 as local
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
ALLOW=list(populated_source.ALLOW)+list(move_source.BLOCKS.values())+[v for k,v in local.BLOCKS.items() if k!='object_step_order_static_only']
PLAN=dict(target_sha256=local.TARGET_HASH,helper_stubs=[],instruction_bound=100000,scope='Visible Death issuance/animation/IScript end and complete image/sprite/CUnit reclamation',limits='Synthetic pools and owner state; original resource loading/rendering and full-game execution excluded')
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--exe',type=Path,required=True);p.add_argument('--data',type=Path,required=True);p.add_argument('--manifest',type=Path,required=True);p.add_argument('--assets',type=Path,required=True);p.add_argument('--asset-manifest',type=Path,required=True);p.add_argument('--pyms',type=Path,required=True);p.add_argument('--out',type=Path);p.add_argument('--case-group',choices=('all','visible','hide','effects','cancel'),default='all');p.add_argument('--execute-original-code',action='store_true');a=p.parse_args()
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
 def new(unit_count=4,sprite_count=8,image_count=32,thingy_count=8,seed=12345,rng_enabled=True):
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
  for i in range(thingy_count):w(u,0x809000+i*16,0x809000+(i-1)*16 if i else 0);w(u,0x809000+i*16+4,0x809000+(i+1)*16 if i+1<thingy_count else 0)
  w(u,0x5C7A20,0x809000 if thingy_count else 0);w(u,0x5C7A24,0x809000+(thingy_count-1)*16 if thingy_count else 0);w(u,0x5C7A28,0);w(u,0x5C7A2C,0)
  w(u,0x6D3140,0)
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
   result['units'].append(dict(address=unit,type=r(u,unit+0x64,2),owner=r(u,unit+0x4C,1),status=r(u,unit+0xDC),hp=r(u,unit+8),shield=r(u,unit+0x60),energy=r(u,unit+0xA2,2),direction=r(u,unit+0x21,1),live_counter=r(u,0x529604+(r(u,unit+0x64,2)*12+3)*4),completed_counter=r(u,0x52C0C4+(r(u,unit+0x64,2)*12+3)*4),wireframe_randomizer=r(u,unit+0xE1,1),generation=r(u,unit+0xA5,1),primary_order=r(u,unit+0x4D,1),secondary_order=r(u,unit+0xA6,1),build_time=r(u,unit+0xAC,2),queue=list(struct.unpack('<5H',u.mem_read(unit+0x98,10))),speed=r(u,unit+0x34),subunit=r(u,unit+0x70),sprite=dict(address=sp,id=r(u,sp+8,2),flags=r(u,sp+0xE,1),owner=r(u,sp+0xA,1),visibility=r(u,sp+0xC,1),main=r(u,sp+0x18),images=images)))
  return result
 def record(name,inputs,actual,expected,ex,state=None):
  assert actual==expected,(name,inputs,actual,expected)
  counters={str(k):r(u,0x673940+k*4) for k in range(50)};assert r(u,0x67393C)==advance(12345,sum(counters.values()))
  ex['rng_validation']=dict(initial_seed=12345,total_calls=sum(counters.values()),source_counters=counters,final_state=r(u,0x67393C),independent_integer_LCG_matches=True)
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
 def environment(u,visibility=0):
  w(u,0x69B584,0xB00000);w(u,0xB00000,4,2);w(u,0xB44A0C,200*256);w(u,0xB44A10,200*256);w(u,0xB97A1C,0x80A000)
  for address,offset in ((0x666454,0),(0x666458,0x10000),(0x66696C,0x20000),(0x6CE650,0x40000),(0x66644C,0x50000)):w(u,address,0xC00000+offset)
  w(u,0x54323C,0xC10000);u.mem_write(0xC00000,struct.pack('<I',0x1FFFF if not visibility else 0x1FF00)*16384);w(u,0x67CF60,0xC60000)
  w(u,0x6D319C,2);w(u,0x5C99B0,0);w(u,0x5D6E40,256);w(u,0x5D6E44,160)
  w(u,0x5264CC+12,8);w(u,0x672668,1);w(u,0x67266C,1)
  w(u,0x5AEF00,0x80B000);w(u,0x5AEF04,0x80B000+31*20);w(u,0x5AEF08,0)
  for i in range(32):w(u,0x80B000+i*20,0x80B000+(i-1)*20 if i else 0);w(u,0x80B000+i*20+4,0x80B000+(i+1)*20 if i<31 else 0)
  w(u,0x5DA390,2);w(u,0x5DA394,2);w(u,0x6663EC,0,1);w(u,0x6751F4,0);w(u,0x6BE650,0,1);w(u,0x665D40,0);w(u,0x665B60,0)
 def death_issue(u,unit,executions):
  executions.append(call(u,0x42A4D0,unit))
  w(u,0x543300,unit);w(u,0x543304,unit)
  executions.append(call(u,0x439420,unit))
  w(u,0x543300,0);w(u,0x543304,0)
 def finish(u,executions,callback=None):
  timeline=[]
  for tick in range(200):
   executions.append(call(u,0x438EF0,0))
   if callback:callback(tick+1)
   executions.append(call(u,0x418990,0))
   row=dict(tick=tick+1,count=r(u,0x665B38),unit_sprite=r(u,UNIT+0xC),things=chain(u,0x5C7A28),free_images=len(chain(u,0x544694)),free_sprites=len(chain(u,0x5C51C8)),pending=chain(u,0x665AE8))
   timeline.append(row)
   if not r(u,0x665B38) and not r(u,0x5C7A28):break
  assert tick<199
  assert set(chain(u,0x665AF0))=={UNIT+i*0x150 for i in range(4)}
  assert r(u,0x543300)==r(u,0x543304)==0
  return timeline
 ordinary_ticks={0:17,7:18,8:18,65:14,2:18,3:18,5:20}
 types=(0,7,8,65,2,3,5)
 for typ in (types if a.case_group in ('all','visible') else ()):
  for visibility in (0,255):
   for queued in (0,2):
    u=new();environment(u,visibility);executions=[call(u,0x424900,typ,200,(200,3)),call(u,0x424DA0,UNIT)]
    before=snapshot(u);assert before['units'][0]['sprite']['visibility']==visibility
    old_subunit=r(u,UNIT+0x70);old_turret_image=r(u,r(u,old_subunit+0xC)+0x18) if old_subunit else 0
    for j in range(queued):executions.append(call(u,0x4295D0,UNIT,args=(6,321|(654<<16),0,228,0)))
    assert len(chain(u,UNIT+0x74))==queued
    assert r(u,0x529604+(typ*12+3)*4)==r(u,0x52C0C4+(typ*12+3)*4)==1
    death_issue(u,UNIT,executions);after=snapshot(u)
    assert r(u,0x529604+(typ*12+3)*4)==r(u,0x52C0C4+(typ*12+3)*4)==0
    transfer=[r(u,old_subunit+0xC),r(u,old_turret_image+0x3C),r(u,old_turret_image+0xC,2)&8,after['units'][0]['sprite']['images'][0]['id']] if old_subunit else None
    if old_subunit:assert transfer==[0,SPRITE,0,235 if typ==3 else 251]
    timeline=finish(u,executions);recycle=next(row['tick'] for row in timeline if not row['count']);things=[row['tick'] for row in timeline if row['things']]
    expected_ticks=165 if typ==0 else ordinary_ticks[typ]
    assert set(chain(u,0x544694))=={IMAGE+i*0x40 for i in range(32)} and set(chain(u,0x5C51C8))=={SPRITE+i*0x24 for i in range(8)} and set(chain(u,0x5C7A20))=={0x809000+i*16 for i in range(8)}
    actual=[recycle,len(timeline),things[0] if things else None,r(u,0x665B38),chain(u,0x665AE8),len(chain(u,0x544694)),len(chain(u,0x5C51C8)),len(chain(u,0x5C7A20)),r(u,0x5AEF08),r(u,UNIT+0x74),r(u,UNIT+0x78),r(u,0x543300),r(u,0x543304)]
    expected=[ordinary_ticks[typ],expected_ticks,16 if typ==0 else None,0,[],32,8,8,0,0,0,0,0]
    record(f'visible_death_stock_{typ}_visibility_{visibility}_queue_{queued}',dict(type=typ,visibility=visibility,queued_orders=queued,position=[200,200],seed=12345,sound_backend=0,dispatcher_ambient_unit_context='Private fixture matching original 438ef0 caller at438fa3/438fa9',object_order='complete438ef0 then complete418990 once per selected update',timeline=timeline,created_state=before,death_state=after,turret_transfer=transfer),actual,expected,joined(executions),snapshot(u))
 for typ in (types if a.case_group in ('all','hide') else ()):
  for reason in ('hidden','forced_hide'):
   u=new();environment(u);executions=[call(u,0x424900,typ,200,(200,3))]
   if reason=='forced_hide':executions.append(call(u,0x424DA0,UNIT));w(u,UNIT+0x93,r(u,UNIT+0x93,1)|4,1)
   death_issue(u,UNIT,executions);before_step=snapshot(u);timeline=finish(u,executions)
   record(f'death_immediate_hidden_{typ}_{reason}',dict(type=typ,branch=reason,fixture_user_action_bit4=reason=='forced_hide',before_step=before_step,timeline=timeline),[len(timeline),r(u,0x665B38),len(chain(u,0x544694)),len(chain(u,0x5C51C8)),chain(u,0x5C7A28)],[1,0,32,8,[]],joined(executions))
 for typ in (types if a.case_group in ('all','effects') else ()):
  profile=dict(unit_count=4,sprite_count=8,image_count=32,thingy_count=8)
  for reason in (('no_thingy','no_sprite','reused_shadow_image') if typ==0 else ('no_overlay_image',) if typ!=65 else ()):
   profile=dict(unit_count=4,sprite_count=8,image_count=32,thingy_count=8)
   if reason=='no_thingy':profile['thingy_count']=0
   elif reason=='no_sprite':profile['sprite_count']=1
   elif reason in ('reused_shadow_image','no_overlay_image'):profile['image_count']=3 if typ in (3,5) else 2
   u=new(**profile);environment(u);executions=[call(u,0x424900,typ,200,(200,3)),call(u,0x424DA0,UNIT)]
   assert executions[0]['return_eax']==UNIT
   death_issue(u,UNIT,executions);timeline=finish(u,executions)
   expected_tick=(165 if reason=='reused_shadow_image' else 17) if typ==0 else 3
   record(f'death_effect_pool_profile_{typ}_{reason}',dict(type=typ,failure=reason,pools=profile.copy(),timeline=timeline),[len(timeline),r(u,0x665B38),len(chain(u,0x544694)),len(chain(u,0x5C51C8)),len(chain(u,0x5C7A20))],[expected_tick,0,profile['image_count'],profile['sprite_count'],profile['thingy_count']],joined(executions))
 for typ in (types if a.case_group in ('all','effects') else ()):
  for self_destruct in (False,True):
   for thingy_pool in (0,8):
    u=new(thingy_count=thingy_pool);environment(u,255);executions=[call(u,0x424900,typ,200,(200,3)),call(u,0x424DA0,UNIT)]
    w(u,UNIT+0xDC,r(u,UNIT+0xDC)|0x40000000|(0x80000000 if self_destruct else 0))
    death_issue(u,UNIT,executions);death=snapshot(u);things_at_death=chain(u,0x5C7A28)
    effect_info=[]
    for t in things_at_death:
     sp=r(u,t+0xC);effect_info.append([r(u,sp+8,2),r(u,sp+0xA,1),r(u,sp+0xD,1),r(u,sp+0xC,1),r(u,r(u,sp+0x18)+8,2)])
    if not self_destruct:assert effect_info==([[374,0,tables['units'][typ].elevation_level+1,255,557]] if thingy_pool else [])
    timeline=finish(u,executions);recycle=next(row['tick'] for row in timeline if not row['count'])
    total_ticks=(165 if typ==0 and thingy_pool else ordinary_ticks[typ]) if self_destruct else (39 if thingy_pool else 1)
    record(f'hallucination_death_{typ}_self_destruct_{self_destruct}_thingy_pool_{thingy_pool}',dict(type=typ,synthetic_status_additions=['hallucination0x40000000']+(['selfDestruct0x80000000'] if self_destruct else []),sound_backend=0,thingy_pool=thingy_pool,effect_at_death=effect_info,death_state=death,timeline=timeline),[recycle,len(timeline),r(u,0x665B38),len(chain(u,0x544694)),len(chain(u,0x5C51C8)),len(chain(u,0x5C7A20))],[ordinary_ticks[typ] if self_destruct else 1,total_ticks,0,32,8,thingy_pool],joined(executions))
 if a.case_group in ('all','effects'):
  u=new(image_count=2);environment(u);executions=[call(u,0x424900,0,200,(200,3)),call(u,0x424DA0,UNIT)]
  death_issue(u,UNIT,executions);extra=[]
  def consume_free_shadow(tick):
   if tick==14:
    assert len(chain(u,0x544694))==1
    ex=call(u,0x417B80,236,300,(300,0));executions.append(ex);extra.append(ex['return_eax'])
    assert ex['return_eax']==0x809000 and not r(u,0x544694)
  timeline=finish(u,executions,consume_free_shadow)
  record('Marine_corpse_image_exhaustion_after_real_competing_Thingy_creation',dict(image_pool=2,additional_stock_corpse_created_at_update=14,additional_position=[300,300],timeline=timeline),[next(x['tick'] for x in timeline if not x['count']),len(timeline),len(chain(u,0x544694)),len(chain(u,0x5C51C8)),len(chain(u,0x5C7A20)),len(timeline[15]['things'])],[17,163,2,8,8,1],joined(executions))
 for typ,producer_id in (((0,111),(7,106),(8,114),(65,160),(2,113),(3,113),(5,113)) if a.case_group in ('all','cancel') else ()):
  for slot in range(5):
   u=new();environment(u);producer=UNIT+8*0x150;producer_sprite=0x808000
   u.mem_write(producer,bytes(0x150));u.mem_write(producer_sprite,bytes(0x24))
   w(u,producer+0xC,producer_sprite);w(u,producer+0x64,producer_id,2);w(u,producer+0xDC,3);w(u,producer+0x4C,3,1);w(u,producer+0xA6,38,1);w(u,producer+0xE2,1,1);w(u,producer+0xA4,slot,1)
   w(u,producer_sprite+0x14,200,2);w(u,producer_sprite+0x16,200,2)
   queue=[228]*5;queue[slot]=typ;u.mem_write(producer+0x98,struct.pack('<5H',*queue))
   w(u,0x5263D0+12,1000);w(u,0x526400+12,1000)
   for total,maximum in ((0x529484,0x529424),(0x529514,0x5294B4),(0x5295A4,0x529544)):w(u,total+12,100);w(u,maximum+12,400)
   w(u,0x665AE0,producer);w(u,0x665AE4,producer);w(u,0x665B08+12,producer)
   executions=[call(u,0x439310,producer)];assert r(u,producer+0xEC)==UNIT,(typ,slot,snapshot(u),r(u,producer+0xE2,1))
   created=snapshot(u);executions.append(call(u,0x41B810,producer,args=(0,)))
   canceled=dict(parent_current_child=r(u,producer+0xEC),parent_train_state=r(u,producer+0xE2,1),child_order=r(u,UNIT+0x4D,1),child_user_action=r(u,UNIT+0x93,1),queue=list(struct.unpack('<5H',u.mem_read(producer+0x98,10))),stocks=[r(u,0x5263D0+12),r(u,0x526400+12)])
   assert canceled==dict(parent_current_child=UNIT,parent_train_state=2,child_order=0,child_user_action=0,queue=[228]*5,stocks=[1000+tables['units'][typ].mineral_cost,1000+tables['units'][typ].vespene_cost])
   w(u,0x543300,UNIT);w(u,0x543304,UNIT);executions.append(call(u,0x439420,UNIT));w(u,0x543300,0);w(u,0x543304,0)
   executions.append(call(u,0x439310,producer));parent_final=[r(u,producer+0xEC),r(u,producer+0xE2,1),r(u,producer+0xA4,1)]
   assert parent_final==[0,0,slot]
   # Remove the synthetic producer from the active object list using its real
   # list helper: the following complete step covers child retirement, not
   # synthetic producer movement/animation or a whole game frame.
   executions.append(call(u,0x427080,producer));timeline=finish(u,executions)
   record(f'cancel_actual_populated_Train_child_{typ}_head_{slot}',dict(type=typ,producer_type=producer_id,queue_head=slot,created_child=created,after_cancel=canceled,producer_after_Train=parent_final,pending_step_contract='Synthetic existing producer excluded from active list by complete native427080 before pending object step',timeline=timeline),[len(timeline),r(u,0x665B38),chain(u,0x665AE8),len(chain(u,0x544694)),len(chain(u,0x5C51C8)),chain(u,0x5C7A28),r(u,0x5AEF08)],[1,0,[],32,8,[],0],joined(executions),snapshot(u))
 modules={Path(module.__file__).name:hashlib.sha256(Path(module.__file__).read_bytes()).hexdigest() for name,module in sorted(sys.modules.items()) if getattr(module,'__file__',None) and Path(module.__file__).parent.resolve()==Path(__file__).parent.resolve() and name!='__main__'}
 result=dict(target_sha256=local.TARGET_HASH,probe_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),source_dependencies_sha256=modules,engine=dict(unicorn=unicorn.__version__,pefile=pefile.__version__,machine=platform.machine()),dat_sha256=dat_hashes,asset_manifest_sha256=hashlib.sha256(a.asset_manifest.read_bytes()).hexdigest(),case_group=a.case_group,helper_stubs=[],complete_cases=len(cases),prefix_cases=0,all_passed=all(x['passed'] for x in cases),cases=cases)
 a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(dict(cases=len(cases),all_passed=result['all_passed'])))
if __name__=='__main__':main()
