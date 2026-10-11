#!/usr/bin/env python3
"""Compose deterministic font format steps with complete original crypto leaves.

Retail keys and all derived intermediate states stay private. The original
decoder/key wrappers and CRT/TLS/time imports are never executed or stubbed.
"""
import argparse,hashlib,json,platform,struct,sys
from collections import Counter
from pathlib import Path
import capstone,pefile
import inspect_native_font_decoder_117 as source
PRIVATE,STACK,STOP=0x800000,0x900000,0xA00000
HASH_INITIAL=[0x67452301,0xEFCDAB89,0x98BADCFE,0x10325476,0xC3D2E1F0]
def sha(b):return hashlib.sha256(b).hexdigest()
def crt_bytes(seed,count,skip_zero=False):
 result=bytearray()
 while len(result)<count:
  seed=(seed*0x343FD+0x269EC3)&0xFFFFFFFF;value=(seed>>16)&255
  if value or not skip_zero:result.append(value)
 return bytes(result)
def signed_rotate(value,bits):
 signed=value if value<0x80000000 else value-0x100000000
 return ((value<<bits)|(signed>>(32-bits)))&0xFFFFFFFF
def hash_block(state,b):
 assert len(b)==64;words=list(struct.unpack('<16I',b))
 for n in range(16,80):words.append(words[n-3]^words[n-8]^words[n-14]^words[n-16])
 a,c,d,e,f=state
 for n in range(80):
  if n<20:mix=(c&d)|((~c)&e);constant=0x5A827999
  elif n<40:mix=c^d^e;constant=0x6ED9EBA1
  elif n<60:mix=(c&d)|(c&e)|(d&e);constant=0x8F1BBCDC
  else:mix=c^d^e;constant=0xCA62C1D6
  temp=(signed_rotate(a,5)+mix+f+constant+words[n])&0xFFFFFFFF;a,c,d,e,f=temp,a,signed_rotate(c,30),d,e
 return [(old+new)&0xFFFFFFFF for old,new in zip(state,(a,c,d,e,f))]
def idea_expand(key):
 words=list(struct.unpack('<8H',key))
 while len(words)<52:
  group=words[-8:];words.extend(((group[(i+1)%8]<<9)|(group[(i+2)%8]>>7))&65535 for i in range(8))
 return words[:52]
def inverse(value):return pow(value if value else 65536,-1,65537)&65535
def idea_inverse(keys):
 out=[inverse(keys[48]),(-keys[49])&65535,(-keys[50])&65535,inverse(keys[51]),keys[46],keys[47]]
 for n in range(1,8):
  at=48-6*n;out += [inverse(keys[at]),(-keys[at+2])&65535,(-keys[at+1])&65535,inverse(keys[at+3]),keys[at-2],keys[at-1]]
 return out+[inverse(keys[0]),(-keys[1])&65535,(-keys[2])&65535,inverse(keys[3])]
def idea_transform(b,keys):
 def mul(a,b):return (((a if a else 65536)*(b if b else 65536))%65537)&65535
 a,b,c,d=struct.unpack('<4H',b)
 for round_index in range(8):
  k=keys[round_index*6:round_index*6+6];a=mul(a,k[0]);b=(b+k[1])&65535;c=(c+k[2])&65535;d=mul(d,k[3]);t0=mul(a^c,k[4]);t1=mul(((b^d)+t0)&65535,k[5]);t0=(t0+t1)&65535;a,b,c,d=a^t1,c^t1,b^t0,d^t0
 return struct.pack('<4H',mul(a,keys[48]),(c+keys[49])&65535,(b+keys[50])&65535,mul(d,keys[51]))
def main():
 p=argparse.ArgumentParser(description=__doc__)
 for key in ('exe','assets','asset-manifest'):p.add_argument('--'+key,type=Path,required=True)
 p.add_argument('--out',type=Path);p.add_argument('--execute-original-code',action='store_true');a=p.parse_args();raw=a.exe.read_bytes();assert sha(raw)==source.HASH
 if not a.execute_original_code:print(json.dumps(dict(original_code_executed=False,target_sha256=source.HASH)));return
 assert a.out and platform.machine().lower() in ('x86_64','amd64')
 import unicorn
 from unicorn import Uc,UC_ARCH_X86,UC_MODE_32,UC_HOOK_CODE,UC_HOOK_MEM_WRITE
 from unicorn.x86_const import UC_X86_REG_ECX,UC_X86_REG_EDX,UC_X86_REG_EAX,UC_X86_REG_ESP,UC_X86_REG_EIP,UC_X86_REG_EFLAGS
 pe=pefile.PE(data=raw);u=Uc(UC_ARCH_X86,UC_MODE_32);u.mem_map(0x400000,(pe.OPTIONAL_HEADER.SizeOfImage+4095)&~4095);u.mem_write(0x400000,pe.get_memory_mapped_image());u.mem_map(PRIVATE,0x10000);u.mem_map(STACK,0x10000);u.mem_map(STOP,4096)
 def w(addr,val,n=4):u.mem_write(addr,(val&((1<<(n*8))-1)).to_bytes(n,'little'))
 w(0x6D319C,2);w(0x50F310,0)
 ranges=[v for key,v in source.BLOCKS.items() if not key.endswith('_static')];entries={lo for lo,hi in ranges};approved=set();visited=set();cases=[];native_calls=Counter();native_writes=0
 def hook_write(machine,access,address,size,value,unused):
  nonlocal native_writes
  assert any(lo<=address and address+size<=hi for lo,hi in ((PRIVATE,PRIVATE+0x10000),(STACK,STACK+0x10000),(0x66CB70,0x66CCA8),(0x6A60A8,0x6A61BC))),'Outside private writable state';native_writes+=1
 u.hook_add(UC_HOOK_MEM_WRITE,hook_write)
 def call(entry,ecx=0,edx=0,args=()):
  done=False;steps=0;calls=[]
  def hook(machine,pc,n,unused):
   nonlocal done,steps
   if pc==STOP:done=True;machine.emu_stop();return
   if (pc,n) not in approved:assert any(lo<=pc and pc+n<=hi for lo,hi in ranges),'Outside reviewed native crypto leaves';approved.add((pc,n))
   visited.add(pc);steps+=1
   if pc in entries:native_calls[hex(pc)]+=1;calls.append(hex(pc))
  h=u.hook_add(UC_HOOK_CODE,hook);sp=STACK+0xF000;u.mem_write(sp,struct.pack('<'+'I'*(len(args)+1),STOP,*args));u.reg_write(UC_X86_REG_ESP,sp);u.reg_write(UC_X86_REG_ECX,ecx);u.reg_write(UC_X86_REG_EDX,edx);u.reg_write(UC_X86_REG_EAX,0);u.reg_write(UC_X86_REG_EFLAGS,2)
  try:u.emu_start(entry,STOP+1,count=1000000)
  finally:u.hook_del(h)
  assert done and u.reg_read(UC_X86_REG_ESP)==sp+4*(len(args)+1),'Incomplete original crypto leaf'
  return dict(complete_return=True,original_instructions=steps,calls=calls,return_eax=u.reg_read(UC_X86_REG_EAX))
 def record(family,inputs,actual,expected,ex):
  # Never interpolate private intermediate bytes in assertion text.
  assert actual==expected,'Native cryptographic contract mismatch';cases.append(dict(case=family,inputs=inputs,actual=actual,expected=expected,passed=True,execution=ex))
 states={};schedules={}
 def init(context):
  ex=call(0x49D0B0,context);states[context]=HASH_INITIAL.copy();assert bytes(u.mem_read(0x6A60A8+context*92,20))==struct.pack('<5I',*states[context]);return ex
 def update(context,b):
  u.mem_write(PRIVATE,b);ex=call(0x49D050,context,PRIVATE,(PRIVATE+0x100,));states[context]=hash_block(states[context],b);assert bytes(u.mem_read(PRIVATE+0x100,20))==struct.pack('<5I',*states[context]),'Private native hash mismatch';return bytes(u.mem_read(PRIVATE+0x100,20)),ex
 def snapshot(context):
  ex=call(0x49D010,context,PRIVATE+0x100);actual=bytes(u.mem_read(PRIVATE+0x100,20));assert actual==struct.pack('<5I',*states[context]),'Private native snapshot mismatch';return actual,ex
 def setup(context,key,mode):
  u.mem_write(PRIVATE+0x200,key);ex=call(0x465380,context,mode,(PRIVATE+0x200,));expected=idea_expand(key);expected=expected if mode else idea_inverse(expected);assert bytes(u.mem_read(0x66CB70+context*104,104))==struct.pack('<52H',*expected),'Private IDEA schedule mismatch';schedules[context]=expected;return ex
 def transform(context,b):
  u.mem_write(PRIVATE+0x300,b);ex=call(0x465360,context,PRIVATE+0x300,(PRIVATE+0x400,));actual=bytes(u.mem_read(PRIVATE+0x400,8));assert actual==idea_transform(b,schedules[context]),'Private IDEA block mismatch';return actual,ex
 # Public synthetic vectors independently discriminate Blizzard's signed
 # rotate and unrotated little-endian SHA0 expansion from standard hashes.
 for context in range(3):
  init(context)
  for tag,b in [('zeros',bytes(64)),('ff',bytes([255]*64)),('ramp',bytes(range(64))),('reverse_ramp',bytes(reversed(range(64))))]:
   digest,ex=update(context,b);record('complete_native_modified_sha0_public_vectors',dict(context=context,profile=tag,input_sha256=sha(b)),dict(digest_hex=digest.hex()),dict(digest_hex=struct.pack('<5I',*states[context]).hex()),ex);snapshot(context)
 for value in list(range(256))+[256,257,1023,1024,32767,32768,65534,65535]:
  ex=call(0x465400,value);record('complete_native_idea_multiplicative_inverse',dict(word=value),dict(inverse=ex['return_eax']),dict(inverse=inverse(value)),ex)
 for context in range(3):
  for tag,key in [('zero',bytes(16)),('ones',bytes([255]*16)),('ramp',bytes(range(16))),('word_boundaries',struct.pack('<8H',0,1,65535,32768,32767,256,257,2))]:
   for mode in (0,1):
    ex=setup(context,key,mode);record('complete_native_idea_schedule_public_vectors',dict(context=context,profile=tag,mode=mode,key_hex=key.hex()),dict(schedule_sha256=sha(bytes(u.mem_read(0x66CB70+context*104,104)))),dict(schedule_sha256=sha(struct.pack('<52H',*schedules[context]))),ex)
    for plain in (bytes(8),bytes([255]*8),bytes(range(8)),struct.pack('<4H',0,1,32768,65535)):
     block,ex=transform(context,plain);record('complete_native_idea_transform_public_vectors',dict(context=context,profile=tag,mode=mode,input_hex=plain.hex()),dict(output_hex=block.hex()),dict(output_hex=idea_transform(plain,schedules[context]).hex()),ex)
 # Orchestration below is host code; every hash/IDEA leaf remains actual
 # native code. Secret inputs, derived hashes and schedules are not logged.
 def prepare(key):
  assert key and 0 not in key,'Invalid private key representation';init(0);digest,ex=update(0,(key*64)[:64]);mixed=bytes(v^digest[n%20] for n,v in enumerate(crt_bytes(0x4FA7,112)))
  for context in range(3):setup(context,mixed[context*16:context*16+16],0);init(context);update(context,mixed[48:112])
 def decode(b,key):
  assert len(b)>8 and (len(b)-8)%64==0,'Invalid encrypted container geometry';prepare(key);out=bytearray();blocks=(len(b)-8)//64;stats=dict(blocks=blocks,idea_blocks=0,hash_updates=0,complete_native_leaf_calls=0)
  for index in range(blocks):
   digest,ex=snapshot(0);block=b[index*64:index*64+64];stats['complete_native_leaf_calls']+=1
   if index%8==0:
    parts=[]
    for at in range(0,64,8):decoded,ex=transform(0,block[at:at+8]);parts.append(decoded);stats['idea_blocks']+=1;stats['complete_native_leaf_calls']+=1
    block=b''.join(parts)
   block=bytes((value-digest[(63-j)%20])&255 for j,value in enumerate(block));out.extend(block)
   if index%16==0:update(0,block);stats['hash_updates']+=1;stats['complete_native_leaf_calls']+=1
  trailer=b[-8:];digest,ex=snapshot(0);stats['complete_native_leaf_calls']+=1;assert not trailer[4] and trailer[:4]==digest[:4],'Private encrypted-font footer verification failed';length=len(out)+trailer[5]-64;assert 0<length<=len(out),'Invalid private decoded length'
  clear1=call(0x465340);clear2=call(0x49CFF0);assert not any(u.mem_read(0x66CB70,312)) and not any(u.mem_read(0x6A60A8,276)),'Native crypto clear mismatch';return bytes(out[:length]),stats
 am=json.loads(a.asset_manifest.read_text());assets={}
 for path,row in am['assets'].items():
  b=(a.assets/path.replace('\\','/')).read_bytes();assert sha(b)==row['sha256'],'Private input provenance mismatch';assets[path]=b
 key,private_stats=decode(assets['font\\font.gid'],crt_bytes(0x150B,19,True));key=key.split(b'\0',1)[0];assert key,'Private key carrier had no payload'
 results=[]
 for name in ('font8','font10','font14','font16','font16x'):
  encrypted='font\\'+name+'.fnt';plain='files\\font\\'+name+'.fnt';decoded,stats=decode(assets[encrypted],key);expected=assets[plain]+b'\0';equal=decoded==expected;assert equal,'Decoded font differs from stock plaintext corpus'
  results.append(dict(font=name,encrypted_path=encrypted,plaintext_path=plain,encrypted_sha256=sha(assets[encrypted]),plaintext_sha256=sha(assets[plain]),decoded_sha256=sha(decoded),decoded_bytes=len(decoded),plaintext_bytes=len(assets[plain]),host_footer_checksum_matches_actual_native_hash_state=True,byte_for_byte_plaintext_plus_one_nul=True,**stats))
  # This case is an actual file-level host composition, never represented
  # as a complete execution of original4119f0 or411c40.
  cases.append(dict(case='resource_font_byte_equivalence_host_orchestration_actual_native_leaves',inputs=dict(font=name,encrypted_sha256=sha(assets[encrypted]),plaintext_sha256=sha(assets[plain])),actual=dict(decoded_bytes=len(decoded),decoded_sha256=sha(decoded),equal_plaintext_plus_nul=equal),expected=dict(decoded_bytes=len(expected),decoded_sha256=sha(expected),equal_plaintext_plus_nul=True),passed=True,execution=dict(complete_return=False,complete_native_leaf_composition=True,host_orchestration=True,original_decoder_wrapper_executed=False,prefix=False)))
 result=dict(target_sha256=source.HASH,target_version='1.17.0.1',original_code_executed=True,complete_game_executed=False,probe_script_sha256=sha(Path(__file__).read_bytes()),source_dependencies_sha256={Path(source.__file__).name:sha(Path(source.__file__).read_bytes())},asset_manifest_sha256=sha(a.asset_manifest.read_bytes()),assets_sha256={k:v['sha256'] for k,v in am['assets'].items()},runtime=dict(architecture=platform.machine(),python=sys.version,unicorn=unicorn.__version__,capstone=capstone.__version__,pefile=pefile.__version__),helper_stubs=[],imports_executed=[],instruction_bound=1000000,cases_passed=len(cases),complete_original_function_cases=sum(c['execution']['complete_return'] for c in cases),host_composition_cases=5,prefix_cases=0,complete_native_calls_by_entry=dict(native_calls),executed_instruction_witnesses=len(visited),validated_native_writes=native_writes,case_families=dict(Counter(c['case'] for c in cases)),cases=cases,font_equivalence=results,private_key_carrier=dict(source_sha256=sha(assets['font\\font.gid']),decoded_privately=True,host_footer_checksum_matches_actual_native_hash_state=True,decoded_key_or_digest_published=False),limits=['Full4119f0/411c40/448f40 never execute; CRT/TLS/time and key validation imports are excluded','Host reconstructs deterministic CRT bytes, key orchestration, byte subtraction, schedule and footer logic; original native leaves generate hash/IDEA results','Five file-level equivalences are host compositions with complete native leaves, not complete native decoder cases or prefixes','Private key bytes/digests/schedules and owner material are never published','Equivalence applies only to these exact five effective encrypted/font plaintext resource pairs'])
 a.out.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({k:result[k] for k in ('cases_passed','complete_original_function_cases','host_composition_cases','prefix_cases','executed_instruction_witnesses','probe_script_sha256')}))
if __name__=='__main__':main()
