#!/usr/bin/env python3
"""Guarded original portrait/audio/dialog calls; imports never execute."""
import argparse,hashlib,json,platform,struct,sys
from collections import Counter
from pathlib import Path
import pefile
import inspect_native_audiovisual_117 as source
PRIVATE,CONTROL,PARENT,EVENT,OUTPUT,WAV,STACK,STOP=0x800000,0x800100,0x800200,0x800300,0x800400,0x810000,0x900000,0xA00000
MASK=0xFFFFFFFF
def sha(b):return hashlib.sha256(b).hexdigest()
def ui_rng(state,low,high,mask):
 x,y,z=state;steps=0
 while True:
  x=(x+y)&MASK;y=(y+0x71823402)&MASK;z=(z+x)&MASK;x>>=1;y^=z;value=(y^x)&(mask-1);steps+=1
  if value<high-low+1:return value+low,(x,y,z),steps
  assert steps<100000
def signed16(v):return (v+32768)%65536-32768
def native_wav_model(b,length):
 remain=length;fmt=bytes([0xA5]*18)
 if remain<12:return 0,remain,fmt
 remain-=12
 if b[:4]!=b'RIFF' or b[8:12]!=b'WAVE':return 0,remain,fmt
 def chunk(at,remaining,tag):
  while remaining>=8:
   remaining-=8;name=b[at:at+4];n=struct.unpack_from('<I',b,at+4)[0];at+=8
   if name==tag:return at,remaining,n
   if remaining<n:return 0,remaining,0
   remaining-=n;at+=n
  return 0,remaining,0
 at,remain,n=chunk(12,remain,b'fmt ')
 if not at or n<16 or remain<n:return 0,remain,fmt
 fmt=b[at:at+16]+b'\0\0';remain-=n;at+=n
 at,remain,n=chunk(at,remain,b'data')
 if not at or remain<n:return 0,remain,fmt
 return at,n,fmt
def main():
 p=argparse.ArgumentParser(description=__doc__)
 for k in ('exe','assets','asset-manifest'):p.add_argument('--'+k,type=Path,required=True)
 p.add_argument('--out',type=Path);p.add_argument('--execute-original-code',action='store_true');a=p.parse_args();raw=a.exe.read_bytes();assert sha(raw)==source.HASH
 if not a.execute_original_code:print(json.dumps(dict(original_code_executed=False,target_sha256=source.HASH,stubs=[])));return
 assert a.out and platform.machine().lower() in ('x86_64','amd64')
 import unicorn
 from unicorn import Uc,UC_ARCH_X86,UC_MODE_32,UC_HOOK_CODE,UC_HOOK_MEM_WRITE
 from unicorn.x86_const import UC_X86_REG_ECX,UC_X86_REG_EDX,UC_X86_REG_EAX,UC_X86_REG_EBX,UC_X86_REG_ESI,UC_X86_REG_ESP,UC_X86_REG_EIP,UC_X86_REG_EFLAGS
 pe=pefile.PE(data=raw);u=Uc(UC_ARCH_X86,UC_MODE_32);u.mem_map(0x400000,(pe.OPTIONAL_HEADER.SizeOfImage+4095)&~4095);u.mem_write(0x400000,pe.get_memory_mapped_image());u.mem_map(PRIVATE,0x80000);u.mem_map(STACK,0x10000);u.mem_map(STOP,4096)
 def w(addr,v,n=4):u.mem_write(addr,(v&((1<<(n*8))-1)).to_bytes(n,'little'))
 def r(addr,n=4):return int.from_bytes(u.mem_read(addr,n),'little')
 am=json.loads(a.asset_manifest.read_text());assets={}
 for path,row in am['assets'].items():
  b=(a.assets/path.replace('\\','/')).read_bytes();assert sha(b)==row['sha256'];assets[path]=b
 for key,path in (('units','arr\\units.dat'),('portdata','arr\\portdata.dat'),('sfxdata','arr\\sfxdata.dat')):
  at=0;base,count=source.DESCRIPTORS[key];b=assets[path]
  for i in range(count):
   dest,width,number=struct.unpack('<3I',pe.get_data(base-0x400000+i*12,12));n=width*number;u.mem_write(dest,b[at:at+n]);at+=n
  assert at==len(b)
 ports=source.rows(assets['arr\\portdata.dat'],[4,4,1,1,1,1],110)
 ranges=[v for k,v in source.BLOCKS.items() if not k.endswith('_static')];entries={lo for lo,hi in ranges};approved=set();visited=set();cases=[];writes=0;entry_counts=Counter()
 writable=[(PRIVATE,PRIVATE+0x1000),(STACK,STACK+0x10000),(0x50D7C8,0x50D7D8),(0x666978,0x667290),(0x6A61D0,0x6AAA64),(0x6AB380,0x6AB4C8),(0x6CF798,0x6CF7A8)]
 def write_hook(machine,access,addr,n,value,unused):
  nonlocal writes
  assert any(lo<=addr and addr+n<=hi for lo,hi in writable),('Undeclared write',hex(addr),n);writes+=1
 u.hook_add(UC_HOOK_MEM_WRITE,write_hook)
 def call(start,ecx=0,edx=0,args=(),stop_before=None,stop_kind='callee',esi=None):
  done=False;prefix=False;steps=0;calls=[];choices=[]
  def hook(machine,pc,n,unused):
   nonlocal done,prefix,steps
   if pc==STOP:done=True;machine.emu_stop();return
   if pc==stop_before:prefix=True;machine.emu_stop();return
   if (pc,n) not in approved:assert any(lo<=pc and pc+n<=hi for lo,hi in ranges),('Unreviewed instruction',hex(pc),calls);approved.add((pc,n))
   visited.add(pc);steps+=1
   if pc in entries:calls.append(hex(pc));entry_counts[hex(pc)]+=1
   if pc in (0x4A6BB0,0x4A6C95):choices.append(dict(pc=hex(pc),variant=u.reg_read(UC_X86_REG_EBX)&255))
  h=u.hook_add(UC_HOOK_CODE,hook);sp=STACK+0xF000;u.mem_write(sp,struct.pack('<'+'I'*(len(args)+1),STOP,*args));u.reg_write(UC_X86_REG_ESP,sp);u.reg_write(UC_X86_REG_ECX,ecx&MASK);u.reg_write(UC_X86_REG_EDX,edx&MASK);u.reg_write(UC_X86_REG_EAX,0);u.reg_write(UC_X86_REG_EFLAGS,2)
  if esi is not None:u.reg_write(UC_X86_REG_ESI,esi)
  try:u.emu_start(start,STOP+1,count=1000000)
  except Exception as e:raise RuntimeError((hex(start),hex(u.reg_read(UC_X86_REG_EIP)),calls)) from e
  finally:u.hook_del(h)
  assert done or prefix
  if done:assert u.reg_read(UC_X86_REG_ESP)==sp+4*(len(args)+1)
  return dict(start_va=hex(start),complete_return=done,stopped_before_unexecuted_callee=prefix and stop_kind=='callee',stopped_before_unexecuted_instruction=prefix and stop_kind=='instruction',stop_va=hex(stop_before) if prefix else None,original_instructions=steps,calls=calls,variant_observations=choices,return_eax=u.reg_read(UC_X86_REG_EAX))
 def record(family,inputs,actual,expected,ex):
  assert actual==expected,(family,inputs,actual,expected);cases.append(dict(case=family,inputs=inputs,actual=actual,expected=expected,passed=True,execution=ex))
 def portrait_fixture(seed,mode=2):
  u.mem_write(CONTROL,bytes(0x60));w(CONTROL+0x18,1);w(0x6AB39C,CONTROL);w(0x6AB390,PRIVATE+0x700);w(0x50D7C8,mode)
  for address,value in zip((0x50D7CC,0x50D7D0,0x50D7D4),seed):w(address,value)
  for i in range(4):w(0x6AB3D4+i*4,r(0x501380+i*12))
 # Complete resolver for all 228 genuine DAT rows, including both thresholds.
 for mission in (0,27,28,31,32,65535):
  w(0x526524,mission,2)
  for typ in range(228):
   base=r(0x523358+typ*2,2);expect=r(0x523488,2) if base==38 and mission>31 else r(0x5233F4,2) if base==60 and mission>=28 else base
   ex=call(0x4A65D0,typ);record('stock_portrait_campaign_thresholds',dict(type=typ,mission=mission,base_portrait=base),ex['return_eax'],expect,ex)
 # Each UI draw value is represented by a deterministic public synthetic seed.
 seeds={}
 for seed in range(10000):
  draw,state,steps=ui_rng((0,seed,0),0,100,128);seeds.setdefault(draw,(0,seed,0))
  if len(seeds)==101:break
 assert len(seeds)==101
 for low,high,mask in ((0,100,128),(0,2,4),(3,3,4),(5,6,4)):
  for seed in list(seeds.values())+[(MASK,MASK,MASK),(1,2,3),(0x12345678,0x9ABCDEF0,0xFEDCBA98),struct.unpack('<3I',pe.get_data(0x50D7CC-0x400000,12))]:
   portrait_fixture(seed);want,new,iterations=ui_rng(seed,low,high,mask);ex=call(0x4A6E00,low,high,(mask,));record('complete_ui_rng_rejection',dict(seed=list(seed),low=low,high=high,mask=mask,rejection_iterations=iterations),[ex['return_eax']]+[r(x) for x in (0x50D7CC,0x50D7D0,0x50D7D4)],[want]+list(new),ex)
 # All distinct stock weight profiles, every accepted draw, complete idle routine.
 # The real guard with a synthetic nonzero handle sentinel skips file opening; native choice PCs are observed.
 profiles={tuple(row):i for i,row in enumerate(zip(*ports[2:]))}
 for weights,ident in profiles.items():
  for draw,seed in sorted(seeds.items()):
   portrait_fixture(seed);total=0;variant=0
   for index,value in enumerate(weights):
    total+=value
    if draw<=total:variant=index;break
   else:variant=0
   _,state,_=ui_rng(seed,0,100,128);ex=call(0x4A6B40,ident);observed=[v['variant'] for v in ex['variant_observations']];record('stock_idle_weight_boundaries_existing_handle',dict(portrait=ident,weights=list(weights),draw=draw,seed=list(seed),synthetic_nonnull_smk_handle=True,control_already_dirty=True),dict(variants=observed,rng=[r(x) for x in (0x50D7CC,0x50D7D0,0x50D7D4)],handle=r(0x6AB390)),dict(variants=[variant],rng=list(state),handle=PRIVATE+0x700),ex)
 # Every stock portrait ID against eight seeds; no claims about opening/decoding.
 for ident in range(110):
  for seed in list(seeds.values())[:8]:
   portrait_fixture(seed);draw,state,_=ui_rng(seed,0,100,128);total=0;variant=0
   for index,row in enumerate(ports[2:]):
    total+=row[ident]
    if draw<=total:variant=index;break
   ex=call(0x4A6B40,ident);record('all_stock_portrait_idle_input_rows_existing_handle',dict(portrait=ident,seed=list(seed),synthetic_nonnull_smk_handle=True),[v['variant'] for v in ex['variant_observations']],[variant],ex)
  for previous in (0,1,2,255):
   for seed in list(seeds.values())[:8]:
    portrait_fixture(seed);w(0x6AB3EC,previous,1);draw,state,_=ui_rng(seed,0,2,4);variant=(draw+2)%3 if draw==previous else draw;ex=call(0x4A6C50,ident);record('all_stock_talking_variants_no_immediate_repeat_existing_handle',dict(portrait=ident,previous=previous,seed=list(seed),synthetic_nonnull_smk_handle=True),dict(variants=[v['variant'] for v in ex['variant_observations']],previous=r(0x6AB3EC,1),rng=[r(x) for x in (0x50D7CC,0x50D7D0,0x50D7D4)]),dict(variants=[variant],previous=variant,rng=list(state)),ex)
 # All portrait/SFX DAT associations use the unchanged original TBL lookup.
 for dat,tblpath,width,count,properties in (('arr\\portdata.dat','arr\\portdata.tbl',4,110,2),('arr\\sfxdata.dat','arr\\sfxdata.tbl',4,1144,1)):
  b=assets[tblpath];u.mem_write(WAV,b);n=struct.unpack_from('<H',b)[0]
  for prop in range(properties):
   for ident in range(count):
    index=struct.unpack_from('<I',assets[dat],4*(prop*count+ident))[0];expected=0 if not index else WAV+struct.unpack_from('<H',b,2*index)[0] if index<=n else 0x4EC14E;ex=call(0x4C13B0,WAV,index);record('all_stock_portrait_sfx_dat_tbl_associations',dict(dat=dat,table=tblpath,property=prop,row=ident,string_id=index),ex['return_eax'],expected,ex)
 # These two complete wrapper getters have independently identified native writers.
 for table,global_address,entry in (('rez\\network.tbl',0x665DE0,0x43F0C0),('rez\\stat_txt.tbl',0x6AB4F8,0x4A7D70)):
  b=assets[table];u.mem_write(WAV,b);w(global_address,WAV);n=struct.unpack_from('<H',b)[0]
  for index in list(range(1,n+1))+[0,n+1,65535,65536,65537]:
   effective=index&65535;expected=0 if not effective else WAV+struct.unpack_from('<H',b,2*effective)[0] if effective<=n else 0x4EC14E;ex=call(entry,index);record('native_message_getter_actual_table_identity',dict(table=table,global_address=hex(global_address),input_id=index),ex['return_eax'],expected,ex)
 # All stock sound IDs read exact cached duration and unsigned truncation.
 for ident in range(1144):
  for duration in (0,999,1000,1001,0xFFFFFFFF):
   w(0x6A61D8+ident*16,duration);ex=call(0x446740,ident);record('all_stock_sound_cached_duration_whole_second_rounding',dict(sound=ident,cached_duration=duration),ex['return_eax'],duration//1000*1000,ex)
 # Complete fixed/sequential sound branches; random branch stops before CRT.
 for ident in range(228):
  first=r(0x522578+ident*2,2) if ident<=105 else 0;last=r(0x522650+ident*2,2) if ident<=105 else 0
  if first==0 or first==last:
   w(0x66697C,123);ex=call(0x447130,ident);record('stock_yes_fixed_or_absent',dict(type=ident),[ex['return_eax'],r(0x66697C)],[first,first if first else 123],ex)
  what=r(0x522038+ident*2,2);lastwhat=r(0x522200+ident*2,2)
  if what==lastwhat:
   w(0x66697C,123);ex=call(0x447190,ident,0);record('stock_what_fixed',dict(type=ident),[ex['return_eax'],r(0x66697C)],[what,what],ex)
  if ident<=105:
   first=r(0x5223C8+ident*2,2);last=r(0x5224A0+ident*2,2)
   if first:
    for index in range(1,last-first+2):
     w(0x66697C,123);w(0x667270,17);ex=call(0x447190,ident,index);record('all_stock_sequential_annoyed_sounds',dict(type=ident,annoyed_index=index,first=first,last=last),[ex['return_eax'],r(0x66697C),r(0x667270)],[first+index-1,123,17],ex)
 for entry,typ,sequence,stop in ((0x447130,0,0,0x4D14D4),(0x447190,0,0,0x4D14D4),(0x447190,0,8,0x4D14D4)):
  w(0x667270,17);ex=call(entry,typ,sequence,stop_before=stop);record('crt_random_sound_import_boundary',dict(entry=hex(entry),type=typ,sequence=sequence),[ex['stopped_before_unexecuted_callee'],r(0x667270)],[True,0 if sequence==8 else 17],ex)
 # Complete native parser on 15 genuine RIFF/WAVE inputs, and bounded failures.
 for path,b in assets.items():
  if not path.lower().endswith('.wav'):continue
  u.mem_write(WAV,b)
  for length in (len(b),len(b)-1,11,12):
   w(OUTPUT,length);u.mem_write(OUTPUT+32,bytes([0xA5]*18));want,remaining,fmt=native_wav_model(b,length);ex=call(0x49D830,WAV,OUTPUT,(OUTPUT+32,));record('stock_wav_parser_lengths',dict(path=path,sha256=sha(b),input_length=length),dict(payload_offset=ex['return_eax']-WAV if ex['return_eax'] else 0,remaining=r(OUTPUT),format_sha256=sha(bytes(u.mem_read(OUTPUT+32,18)))),dict(payload_offset=want,remaining=remaining,format_sha256=sha(fmt)),ex)
 fmt=struct.pack('<HHIIHH',1,1,22050,44100,2,16)
 def riff(parts,advertised=None):
  body=b'WAVE'+b''.join(tag+struct.pack('<I',len(v))+v for tag,v in parts);return b'RIFF'+struct.pack('<I',len(body) if advertised is None else advertised)+body
 synthetic=[('normal',riff([(b'fmt ',fmt),(b'data',b'1234')])),('wrong_riff',b'XXXX'+riff([(b'fmt ',fmt),(b'data',b'1234')])[4:]),('wrong_wave',riff([(b'fmt ',fmt),(b'data',b'1234')])[:8]+b'XXXX'+riff([(b'fmt ',fmt),(b'data',b'1234')])[12:]),('short_fmt',riff([(b'fmt ',fmt[:15]),(b'data',b'1234')])),('fmt_extension',riff([(b'fmt ',fmt+b'abcd'),(b'data',b'1234')])),('odd_unpadded_junk',riff([(b'JUNK',b'x'),(b'fmt ',fmt),(b'data',b'1234')])),('odd_padded_junk',riff([(b'JUNK',b'x\0'),(b'fmt ',fmt),(b'data',b'1234')])[:17]+b'\0'+riff([(b'JUNK',b'x\0'),(b'fmt ',fmt),(b'data',b'1234')])[18:]),('size_ignored',riff([(b'fmt ',fmt),(b'data',b'1234')],0)),('data_before_fmt',riff([(b'data',b'1234'),(b'fmt ',fmt)])),('empty_data',riff([(b'fmt ',fmt),(b'data',b'')])),('zero_rate',riff([(b'fmt ',struct.pack('<HHIIHH',99,0,0,0,0,0)),(b'data',b'1234')]))]
 # Correct standard odd padding: declaration1, physical payload2.
 synthetic[6]=('odd_standard_padded_junk',riff([(b'JUNK',b'x'),(b'fmt ',fmt),(b'data',b'1234')])[:21]+b'\0'+riff([(b'JUNK',b'x'),(b'fmt ',fmt),(b'data',b'1234')])[21:])
 for name,b in synthetic:
  u.mem_write(WAV,b+bytes(100));w(OUTPUT,len(b));u.mem_write(OUTPUT+32,bytes([0xA5]*18));want,remaining,fmtout=native_wav_model(b,len(b));ex=call(0x49D830,WAV,OUTPUT,(OUTPUT+32,));record('native_wav_format_mutation_boundaries',dict(profile=name,input_length=len(b)),[ex['return_eax']-WAV if ex['return_eax'] else 0,r(OUTPUT),sha(bytes(u.mem_read(OUTPUT+32,18)))],[want,remaining,sha(fmtout)],ex)
 # Explicitly partial original duration arithmetic, after the unexecuted COM query.
 # ESI is a supplied SoundData prefix, ECX is supplied nAvgBytesPerSec; the
 # original imul/xor/pop/div runs, then stops before the caller-frame restore.
 duration_inputs=[(path,source.wav(b)['data_bytes'],source.wav(b)['format'][3]) for path,b in assets.items() if path.lower().endswith('.wav')]
 duration_inputs += [('synthetic_overflow',n,rate) for n in (0,1,4294967,4294968,0x7FFFFFFF,0xFFFFFFFF) for rate in (1,22050,44100)]
 for label,n,rate in duration_inputs:
  w(OUTPUT,n);ex=call(0x49DAA0,rate,stop_before=0x49DAAB,stop_kind='instruction',esi=OUTPUT);record('native_duration_arithmetic_fragment_after_unexecuted_com_query',dict(resource=label,buffer_bytes=n,device_average_bytes_per_second_supplied=rate,entry_registers=dict(esi=OUTPUT,ecx=rate),synthetic_saved_esi_stack=True,full_producer_not_executed=True),ex['return_eax'],((n*1000)&MASK)//rate,ex)
 # Complete eight-channel choice without COM: each channel is empty or queued.
 for queued in range(256):
  u.mem_write(0x6AA990,bytes(8*24))
  for i in range(8):w(0x6AA996+i*24,8 if queued&(1<<i) else 0,1)
  ex=call(0x49DB60,99);available=[i for i in range(8) if not queued&(1<<i)];want=available[-1] if available else MASK;record('eight_channel_queued_masks_last_free',dict(queued_mask=queued),ex['return_eax'],want,ex)
 # Every sound with the runtime queued bit is rejected before loading/playing.
 for ident in range(1144):
  stockflags=assets['arr\\sfxdata.dat'][1144*5+ident];w(0x51D8C8+ident,stockflags|8,1);w(0x6AA954,PRIVATE+0x700);w(0x6D3140,100);u.mem_write(0x6AA990,bytes(8*24));ex=call(0x49E4E0,ident,0,(0,0));record('all_stock_sfx_already_queued_rejected',dict(sound=ident,stock_flags=stockflags,runtime_queued_bit=True),ex['return_eax'],0,ex);w(0x51D8C8+ident,stockflags,1)
 for device in (0,PRIVATE+0x700):
  for volume in (0,100):
   w(0x6AA954,device);w(0x6D3140,volume);ex=call(0x49D810);record('audio_device_and_master_volume_gate',dict(device_present=bool(device),master_volume=volume),ex['return_eax'],int(bool(device and volume)),ex)
 # Complete stock preload-list initialization with audio-device gate disabled.
 lists={0x666F50:list(range(1,114))+list(range(473,755))+[0],0x666C10:list(range(1,114))+list(range(174,473))+[0],0x666980:list(range(1,114))+list(range(755,968))+[0]}
 for initialized in (0,1):
  w(0x666978,initialized);w(0x6AA954,0);w(0x5264C2,0,1)
  for address,values in lists.items():u.mem_write(address,bytes([0xA5]*(len(values)*2)))
  for address in range(0x66726C,0x667284,4):w(address,17)
  ex=call(0x446800,0);expected={hex(address):sha(struct.pack('<'+'H'*len(values),*values)) if not initialized else sha(bytes([0xA5]*(len(values)*2))) for address,values in lists.items()};actual={hex(address):sha(bytes(u.mem_read(address,len(values)*2))) for address,values in lists.items()};record('native_selection_sound_initialize_lists_once',dict(already_initialized=initialized,audio_device_present=False,preload_requested=False),dict(lists=actual,initialized=r(0x666978),selection_state=[r(x) for x in range(0x66726C,0x667284,4)]),dict(lists=expected,initialized=1,selection_state=[0]*6),ex)
 # All stock type IDs enter complete owner/replay/cooldown predicates.
 # Accepted units have a genuine positive cooldown, so no sound random/device path runs.
 unit=PRIVATE+0x500
 for typ in range(228):
  for owner in (0,1,11):
   for replay in (0,1):
    u.mem_write(unit,bytes(0x150));w(unit+0x64,typ,2);w(unit+0x4C,owner,1);w(0x508D50,0);w(0x67CC90,replay);w(0x667280,0);w(0x66726C,unit);w(0x667270,7);w(0x667274,1);w(0x66697C,123)
    allowed=not replay and (owner==0 or owner==11 and typ in (176,177,178,188,89,90,93,94,95,96));ex=call(0x446C30,unit);record('all_stock_selection_sound_owner_replay_cooldown',dict(type=typ,owner=owner,local_player=0,replay=bool(replay),same_previous_unit=True,positive_cooldown=1),[r(0x66726C),r(0x667270),r(0x667274),r(0x66697C)],[unit if allowed else 0,7 if allowed else 0,1,123],ex)
 for quiet in (0,1):
  w(0x667280,quiet);w(0x66726C,unit);w(0x667270,7);w(0x667274,1);ex=call(0x446C30,0);record('selection_sound_null_and_quiet_state',dict(unit_is_null=True,quiet=bool(quiet)),[r(0x66726C),r(0x667270)],[unit if quiet else 0,7 if quiet else 0],ex)
 # Actual timer callback's pure fade branch; no decode/device or clock call.
 for visible in (0,8):
  for loaded in (0,PRIVATE+0x700):
   for fresh in (0,1):
    for fade in (0,49,50,79,80,255):
     portrait_fixture((0,0,0));w(CONTROL+0x18,1|visible);w(0x6AB390,loaded);w(0x6AB38C,fresh,1);w(0x6AB3A1,fade,1);ex=call(0x4A7030,CONTROL,2);want=(fade+10)&255 if visible and loaded and not fresh and fade<80 else fade;record('portrait_timer_fade_transition',dict(visible=bool(visible),loaded=bool(loaded),fresh=fresh,fade=fade,already_dirty=True),[r(0x6AB3A1,1),r(0x6AB38C,1)],[want,fresh],ex)
 for mode in (0,1,2):
  for fresh in (0,1):
   w(0x50D7C8,mode);w(0x6AB394,fresh);ex=call(0x4A6700,PRIVATE+0x700,0,(OUTPUT,),stop_before=0x4D0184 if mode==2 or fresh else None);expected=0 if mode==0 else 1 if mode==1 and not fresh else None
   record('portrait_decode_mode_gate',dict(mode=mode,first_frame=fresh),ex['return_eax'] if ex['complete_return'] else 'import_boundary',expected if expected is not None else 'import_boundary',ex)
 for mode in (0,1,2,3,0xFFFFFFFF):
  w(0x6AB39C,0);ex=call(0x4A6E90,mode);record('portrait_mode_unsigned_clamp_without_control',dict(mode=mode),r(0x50D7C8),min(mode,2),ex)
 for duration in (0,1,999,1000,0xFFFFFFFF):
  w(0x6AB3D0,1);ex=call(0x4A7200,0,duration,(1,17,287));record('trigger_portrait_suppresses_talking_request',dict(duration=duration,trigger_portrait_active=True),[r(0x6AB3D0),ex['complete_return']],[1,True],ex)
 # File control bounds enter genuine pure hit/relative-coordinate routines.
 for path,b in assets.items():
  if not path.lower().endswith('.bin'):continue
  widgets=source.bins(b);root=widgets[0]['raw_bounds']
  for widget in widgets:
   bounds=widget['raw_bounds'];x1,y1,x2,y2=map(signed16,bounds);points=[(x1,y1),(x2,y2),(x1-1,y1),(x2+1,y2),(x1,y1-1),(x2,y2+1),(-32768,32767)]
   for x,y in points:
    u.mem_write(CONTROL,bytes(0x60));u.mem_write(CONTROL+4,struct.pack('<4H',*bounds));w(EVENT+14,x,2);w(EVENT+16,y,2);ex=call(0x4C29A0,CONTROL,EVENT);want=int(x1<=signed16(x)<=x2 and y1<=signed16(y)<=y2);record('stock_dialog_raw_bound_inclusive_signed_hit',dict(path=path,widget_offset=widget['offset'],identifier=widget['identifier'],raw_bounds=bounds,point=[x,y],native_loader_not_executed=True),ex['return_eax'],want,ex)
    w(CONTROL+0x22,widget['type'],2);w(CONTROL+0x32,PARENT);u.mem_write(PARENT+4,struct.pack('<4H',*root));origin=root[:2] if widget['type'] else bounds[:2];ex=call(0x4C29D0,CONTROL,EVENT,(OUTPUT,));record('stock_dialog_relative_point_parent_selection',dict(path=path,widget_offset=widget['offset'],type=widget['type'],point=[x,y],parent_fixture='file root origin; not a native loader result'),[r(OUTPUT,2),r(OUTPUT+2,2)],[(x-origin[0])&65535,(y-origin[1])&65535],ex)
 result=dict(target_sha256=source.HASH,target_version='1.17.0.1',original_code_executed=True,complete_game_executed=False,imports_executed=False,stubs=[],probe_script_sha256=sha(Path(__file__).read_bytes()),inspector_sha256=sha(Path(source.__file__).read_bytes()),asset_manifest_sha256=sha(a.asset_manifest.read_bytes()),runtime_architecture=platform.machine(),python_version=sys.version,unicorn_version=unicorn.__version__,cases=cases,case_count=len(cases),complete_case_count=sum(c['execution']['complete_return'] for c in cases),prefix_case_count=sum(not c['execution']['complete_return'] for c in cases),arithmetic_fragment_case_count=sum(c['case']=='native_duration_arithmetic_fragment_after_unexecuted_com_query' for c in cases),case_families=dict(Counter(c['case'] for c in cases)),visited_instruction_count=len(visited),entry_visits=dict(entry_counts),guarded_guest_writes=writes,limits=['Private native-layout fixtures are not complete game initialization','Portrait variant observation is a native intermediate PC under a synthetic nonzero handle guard; no movie opens/decodes','Sound cached duration values are supplied; COM-based original duration producer is not executed','Duration arithmetic fragment starts after the unexecuted COM query and stops before frame restoration; it is not a complete function call','Channel masks cover empty/queued state, not live COM buffers/preemption','BIN raw bounds are file inputs; loader normalization and event dispatch remain distinct','CRT random sound branches and SMK draw calls stop before imports','Native resource loader/writer paths are static; table getters receive authenticated private inputs directly'])
 a.out.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({k:result[k] for k in ('case_count','complete_case_count','prefix_case_count','visited_instruction_count','guarded_guest_writes','probe_script_sha256')}))
if __name__=='__main__':main()
