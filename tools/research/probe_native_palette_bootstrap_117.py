#!/usr/bin/env python3
"""Execute original all-tileset cycle/bootstrap and SSE RGB brightness contracts."""
import argparse,hashlib,json,math,platform,struct,sys
from collections import Counter
from pathlib import Path
import capstone,pefile
import inspect_native_palette_bootstrap_117 as source
PRIVATE,STACK,STOP=0x800000,0x900000,0xA00000
INPUT,OUTPUT=PRIVATE,PRIVATE+0x1000
CYCLES,CURRENT=0x6D2F88,0x6D2388
def sha(b):return hashlib.sha256(b).hexdigest()
def main():
 p=argparse.ArgumentParser(description=__doc__)
 for key in ('exe','assets','asset-manifest'):p.add_argument('--'+key,type=Path,required=True)
 p.add_argument('--out',type=Path);p.add_argument('--execute-original-code',action='store_true');a=p.parse_args();raw=a.exe.read_bytes();assert sha(raw)==source.HASH;deps=source.dependencies();deps[Path(source.__file__).name]=sha(Path(source.__file__).read_bytes())
 if not a.execute_original_code:print(json.dumps(dict(original_code_executed=False,target_sha256=source.HASH)));return
 assert a.out and platform.machine().lower() in ('x86_64','amd64')
 import unicorn
 from unicorn import Uc,UC_ARCH_X86,UC_MODE_32,UC_HOOK_CODE
 from unicorn.x86_const import UC_X86_REG_ECX,UC_X86_REG_EDX,UC_X86_REG_EAX,UC_X86_REG_ESP,UC_X86_REG_EIP,UC_X86_REG_EFLAGS,UC_X86_REG_MXCSR,UC_X86_REG_FPCW
 pe=pefile.PE(data=raw);u=Uc(UC_ARCH_X86,UC_MODE_32);u.mem_map(0x400000,(pe.OPTIONAL_HEADER.SizeOfImage+4095)&~4095);u.mem_write(0x400000,pe.get_memory_mapped_image());u.mem_map(PRIVATE,0x10000);u.mem_map(STACK,0x10000);u.mem_map(STOP,4096)
 def w(addr,val,n=4):u.mem_write(addr,(val&((1<<(n*8))-1)).to_bytes(n,'little'))
 def r(addr,n=4):return int.from_bytes(u.mem_read(addr,n),'little')
 w(0x6D319C,2);w(0x50F310,0)
 ranges=[row for key,row in source.BLOCKS.items() if key not in ('palette_device_static','palette_fade_clock_boundary')];entries={lo for lo,hi in ranges};approved=set();visited=set();cases=[]
 def call(entry,ecx=0,edx=0,args=(),stop_before=None):
  done=False;prefix=False;steps=0;calls=[]
  def hook(machine,pc,n,unused):
   nonlocal done,prefix,steps
   if pc==STOP:done=True;machine.emu_stop();return
   if stop_before is not None and pc==stop_before:prefix=True;machine.emu_stop();return
   if (pc,n) not in approved:assert any(lo<=pc and pc+n<=hi for lo,hi in ranges),('Outside reviewed function',hex(pc),calls[-8:]);approved.add((pc,n))
   visited.add(pc);steps+=1
   if pc in entries:calls.append(hex(pc))
  h=u.hook_add(UC_HOOK_CODE,hook);sp=STACK+0xF000;u.mem_write(sp,struct.pack('<'+'I'*(len(args)+1),STOP,*args));u.reg_write(UC_X86_REG_ESP,sp);u.reg_write(UC_X86_REG_ECX,ecx&0xFFFFFFFF);u.reg_write(UC_X86_REG_EDX,edx&0xFFFFFFFF);u.reg_write(UC_X86_REG_EAX,0);u.reg_write(UC_X86_REG_EFLAGS,2);u.reg_write(UC_X86_REG_MXCSR,0x1F80);u.reg_write(UC_X86_REG_FPCW,0x027F)
  try:u.emu_start(entry,STOP+1,count=2000000)
  except Exception as e:raise RuntimeError((hex(entry),hex(u.reg_read(UC_X86_REG_EIP)),calls[-8:])) from e
  finally:u.hook_del(h)
  assert done or prefix
  if done:assert u.reg_read(UC_X86_REG_ESP)==sp+4*(len(args)+1)
  return dict(complete_return=done,stopped_before_upload=prefix and stop_before in (0x4CF8C0,0x4CFD60),stopped_before_unexecuted_callee=prefix,stop_va=hex(stop_before) if prefix else None,original_instructions=steps,calls=calls,return_eax=u.reg_read(UC_X86_REG_EAX))
 def record(family,inputs,actual,expected,ex):
  assert actual==expected,(family,inputs,actual,expected);cases.append(dict(case=family,inputs=inputs,actual=actual,expected=expected,passed=True,execution=ex))
 am=json.loads(a.asset_manifest.read_text());palettes={}
 for path,row in am['assets'].items():
  b=(a.assets/path.replace('\\','/')).read_bytes();assert len(b)==1024 and sha(b)==row['sha256'];palettes[path]=b
 definitions=source.definitions(pe)
 def setup(tileset,marker=0xA5):
  w(0x5264BC,tileset,2);u.mem_write(CYCLES,bytes([marker]*128));ex=call(0x46A390);want=bytearray(128);definition=definitions[tileset]
  for i,row in enumerate(definition['cycles']):want[i*16:i*16+6]=bytes([int(bool(definition['active_mask']&(1<<i))),row['speed'],row['speed'],row['low'],0,row['high']]);struct.pack_into('<I',want,i*16+8,int(row['pattern_pointer'],16));want[i*16+12]=row['count']
  return ex,want
 for d in definitions:
  tileset=d['tileset'];palette=palettes['tileset\\'+d['name']+'.wpe']
  for marker in (0,0xA5,0xFF):
   ex,want=setup(tileset,marker);record('all_tileset_stock_or_mapped_zero_bootstrap',dict(tileset=tileset,name=d['name'],initial_cycle_byte=marker,definition=d),dict(cycles_sha256=sha(bytes(u.mem_read(CYCLES,128)))),dict(cycles_sha256=sha(want)),ex)
  for index in range(-1,258):
   w(0x5264BC,tileset,2);ex=call(0x4697C0,index);expected=int(any(row['low']<=index<=row['high'] for row in d['cycles']));record('all_tileset_cycle_index_membership_signed_full_argument',dict(tileset=tileset,index=index,mapped_zero_profile=d['mapped_pe_zero_only']),dict(member=ex['return_eax']),dict(member=expected),ex)
  for row_index,row in enumerate(d['cycles']):
   ex,want_cycles=setup(tileset);u.mem_write(CURRENT,palette);lo,hi=row['low'],row['high'];want=bytearray(palette);want[lo*4:(hi+1)*4]=palette[hi*4:(hi+1)*4]+palette[lo*4:hi*4];rotate=call(0x4CCCF0,row_index);record('all_stock_tileset_positive_simple_cycle_actual_wpe',dict(tileset=tileset,cycle_index=row_index,low=lo,high=hi,wpe_sha256=sha(palette)),dict(current_palette_sha256=sha(bytes(u.mem_read(CURRENT,1024))),cycles_sha256=sha(bytes(u.mem_read(CYCLES,128)))),dict(current_palette_sha256=sha(want),cycles_sha256=sha(want_cycles)),dict(complete_return=True,original_instructions=ex['original_instructions']+rotate['original_instructions'],calls=ex['calls']+rotate['calls']))
  ex,want_cycles=setup(tileset);u.mem_write(CURRENT,palette)
  for invocation in range(7):
   for i,row in enumerate(d['cycles']):want_cycles[i*16+2]=(row['speed']-invocation-1)&255
   driver=call(0x4CCD30);record('all_tileset_driver_seven_nonexpiry_invocations',dict(tileset=tileset,isolated_invocation=invocation+1,clock_cadence_not_claimed=True),dict(cycles_sha256=sha(bytes(u.mem_read(CYCLES,128))),current_palette_sha256=sha(bytes(u.mem_read(CURRENT,1024)))),dict(cycles_sha256=sha(want_cycles),current_palette_sha256=sha(palette)),driver)
  # Empty definitions finish the eighth invocation; positive profiles stop
  # before submission, after native first-pass ranges actually rotate.
  want=bytearray(palette);positive=False
  for i,row in enumerate(d['cycles']):
   if row['low']>128:break
   want_cycles[i*16+2]=row['speed'];lo,hi=row['low'],row['high'];want[lo*4:(hi+1)*4]=palette[hi*4:(hi+1)*4]+palette[lo*4:hi*4];positive=True
  driver=call(0x4CCD30,stop_before=0x4CF8C0);record('all_tileset_eighth_driver_call_complete_empty_or_positive_upload_prefix',dict(tileset=tileset,isolated_invocation=8,mapped_zero_profile=d['mapped_pe_zero_only']),dict(cycles_sha256=sha(bytes(u.mem_read(CYCLES,128))),current_palette_sha256=sha(bytes(u.mem_read(CURRENT,1024))),stopped_before_upload=driver['stopped_before_upload']),dict(cycles_sha256=sha(want_cycles),current_palette_sha256=sha(want),stopped_before_upload=positive),driver)
  for i,row in enumerate(d['cycles']):
   if row['low']<=128:continue
   setup(tileset);u.mem_write(CURRENT,palette);w(CYCLES+i*16+2,1,1);want_cycles=bytearray(u.mem_read(CYCLES,128));want_cycles[i*16+2]=row['speed'];want=bytearray(palette);lo,hi=row['low'],row['high'];want[lo*4:(hi+1)*4]=palette[hi*4:(hi+1)*4]+palette[lo*4:hi*4];ex=call(0x4CCBC0,CYCLES+i*16,i,(256,),stop_before=0x4CF8C0)
   record('all_stock_high_palette_second_pass_positive_upload_prefix',dict(tileset=tileset,cycle_index=i,low=lo,high=hi,limit=256),dict(cycles_sha256=sha(bytes(u.mem_read(CYCLES,128))),current_palette_sha256=sha(bytes(u.mem_read(CURRENT,1024))),stopped_before_upload=ex['stopped_before_upload']),dict(cycles_sha256=sha(want_cycles),current_palette_sha256=sha(want),stopped_before_upload=True),ex)
 # The original RGB routine preserves destination byte3 when applying gamma.
 # Brightness100 instead copies all4bytes through real native memmove.
 def gamma_case(tag,b,brightness,count):
  initial=bytes([0xA7]*1024);u.mem_write(INPUT,b);u.mem_write(OUTPUT,initial);setter=call(0x4CF930,brightness);ex=call(0x4CF760,OUTPUT,INPUT,(count,));want=bytearray(initial)
  if brightness==100:want[:count*4]=b[:count*4]
  else:
   power=(200-brightness)/100
   for i in range(count):
    for channel in range(3):want[i*4+channel]=int(math.pow(b[i*4+channel]/256,power)*256)&255
  record('complete_native_rgb_gamma_sse_pow_or_identity_copy',dict(profile=tag,brightness=brightness,count=count,input_sha256=sha(b),destination_initial_byte=0xA7,mxcsr=hex(0x1F80),fpu_control=hex(0x027F)),dict(output_sha256=sha(bytes(u.mem_read(OUTPUT,1024))),input_sha256=sha(bytes(u.mem_read(INPUT,1024)))),dict(output_sha256=sha(want),input_sha256=sha(b)),dict(complete_return=True,original_instructions=setter['original_instructions']+ex['original_instructions'],calls=setter['calls']+ex['calls']))
 gray=bytes(v for v in range(256) for unused in range(4))
 for brightness in range(60,141):gamma_case('all_256_grayscale_bytes',gray,brightness,256)
 for d in definitions:
  b=palettes['tileset\\'+d['name']+'.wpe']
  for brightness in (60,80,100,120,140):gamma_case('stock_'+d['name']+'_wpe',b,brightness,256)
 for count in (0,1,7):
  for brightness in (60,100,140):gamma_case('short_count_grayscale',gray,brightness,count)
 # Full original upload preparation, stopped at device adapter entry before
 # it can invoke Storm. Stack destination byte3 is explicitly authored A7.
 b=palettes['tileset\\badlands.wpe'];u.mem_write(INPUT,b)
 for first,count in ((0,256),(1,13),(248,7),(0,0)):
  for brightness in (60,80,100,120,140):
   u.mem_write(STACK,bytes([0xA7]*0x10000));call(0x4CF930,brightness);ex=call(0x4CF8C0,INPUT+first*4,first,(count,),stop_before=0x4CFD60);actual_ptr=u.reg_read(UC_X86_REG_ECX);expected_ptr=INPUT+first*4 if brightness==100 else STACK+0xF000-0x404+first*4;want=bytearray(b[first*4:(first+count)*4] if brightness==100 else bytes([0xA7]*count*4))
   if brightness!=100:
    for i in range(count):
     for channel in range(3):want[i*4+channel]=int(math.pow(b[(first+i)*4+channel]/256,(200-brightness)/100)*256)&255
   record('original_upload_preparation_rgb_and_slice_pointer_before_device',dict(brightness=brightness,first=first,count=count,stack_initial_byte=0xA7),dict(device_input_pointer=actual_ptr,device_first=u.reg_read(UC_X86_REG_EDX),payload_sha256=sha(bytes(u.mem_read(actual_ptr,count*4))) if count else sha(b''),input_sha256=sha(bytes(u.mem_read(INPUT,1024)))),dict(device_input_pointer=expected_ptr,device_first=first,payload_sha256=sha(want),input_sha256=sha(b)),ex)
 # Native grayscale conversion reaches its real fade routine, whose first
 # clock call is imported. Stop before that routine instead of supplying time.
 profiles=[('stock_'+d['name'],palettes['tileset\\'+d['name']+'.wpe']) for d in definitions]
 profiles += [('single_channel_'+str(channel),bytes(v if c==channel else 0 for v in range(256) for c in range(4))) for channel in range(4)]
 for tag,b in profiles:
  u.mem_write(CURRENT,b);u.mem_write(0x6D2B88,bytes([0xA7]*1024));ex=call(0x4CCF40,stop_before=0x4CCD60);want=bytearray(b)
  for i in range(256):v=b[i*4]*77//256+b[i*4+1]*151//256+b[i*4+2]*28//256;want[i*4:i*4+3]=bytes([v]*3)
  record('original_grayscale_component_floor_and_fourth_byte_before_clock_fade',dict(profile=tag,input_sha256=sha(b)),dict(target_palette_sha256=sha(bytes(u.mem_read(0x6D2B88,1024))),current_palette_sha256=sha(bytes(u.mem_read(CURRENT,1024))),fade_steps_argument=u.reg_read(UC_X86_REG_ECX)),dict(target_palette_sha256=sha(want),current_palette_sha256=sha(b),fade_steps_argument=0),ex)
 # Transition guards return completely; active transitions preserve their
 # measured preparation and stop before real upload/time dependencies.
 b=palettes['tileset\\badlands.wpe'];from_initial=bytes([0xA7]*1024);target_initial=bytes([0x5A]*1024)
 for entry in (0x4CCE50,0x4CCEB0):
  for black_state in (0,1):
   u.mem_write(CURRENT,b);u.mem_write(0x6D2788,from_initial);u.mem_write(0x6D2B88,target_initial);w(0x50F0F0,black_state,1);positive=(entry==0x4CCE50 and black_state==1) or (entry==0x4CCEB0 and black_state==0);boundary=0x4CF8C0 if entry==0x4CCE50 else 0x4CCD60;ex=call(entry,7,stop_before=boundary);want_current=bytes(1024) if positive and entry==0x4CCE50 else b;want_from=b if positive and entry==0x4CCEB0 else from_initial;want_target=bytes(1024) if positive and entry==0x4CCEB0 else target_initial;want_state=1-black_state if positive else black_state
   record('original_black_fade_transition_guard_or_preparation_prefix',dict(entry=hex(entry),old_black_state=black_state,positive_transition=positive),dict(state=r(0x50F0F0,1),current_sha256=sha(bytes(u.mem_read(CURRENT,1024))),from_sha256=sha(bytes(u.mem_read(0x6D2788,1024))),target_sha256=sha(bytes(u.mem_read(0x6D2B88,1024)))),dict(state=want_state,current_sha256=sha(want_current),from_sha256=sha(want_from),target_sha256=sha(want_target)),ex)
 complete=sum(row['execution']['complete_return'] for row in cases);result=dict(target_sha256=source.HASH,target_version='1.17.0.1',original_code_executed=True,complete_game_executed=False,probe_script_sha256=sha(Path(__file__).read_bytes()),source_dependencies_sha256=deps,asset_manifest_sha256=sha(a.asset_manifest.read_bytes()),assets_sha256={path:sha(b) for path,b in palettes.items()},tileset_definitions=definitions,runtime=dict(architecture=platform.machine(),python=sys.version,unicorn=unicorn.__version__,capstone=capstone.__version__,pefile=pefile.__version__),helper_stubs=[],imports_executed=[],instruction_bound=2000000,cases_passed=len(cases),complete_original_function_cases=complete,prefix_cases=len(cases)-complete,executed_instruction_witnesses=len(visited),cases=cases,limits=['Private paletteRGB/current-cycle buffers, no presented renderer or visual parity','Platform/install definition66fd40 uses mappedPE zero bytes; no post-loader mutation invariant claimed','Positive scheduler updates stop before native device submission4cf8c0; no helper replaced','SSE CRTpow profile fixes MXCSR1f80/FPCW027f; fallback4e0824 and exception/errno routes are unexecuted','WPE bytes are stock inputs; all scheduler calls are isolated invocations, not clock cadence'])
 a.out.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(dict(cases_passed=len(cases),complete=complete,prefixes=len(cases)-complete,pcs=len(visited),families=dict(Counter(row['case'] for row in cases)),script_sha256=result['probe_script_sha256'])))
if __name__=='__main__':main()
