#!/usr/bin/env python3
"""Execute unchanged original font/text routines with private stock inputs."""
import argparse,hashlib,json,platform,struct,sys
from collections import Counter
from pathlib import Path
import pefile
import inspect_native_text_117 as source
from probe_native_pixel_blitters_117 import pcx
PRIVATE,FONT,TEXT,PIXELS,STACK,STOP=0x800000,0x810000,0x820000,0x830000,0x900000,0xA00000
BITMAP,COUNT=PRIVATE,PRIVATE+0x40
WIDTH,HEIGHT=128,64
def sha(b):return hashlib.sha256(b).hexdigest()
def main():
 p=argparse.ArgumentParser(description=__doc__)
 for key in ('exe','assets','asset-manifest'):p.add_argument('--'+key,type=Path,required=True)
 p.add_argument('--out',type=Path);p.add_argument('--campaign-data',type=Path);p.add_argument('--execute-original-code',action='store_true');a=p.parse_args();raw=a.exe.read_bytes();assert sha(raw)==source.HASH
 deps={Path(source.__file__).name:sha(Path(source.__file__).read_bytes()),'probe_native_pixel_blitters_117.py':sha(Path(__file__).with_name('probe_native_pixel_blitters_117.py').read_bytes())};assert deps['probe_native_pixel_blitters_117.py']=='5cc11182fa6d44a99333d6252c6511e82180b857444a6fbff1834f79ccf869e2'
 if not a.execute_original_code:print(json.dumps(dict(original_code_executed=False,target_sha256=source.HASH)));return
 assert a.out and platform.machine().lower() in ('x86_64','amd64')
 import unicorn,capstone
 from unicorn import Uc,UC_ARCH_X86,UC_MODE_32,UC_HOOK_CODE,UC_HOOK_MEM_WRITE
 from unicorn.x86_const import UC_X86_REG_ECX,UC_X86_REG_EDX,UC_X86_REG_EAX,UC_X86_REG_ESP,UC_X86_REG_EIP,UC_X86_REG_EFLAGS
 pe=pefile.PE(data=raw);u=Uc(UC_ARCH_X86,UC_MODE_32);u.mem_map(0x400000,(pe.OPTIONAL_HEADER.SizeOfImage+4095)&~4095);u.mem_write(0x400000,pe.get_memory_mapped_image());u.mem_map(PRIVATE,0x50000);u.mem_map(STACK,0x10000);u.mem_map(STOP,4096)
 def w(addr,val,n=4):u.mem_write(addr,(val&((1<<(8*n))-1)).to_bytes(n,'little'))
 def r(addr,n=4,signed=False):return int.from_bytes(u.mem_read(addr,n),'little',signed=signed)
 ranges=[v for k,v in source.BLOCKS.items() if not k.endswith('_static')];entries={lo for lo,hi in ranges};approved=set();visited=set();cases=[];write_count=0
 def write_hook(machine,access,address,size,value,unused):
  nonlocal write_count
  assert any(lo<=address and address+size<=hi for lo,hi in ((STACK,STACK+0x10000),(PIXELS,PIXELS+WIDTH*HEIGHT),(0x6D3008,0x6D311C),(COUNT,COUNT+4))),('Outside declared writable state',hex(address),size)
  write_count+=1
 u.hook_add(UC_HOOK_MEM_WRITE,write_hook)
 def call(entry,ecx=0,edx=0,args=(),stop_before=None):
  done=False;prefix=False;steps=0;calls=[]
  def hook(machine,pc,n,unused):
   nonlocal done,prefix,steps
   if pc==STOP:done=True;machine.emu_stop();return
   if pc==stop_before:prefix=True;machine.emu_stop();return
   if (pc,n) not in approved:assert any(lo<=pc and pc+n<=hi for lo,hi in ranges),('Outside reviewed function',hex(pc),calls[-8:]);approved.add((pc,n))
   visited.add(pc);steps+=1
   if pc in entries:calls.append(hex(pc))
  h=u.hook_add(UC_HOOK_CODE,hook);sp=STACK+0xF000;u.mem_write(sp,struct.pack('<'+'I'*(len(args)+1),STOP,*args));u.reg_write(UC_X86_REG_ESP,sp);u.reg_write(UC_X86_REG_ECX,ecx&0xFFFFFFFF);u.reg_write(UC_X86_REG_EDX,edx&0xFFFFFFFF);u.reg_write(UC_X86_REG_EAX,0);u.reg_write(UC_X86_REG_EFLAGS,2)
  try:u.emu_start(entry,STOP+1,count=2000000)
  except Exception as e:raise RuntimeError((hex(entry),hex(u.reg_read(UC_X86_REG_EIP)),calls[-8:])) from e
  finally:u.hook_del(h)
  assert done or prefix
  if done:assert u.reg_read(UC_X86_REG_ESP)==sp+4*(len(args)+1)
  return dict(complete_return=done,stopped_before_unexecuted_callee=prefix,stop_va=hex(stop_before) if prefix else None,original_instructions=steps,calls=calls,return_eax=u.reg_read(UC_X86_REG_EAX))
 def record(family,inputs,actual,expected,ex):
  assert actual==expected,(family,inputs,actual,expected);cases.append(dict(case=family,inputs=inputs,actual=actual,expected=expected,passed=True,execution=ex))
 am=json.loads(a.asset_manifest.read_text());fonts={};palettes={};corpus=[]
 for name in source.FONTS:
  path='files\\font\\'+name+'.fnt';b=(a.assets/path.replace('\\','/')).read_bytes();assert sha(b)==am['assets'][path]['sha256'];fonts[name]=(b,source.font(b))
 for path in ('game\\tFontGam.pcx','glue\\title\\tFont.pcx'):
  b=(a.assets/path.replace('\\','/')).read_bytes();assert sha(b)==am['assets'][path]['sha256'];width,height,values=pcx(b);assert len(values)%8==0;palettes[path]=[values[n:n+8] for n in range(0,len(values),8)]
 for path in ('rez\\stat_txt.tbl','rez\\gluAll.tbl'):
  b=(a.assets/path.replace('\\','/')).read_bytes();assert sha(b)==am['assets'][path]['sha256'];corpus += [(path,i,t) for i,t in enumerate(source.string_table(b)[1],1)]
 def setup(name,x=5,y=4,left=2,right=120,spacing=1,measure=1):
  b,f=fonts[name];u.mem_write(0x6D3008,bytes(0x114));u.mem_write(FONT,b)
  for i,offset in enumerate(f['offsets']):w(FONT+8+i*4,FONT+offset)
  w(0x6D3020,FONT);w(0x6D3035,spacing,1);w(0x6D3034,3,1);w(0x6D3036,f['max_width'],1);w(0x6D3037,f['max_height'],1);w(0x6D300F,f['max_width']*2,1);w(0x6D300D,measure,1);w(0x6D300E,0x11,1);w(0x50F160,1,1);w(0x6D3038,left,2);w(0x6D303C,right,2);w(0x6D303E,1000,2);w(0x6D3048,x);w(0x6D304C,y);w(0x6D3046,y,2);w(0x6D3010,0x6D3050);w(0x6D3110,0x6D3058);w(BITMAP,WIDTH,2);w(BITMAP+2,HEIGHT,2);w(BITMAP+4,PIXELS);w(0x6D0C40,BITMAP);u.mem_write(PIXELS,bytes([0xA5]*(WIDTH*HEIGHT)))
  return f
 def advance(f,char,spacing=1):
  if char==9:return f['max_width']*2
  if char==32:return f['max_width']//2
  if char<32:return 0
  g=f['glyphs'].get(char);return g['width']+g['x_offset']+spacing if g else 0
 def width_model(f,char,mode,previous,spacing):
  if 1<=char<=32 and char!=11:return advance(f,char,spacing),previous
  if mode:
   if char>=160:
    if previous>=176 or previous==164:return mode,0
    if char>=176 or char==164:return mode,char
   previous=0
  return advance(f,char,spacing),previous
 def line_model(f,text,x,left,right,spacing=1,highlight=0):
  at=0
  while at<len(text) and text[at]:
   c=text[at]
   if c in (10,12):return at+1,x
   if c in (13,18,19,26) or c<32 and c!=9:at+=1;continue
   if c in (9,32):
    x += f['max_width']//2 if c==32 else f['max_width']*2-(x-left)%(f['max_width']*2);at+=1;continue
   if x>right:return at,x
   start,old=at,x
   while at<len(text) and text[at] and text[at] not in (9,10,12,18,19,26,32):
    c=text[at];x+=advance(f,c,spacing);at+=1
   if x>right:return start,old
  return at,x
 for name,(b,f) in fonts.items():
  setup(name)
  for char in range(256):
   ex=call(0x4CDE90,char);expected=FONT+f['offsets'][char-f['low']] if f['low']<=char<=f['high'] else 0;record('all_stock_font_byte_glyph_pointers',dict(font=name,char=char),dict(pointer=ex['return_eax']),dict(pointer=expected),ex)
  for spacing in (0,1,3):
   for char in range(256):
    setup(name,spacing=spacing);ex=call(0x4CDC30,char);record('all_stock_font_byte_widths_bearing_and_spacing',dict(font=name,char=char,spacing=spacing),dict(width=ex['return_eax'],lead_byte=r(0x6D300C,1)),dict(width=advance(f,char,spacing),lead_byte=0),ex)
  # All bytes and all five possible previous-state threshold profiles;
  # this is scalar width state, not OS/Korean glyph rendering.
  for previous in (0,163,164,175,176,255):
   for char in range(256):
    setup(name);w(0x6D3008,7);w(0x6D300C,previous,1);ex=call(0x4CDC30,char);width,lead=width_model(f,char,7,previous,1);record('native_dbcs_width_state_only_all_bytes',dict(font=name,char=char,mode_width=7,previous_lead=previous),dict(width=ex['return_eax'],lead_byte=r(0x6D300C,1)),dict(width=width,lead_byte=lead),ex)
  # Every stock glyph against two genuine PCX rows and two stride profiles.
  for char,g in f['glyphs'].items():
   for palette_path in palettes:
    lookup=palettes[palette_path][0]
    for stride_pad in (0,5):
     setup(name);stride=g['width']+stride_pad;dst=PIXELS+128;u.mem_write(0x6D3050,lookup);w(0x6D3014,g['width']);w(0x6D3018,g['height']);w(0x6D301C,stride_pad);ex=call(0x4CFDF0,dst,FONT+g['offset']+4);decoded,n=source.packets(b[g['offset']+4:],g['width'],g['height'],lookup=lookup);want=bytearray([0xA5]*(WIDTH*HEIGHT))
     for y in range(g['height']):want[128+y*stride:128+y*stride+g['width']]=decoded[y*g['width']:(y+1)*g['width']]
     record('all_stock_font_glyph_packets_private_indexed_buffers',dict(font=name,char=char,lookup_resource=palette_path,row=0,stride=stride,font_sha256=sha(b)),dict(bitmap_sha256=sha(bytes(u.mem_read(PIXELS,WIDTH*HEIGHT))),packet_cursor=u.reg_read(UC_X86_REG_EDX)-(FONT+g['offset']+4),destination_cursor=u.reg_read(UC_X86_REG_ECX)-dst),dict(bitmap_sha256=sha(want),packet_cursor=n,destination_cursor=stride*g['height']),ex)
  # Bounded measured text has no language or rendering calls.
  for char in range(256):
   if char in (18,19):continue
   setup(name);text=bytes([char,ord('A'),0]);u.mem_write(TEXT,text);ex=call(0x4CD670,TEXT,TEXT+len(text));x=5;bottom=4;at=0
   for c in text:
    if c in (0,11,12,20):break
    if c==9:x+=f['max_width']*2-(x-2)%(f['max_width']*2)
    elif c==32:x+=f['max_width']//2
    elif c>=33:
     g=f['glyphs'][c];bottom=max(bottom,4+g['y_offset']+g['height']-1);x+=advance(f,c)
    at+=1
   record('all_byte_bounded_native_measurement_except_alignment',dict(font=name,char=char,measure_only=1,initial_x=5,left=2),dict(x=r(0x6D3048,signed=True),bottom=r(0x6D3046,2,signed=True),consumed=ex['return_eax']-TEXT,bitmap_sha256=sha(bytes(u.mem_read(PIXELS,WIDTH*HEIGHT)))),dict(x=x,bottom=bottom,consumed=at,bitmap_sha256=sha(bytes([0xA5]*(WIDTH*HEIGHT)))),ex)
  for right in (20,45,120):
   for text in (b'A B C',b'ABCD EFGH\nI',b'A\tB\tC',b'\x03A\x0d B\x0cC',b'\x12ABC',b'\x13ABC',b'\xff\xa4\x80 Q'):
    setup(name);w(0x6D303C,right,2);u.mem_write(TEXT,text+b'\0');ex=call(0x4CD100,TEXT);count,x=line_model(f,text,5,2,right);record('complete_native_word_wrap_and_control_parser',dict(font=name,text_hex=text.hex(),right=right,wrap=1),dict(consumed=ex['return_eax']-TEXT,x=r(0x6D3048,signed=True)),dict(consumed=count,x=x),ex)
  # Native font setup is retained as a truthful pre-import prefix.
  setup(name);ex=call(0x4CE160,FONT,stop_before=0x4CE1C5);record('native_font_setup_prefix_before_locale_import',dict(font=name),dict(current=r(0x6D3020),max_width=r(0x6D3036,1),max_height=r(0x6D3037,1),tab=r(0x6D300F,1),gap=r(0x6D3034,1),spacing=r(0x6D3035,1),flags=r(0x6D300E,1)),dict(current=FONT,max_width=f['max_width'],max_height=f['max_height'],tab=f['max_width']*2,gap=3,spacing=1,flags=17),ex)
 # Pure setter covers every byte; previous-color state updates even on
 # unsupported controls except special restore1.
 code_rows={2:0x6D3050,3:0x6D3058,4:0x6D3060,5:0x6D3068,6:0x6D3070,7:0x6D3078,8:0x6D3090,14:0x6D3098,15:0x6D30A0,16:0x6D30A8,17:0x6D30B0,21:0x6D30B8,22:0x6D30C0,23:0x6D30C8,24:0x6D30D0,25:0x6D30D8,27:0x6D30E0,28:0x6D30E8,29:0x6D30F0,30:0x6D30F8,31:0x6D3108}
 for c in range(256):
  setup('font8');ex=call(0x4CE250,c);current=0x6D3058 if c==1 else code_rows.get(c,0x6D3050);previous=0x6D3058 if c==1 else 0x6D3050;record('all_control_color_setter_previous_row_state',dict(code=c),dict(current=r(0x6D3010),previous=r(0x6D3110)),dict(current=current,previous=previous),ex)
 # All genuine color rows use masks0..7 with zero skip, so every table
 # byte is consumed by the actual unchanged indexed glyph blitter.
 for path,rows in palettes.items():
  for index,lookup in enumerate(rows):
   setup('font8');u.mem_write(TEXT,bytes(range(8)));u.mem_write(0x6D3050,lookup);w(0x6D3014,8);w(0x6D3018,1);w(0x6D301C,0);ex=call(0x4CFDF0,PIXELS,TEXT);record('every_stock_text_palette_row_mask',dict(resource=path,row=index),dict(output_sha256=sha(bytes(u.mem_read(PIXELS,8)))),dict(output_sha256=sha(lookup)),ex)
 # Complete genuine wrapped TBL text inputs, with no payload text copied
 # into evidence. Font10 and finite width exercise actual rollback paths.
 for path,index,text in corpus:
  setup('font10');u.mem_write(TEXT,text+b'\0');ex=call(0x4CD100,TEXT);count,x=line_model(fonts['font10'][1],text,5,2,120);record('complete_stock_tbl_wrapped_line_metrics',dict(resource=path,index=index,text_sha256=sha(text),bytes=len(text),font='font10',right=120),dict(consumed=ex['return_eax']-TEXT,x=r(0x6D3048,signed=True)),dict(consumed=count,x=x),ex)
 # Native lookup truncates the request to low16, uses one-based IDs,
 # returns NULL for0 and a native empty-string pointer for out-of-range.
 def table_cases(tag,b,unique_only=False):
  inventory,strings=source.string_table(b);u.mem_write(TEXT,b);n=inventory['count'];indices=list(range(1,n+1))
  if unique_only:
   seen=set();indices=[row['index'] for row in inventory['rows'] if not(row['offset'] in seen or seen.add(row['offset']))]
  for index in [0]+indices+[n+1,65535,65536,65537]:
   ex=call(0x4C13B0,TEXT,index);low=index&65535;ptr=0 if not low else TEXT+inventory['rows'][low-1]['offset'] if low<=n else 0x4EC14E
   record('complete_native_tbl_or_campaign_str_lookup',dict(resource=tag,index=index,table_sha256=sha(b),unique_offsets_only=unique_only),dict(pointer=ex['return_eax']),dict(pointer=ptr),ex)
 for path in ('rez\\stat_txt.tbl','rez\\gluAll.tbl'):table_cases(path,(a.assets/path.replace('\\','/')).read_bytes())
 if a.campaign_data:
  for path in sorted((a.campaign_data/'campaign').rglob('scenario.chk')):
   b=path.read_bytes();at=0;section_index=0
   while at<len(b):
    key=b[at:at+4];size=struct.unpack_from('<I',b,at+4)[0];part=b[at+8:at+8+size];assert len(part)==size;at+=8+size
    if key==b'STR ':table_cases(str(path.relative_to(a.campaign_data))+'#STR'+str(section_index),part,True);section_index+=1
 # Complete multi-line measurement consumes LF through bounded_text and
 # advances by font.maxHeight+gap. Bounds retain measured line extents.
 for name,(b,f) in fonts.items():
  for flags in (0x11,0x13,0x15):
   for text in (b'A',b'AB\nC',b'A\n\nB'):
    setup(name);w(0x6D300E,flags,1);u.mem_write(TEXT,text+b'\0');ex=call(0x4CD9F0,TEXT,COUNT);lines=text.split(b'\n');left_bound=32767;right_bound=0;bottom=4;yy=4
    for line_index,line in enumerate(lines):
     xx=5 if line_index==0 else 2;length=sum(advance(f,c) for c in line)
     start=xx+(120-xx)//2-length//2 if flags&2 else 2+120-length-xx if flags&4 else xx
     left_bound=min(left_bound,start);right_bound=120 if flags&4 else max(right_bound,start+length)
     for c in line:
      g=f['glyphs'][c];bottom=max(bottom,yy+g['y_offset']+g['height']-1)
     if line_index<len(lines)-1:bottom+=2
     yy+=f['max_height']+3
    record('complete_native_multiline_measurement_and_alignment',dict(font=name,flags=flags,text_hex=text.hex()),dict(consumed=ex['return_eax']-TEXT,line_count=r(COUNT),x=r(0x6D3048,signed=True),y=r(0x6D304C,signed=True),bounds=[r(0x6D3040+i*2,2,signed=True) for i in range(4)]),dict(consumed=len(text),line_count=len(lines),x=2,y=4+len(lines)*(f['max_height']+3)-3,bounds=[left_bound,4,right_bound,bottom]),ex)
 # A highlighted byte preceded by control4 contributes height but no
 # advance in this measured consumer. Other positions advance normally.
 for name,(b,f) in fonts.items():
  for text in (b'\x04AB',b'A\x04AB',b'AA',b'\x03AB'):
   setup(name);call(0x4CE240,ord('A'));u.mem_write(TEXT,text+b'\0');ex=call(0x4CD670,TEXT,TEXT+len(text));x=5;bottom=4
   for at,c in enumerate(text):
    if c<32:continue
    g=f['glyphs'][c];bottom=max(bottom,4+g['y_offset']+g['height']-1)
    if not(c==ord('A') and at and text[at-1]==4):x+=advance(f,c)
   record('complete_bounded_highlight_measurement_advance',dict(font=name,text_hex=text.hex(),highlight=65),dict(x=r(0x6D3048),bottom=r(0x6D3046,2)),dict(x=x,bottom=bottom),ex)
 # Positive visible glyph preparation is an explicit prefix before the
 # first imported language query, never a successful draw-wrapper case.
 for name,(b,f) in fonts.items():
  setup(name,measure=0);u.mem_write(TEXT,b'A\0');ex=call(0x4CD670,TEXT,TEXT+2,stop_before=0x4CD837);g=f['glyphs'][65];record('positive_visible_text_prefix_before_locale_import',dict(font=name),dict(width=r(0x6D3014),height=r(0x6D3018),stride_pad=r(0x6D301C),bitmap_sha256=sha(bytes(u.mem_read(PIXELS,WIDTH*HEIGHT)))),dict(width=g['width'],height=g['height'],stride_pad=WIDTH-g['width'],bitmap_sha256=sha(bytes([0xA5]*(WIDTH*HEIGHT)))),ex)
 for width in (1,4,7):
  for height in (1,2,5):
   for pad in (0,5):
    for lead in (bytes(range(8)),bytes([8,1,2,3]),bytes([32,7,8,6]),bytes([248,0]),bytes([0,8,0])):
     setup('font8');packets=lead+bytes([1]*96);u.mem_write(TEXT,packets);lookup=palettes['game\\tFontGam.pcx'][0];u.mem_write(0x6D3050,lookup);w(0x6D3014,width);w(0x6D3018,height);w(0x6D301C,pad);ex=call(0x4CFDF0,PIXELS+128,TEXT);decoded,n=source.packets(packets,width,height,lookup=lookup);want=bytearray([0xA5]*(WIDTH*HEIGHT))
     for y in range(height):want[128+y*(width+pad):128+y*(width+pad)+width]=decoded[y*width:(y+1)*width]
     record('native_glyph_packet_zero_mask_cross_row_and_final_skip',dict(width=width,height=height,stride_pad=pad,packet_prefix_hex=lead.hex()),dict(bitmap_sha256=sha(bytes(u.mem_read(PIXELS,WIDTH*HEIGHT))),consumed=u.reg_read(UC_X86_REG_EDX)-TEXT),dict(bitmap_sha256=sha(want),consumed=n),ex)
 for current in (0x6D3050,0x6D3068):
  for c in range(1,32):
   if c in (18,19):continue
   setup('font8',measure=0);w(0x6D3010,current);u.mem_write(TEXT,bytes([c,0]));ex=call(0x4CD670,TEXT,TEXT+2);color_code=c not in (9,10,11,12,13,20,26);changed=color_code and current!=0x6D3068
   want=0x6D3058 if c==1 and changed else code_rows.get(c,current) if changed else current;previous=current if changed and c!=1 else 0x6D3058
   record('complete_control_only_text_gray_row_lock',dict(code=c,current=current),dict(current=r(0x6D3010),previous=r(0x6D3110)),dict(current=want,previous=previous),ex)
 # The signed idiv used by both line parsers truncates toward zero;
 # negative coordinates therefore differ from a language's floor modulo.
 def tab(x,left,step):
  value=x-left;quotient=abs(value)//step*(1 if value>=0 else -1);return x+step-(value-quotient*step)
 for name,(b,f) in fonts.items():
  for x in (-17,-7,-1,0,3,16):
   for left in (0,5):
    for wrap in (0,1):
     setup(name,x=x,left=left,right=32767);w(0x50F160,wrap,1);text=b'A\tB\nREST';u.mem_write(TEXT,text+b'\0');ex=call(0x4CD100,TEXT);expected=tab(x+advance(f,65),left,f['max_width']*2)+advance(f,66);record('complete_signed_tab_idiv_both_parser_modes',dict(font=name,x=x,left=left,wrap=wrap),dict(consumed=ex['return_eax']-TEXT,x=r(0x6D3048,signed=True)),dict(consumed=4,x=expected),ex)
 for flags in (0x11,0x31):
  for first_y in (-17,-1,0,7,22):
   for final_y in (-7,0,4,19,40):
    setup('font8');w(0x6D300E,flags,1);w(0x6D303E,31,2);w(0x6D304C,final_y);w(COUNT,first_y);ex=call(0x4CDBF0,COUNT);remaining,used=31-first_y,final_y-first_y
    if flags&32:remaining=int(remaining/2);used=int(used/2)
    record('complete_vertical_alignment_signed_rounding',dict(flags=flags,first_y=first_y,final_y=final_y,bottom=31),dict(y=r(COUNT,signed=True)),dict(y=first_y-used+remaining),ex)
 result=dict(target_sha256=source.HASH,target_version='1.17.0.1',original_code_executed=True,complete_game_executed=False,probe_script_sha256=sha(Path(__file__).read_bytes()),source_dependencies_sha256=deps,asset_manifest_sha256=sha(a.asset_manifest.read_bytes()),assets_sha256={k:v['sha256'] for k,v in am['assets'].items()},runtime=dict(architecture=platform.machine(),python=sys.version,unicorn=unicorn.__version__,capstone=capstone.__version__,pefile=pefile.__version__),helper_stubs=[],imports_executed=[],instruction_bound=2000000,cases_passed=len(cases),complete_original_function_cases=sum(c['execution']['complete_return'] for c in cases),prefix_cases=sum(not c['execution']['complete_return'] for c in cases),executed_instruction_witnesses=len(visited),validated_native_writes=write_count,case_families=dict(Counter(c['case'] for c in cases)),cases=cases,limits=['Plaintext BroodWar files\\font corpus equivalence to decrypted effective installed fonts remains unproved','Indexed private glyph outputs, not complete renderer or display/RGB evidence','Locale imports, encrypted-font decoder, menu/briefing UI, and OS DBCS rendering never execute','Word-wrap text corpus uses font10 and declared finite width; all five fonts have full byte width and glyph coverage','Font-setup and positive visible text prefixes stop before actual locale calls'])
 a.out.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({k:result[k] for k in ('cases_passed','complete_original_function_cases','prefix_cases','executed_instruction_witnesses','probe_script_sha256')}))
if __name__=='__main__':main()
