#!/usr/bin/env python3
"""Execute unchanged native effect kernels and palette/image-state consumers.
Only private guarded indexed buffers and declared stock inputs are used. No
device upload, import, process entrypoint or full renderer executes.
"""
import argparse,hashlib,json,platform,struct,sys
from collections import Counter
from pathlib import Path
import capstone,pefile
import inspect_native_pixel_effects_117 as source
from probe_native_pixel_blitters_117 import pcx,decode_frame
PRIVATE,PIXELS,ASSETS,STACK,STOP=0x800000,0x810000,0x900000,0x1200000,0x1300000
FRAME,RECT,BITMAP,SYNTH,IMAGE,PARENT,UNIT,PAL=PRIVATE,PRIVATE+0x40,PRIVATE+0x80,PRIVATE+0x1000,PRIVATE+0x200,PRIVATE+0x300,PRIVATE+0x500,PRIVATE+0x8000
SPRITE=PRIVATE+0x700
WIDTH,HEIGHT=640,480
def sha(b):return hashlib.sha256(b).hexdigest()
def signed(v):return v-256 if v>=128 else v
def brightness(p):return bytes((77*p[i*4]+151*p[i*4+1]+28*p[i*4+2]+4096)>>13 for i in range(256))
def main():
 p=argparse.ArgumentParser(description=__doc__)
 for k in ('exe','assets','asset-manifest','effect-assets','effect-manifest','pixel-assets','pixel-manifest','data'):p.add_argument('--'+k,type=Path,required=True)
 p.add_argument('--out',type=Path);p.add_argument('--execute-original-code',action='store_true');a=p.parse_args()
 raw=a.exe.read_bytes();assert sha(raw)==source.HASH;deps=source.dependencies();deps[Path(source.__file__).name]=sha(Path(source.__file__).read_bytes())
 deps['probe_native_pixel_blitters_117.py']=sha((Path(__file__).parent/'probe_native_pixel_blitters_117.py').read_bytes());assert deps['probe_native_pixel_blitters_117.py']=='5cc11182fa6d44a99333d6252c6511e82180b857444a6fbff1834f79ccf869e2'
 if not a.execute_original_code:print(json.dumps(dict(original_code_executed=False,target_sha256=source.HASH)));return
 assert a.out and platform.machine().lower() in ('x86_64','amd64')
 manifests={k:json.loads(getattr(a,k+'_manifest').read_text()) for k in ('asset','effect','pixel')}
 assert len({v['retail_manifest_sha256'] for v in manifests.values()})==1
 grps={};meta={};provenance={};palettes={}
 for kind,root in (('asset',a.assets),('effect',a.effect_assets)):
  for row in manifests[kind]['images']:
   path=row['path'];b=(root/path.replace('\\','/')).read_bytes();assert sha(b)==row['sha256'];grps[path]=b;meta[path]=row;provenance[path]=row['sha256']
 for path,row in manifests['pixel']['assets'].items():
  b=(a.pixel_assets/path.replace('\\','/')).read_bytes();assert sha(b)==row['sha256'];provenance[path]=row['sha256'];palettes[path]=pcx(b)[2] if path.endswith('.pcx') else b
 script=(a.data/'scripts/iscript.bin').read_bytes();assert sha(script)=='cd35e04eaa8ea95cbbccc980bfaf4cae236f4b18b620dfb4a765822ec32c1cd0';provenance['scripts\\iscript.bin']=sha(script)
 images_dat=(a.data/'arr/images.dat').read_bytes();assert sha(images_dat)=='6b6123e2cc6771d96f914fc9bb92d34da17cc3319dc02e2db0cd10b4f3d2efdc';provenance['arr\\images.dat']=sha(images_dat)
 import unicorn
 from unicorn import Uc,UC_ARCH_X86,UC_MODE_32,UC_HOOK_CODE
 from unicorn.x86_const import UC_X86_REG_ECX,UC_X86_REG_EDX,UC_X86_REG_EAX,UC_X86_REG_EIP,UC_X86_REG_ESP,UC_X86_REG_EFLAGS
 pe=pefile.PE(data=raw);assert pe.OPTIONAL_HEADER.ImageBase==0x400000 and pe.FILE_HEADER.Machine==0x14C
 u=Uc(UC_ARCH_X86,UC_MODE_32);u.mem_map(0x400000,(pe.OPTIONAL_HEADER.SizeOfImage+4095)&~4095);u.mem_write(0x400000,pe.get_memory_mapped_image())
 for addr,size in ((PRIVATE,0x10000),(PIXELS,0x60000),(ASSETS,0x800000),(STACK,0x10000),(STOP,4096)):u.mem_map(addr,size)
 def w(addr,v,size=4):u.mem_write(addr,(v&((1<<(size*8))-1)).to_bytes(size,'little'))
 def r(addr,size=4):return int.from_bytes(u.mem_read(addr,size),'little')
 pointers={};at=ASSETS
 for path,b in grps.items():
  pointers[path]=at;relocated=bytearray(b)
  for f in range(struct.unpack_from('<H',b)[0]):struct.pack_into('<I',relocated,10+f*8,at+struct.unpack_from('<I',b,10+f*8)[0])
  u.mem_write(at,bytes(relocated));at+=(len(b)+4095)&~4095
 blend=bytes([0x5A]*256)+palettes['tileset\\badlands\\trans50.pcx'];blend_at=at;u.mem_write(at,blend);at+=(len(blend)+4095)&~4095;w(0x508890,blend_at)
 fire=bytes([0x5A]*256)+palettes['tileset\\badlands\\ofire.pcx'];fire_at=at;u.mem_write(at,fire);at+=(len(fire)+4095)&~4095;w(0x508840,fire_at)
 script_at=at;u.mem_write(at,script);at+=(len(script)+4095)&~4095;assert at<ASSETS+0x800000
 w(0x54330C,script_at);w(0x543300,UNIT);w(0x543304,UNIT);w(0x543A40,pointers['unit\\protoss\\texture.grp']);w(0x6D319C,2);w(0x50F310,0)
 player=palettes['game\\tunit.pcx'];u.mem_write(0x529056,player[:96]);w(0x671A88,0,1)
 w(0x6D0C40,BITMAP);w(BITMAP,WIDTH,2);w(BITMAP+2,HEIGHT,2);w(BITMAP+4,PIXELS)
 baseline=bytes((i*17+(i//WIDTH)*31)&255 for i in range(WIDTH*HEIGHT));guard=bytes([0xE7]*16);mapping=bytes(range(256))
 palette=palettes['tileset\\badlands.wpe'];stock_brightness=brightness(palette);u.mem_write(0x666460,stock_brightness)
 cases=[];visited=set();approved=set();ranges=[v for k,v in source.BLOCKS.items() if k not in source.STATIC_ONLY];entries={v[0] for v in ranges}
 def call(entry,x=0,y=0,args=(),stop_before=None):
  done=False;prefix=False;steps=0;calls=[]
  def hook(machine,pc,size,unused):
   nonlocal done,prefix,steps
   if pc==STOP:done=True;machine.emu_stop();return
   if pc==stop_before:prefix=True;machine.emu_stop();return
   if (pc,size) not in approved:assert any(lo<=pc and pc+size<=hi for lo,hi in ranges),('Outside reviewed function',hex(pc),calls[-8:]);approved.add((pc,size))
   visited.add(pc);steps+=1
   if pc in entries:calls.append(hex(pc))
  h=u.hook_add(UC_HOOK_CODE,hook);sp=STACK+0xF000;u.mem_write(sp,struct.pack('<'+'I'*(len(args)+1),STOP,*(v&0xFFFFFFFF for v in args)));u.reg_write(UC_X86_REG_ESP,sp);u.reg_write(UC_X86_REG_ECX,x);u.reg_write(UC_X86_REG_EDX,y);u.reg_write(UC_X86_REG_EAX,0);u.reg_write(UC_X86_REG_EFLAGS,2)
  try:u.emu_start(entry,STOP+1,count=2000000)
  except Exception as e:raise RuntimeError((hex(entry),hex(u.reg_read(UC_X86_REG_EIP)),calls[-10:])) from e
  finally:u.hook_del(h)
  assert done or prefix
  if done:assert u.reg_read(UC_X86_REG_ESP)==sp+4*(len(args)+1)
  return dict(complete_return=done,prefix_stop_before=hex(stop_before) if prefix else None,original_instructions=steps,calls=calls,return_eax=u.reg_read(UC_X86_REG_EAX))
 def record(name,inputs,actual,expected,ex):
  assert actual==expected,(name,inputs,actual,expected)
  cases.append(dict(case=name,inputs=inputs,actual=actual,expected=expected,passed=True,execution=ex))
 def render(name,inputs,entry,header,rows,crop,mirror=False,mode='threshold',arg=0,x=7,y=9,lookup_map=None):
  fw,fh=header[2:4];left,top,cw,ch=crop;assert cw>0 and ch>=0 and left+cw<=fw and top+ch<=fh and x+cw<=WIDTH and y+ch<=HEIGHT
  color_map=mapping if lookup_map is None else lookup_map
  u.mem_write(RECT,struct.pack('<4I',left,top,cw,ch));u.mem_write(FRAME,struct.pack('<4BI',*header));u.mem_write(0x501021,color_map);u.mem_write(0x666460,stock_brightness);w(0x6D0C40,BITMAP)
  u.mem_write(PIXELS-16,guard+baseline+guard);ex=call(entry,x,y,(FRAME,RECT,arg));want=bytearray(baseline)
  def pass_pixels(which):
   for yy in range(ch):
    for xx in (range(cw-1,-1,-1) if mirror else range(cw)):
     sx=fw-1-left-xx if mirror else left+xx;v=rows[top+yy][sx]
     if v is None:continue
     dest=(y+yy)*WIDTH+x+xx
     if which in ('cloak','raw'):want[dest]=want[(dest+(-1 if mirror else 1)*(stock_brightness[v] if which=='cloak' else v))%(WIDTH*HEIGHT)]
     elif which=='blend':want[dest]=blend[v*256+want[dest]]
     elif which=='threshold':
      if signed(stock_brightness[color_map[v]])>signed(arg&255):want[dest]=color_map[v]
  for stage in {'cloak_transition':['cloak','threshold'],'detected_transition':['blend','threshold'],'detected':['blend']}.get(mode,[mode]):pass_pixels(stage)
  actual=dict(indexed_bitmap_sha256=sha(bytes(u.mem_read(PIXELS-16,len(baseline)+32))),bitmap_pointer=r(0x6D0C40),mapping_sha256=sha(bytes(u.mem_read(0x501021,256))))
  expected=dict(indexed_bitmap_sha256=sha(guard+want+guard),bitmap_pointer=BITMAP,mapping_sha256=sha(color_map));record(name,inputs,actual,expected,ex)
 def synthetic(rows):
  width=len(rows[0]);height=len(rows);assert 0<width<=255 and all(len(row)==width for row in rows);lines=[]
  for row in rows:
   enc=bytearray();i=0
   while i<width:
    if row[i] is None:
     n=1
     while i+n<width and row[i+n] is None and n<127:n+=1
     enc.append(128|n);i+=n
    elif i+1<width and row[i+1]==row[i]:
     n=2
     while i+n<width and row[i+n]==row[i] and n<63:n+=1
     enc.extend([64|n,row[i]]);i+=n
    else:
     vals=[row[i]];i+=1
     while i<width and row[i] is not None and len(vals)<63 and not(i+1<width and row[i+1]==row[i]):vals.append(row[i]);i+=1
     enc.append(len(vals));enc.extend(vals)
   lines.append(enc)
  offsets=[];pos=2*height
  for line in lines:offsets.append(pos);pos+=len(line)
  b=struct.pack('<'+'H'*height,*offsets)+b''.join(lines);assert len(b)<0x6000;u.mem_write(SYNTH,b);return (0,0,width,height,SYNTH)
 # Stock and channel-isolated palettes prove rounding and channel weights.
 for tag,b in [('stock_badlands_wpe',palette)]+[(f'isolated_channel_{channel}',bytes(v if (i%4)==channel else 0 for v in range(256) for i in range(4))) for channel in range(4)]+[('all_channels_grayscale',bytes(v for v in range(256) for _ in range(4)))]:
  u.mem_write(PAL,b);ex=call(0x445F30,PAL);record('palette_to_cloak_brightness_table',dict(profile=tag,input_sha256=sha(b)),dict(table_sha256=sha(bytes(u.mem_read(0x666460,256)))),dict(table_sha256=sha(brightness(b))),ex)
 rows=[[None]*3+[0]*4+list(range(64))+[None]*5+[8]*63+[255]*63+[None]*6];h=synthetic(rows);width=len(rows[0])
 methods={'threshold':(0x406235,0x406A84),'cloak':(0x4063B5,0x406C09),'raw':(0x406533,0x406D8E),'cloak_transition':(0x40E6C0,0x40E700),'detected_transition':(0x40E740,0x40E780),'detected':(0x40E7C0,0x40E7E0)}
 for mode,pair in methods.items():
  for mirror in (False,True):
   for crop in [(0,0,width,1),(1,0,2,1),(3,0,3,1),(6,0,7,1),(7,0,62,1),(70,0,4,1),(74,0,62,1),(136,0,62,1),(198,0,5,1),(0,0,width,0)]:
    for threshold in (0,4,8,127,128,255) if 'threshold' in mode or 'transition' in mode else (0,):
     render('authored_effect_packet_clipping_signed_threshold',dict(method=mode,mirror=mirror,crop=list(crop),threshold=threshold,synthetic_resource=True),pair[int(mirror)],h,rows,crop,mirror,mode,threshold,lookup_map=bytes(reversed(mapping)) if threshold==4 else None)
 # Fixed buffer wrap and live in-place traversal are distinct from a snapshot read.
 rows=[[255]*32+list(range(1,33))+[None]*4+[8]*32];h=synthetic(rows)
 for mode in ('cloak','raw'):
  for mirror in (False,True):
   for x,y in ((0,0),(WIDTH-len(rows[0]),HEIGHT-1),(7,240)):
    render('distortion_fixed_640x480_wrap_in_place',dict(method=mode,mirror=mirror,destination=[x,y],synthetic_resource=True),methods[mode][int(mirror)],h,rows,(0,0,len(rows[0]),1),mirror,mode,x=x,y=y)
 # Stock visible shapes test actual GRP transparency and cropped packet entry.
 for path in ('unit\\terran\\ughost.grp','unit\\terran\\phoenix.grp','unit\\protoss\\zealot.grp'):
  assert path in grps,path
  for f in (0,struct.unpack_from('<H',grps[path])[0]-1):
   h,rows,_=decode_frame(grps[path],f);h=h[:4]+(pointers[path]+h[4],);fw,fh=h[2:4]
   for mirror in (False,True):
    for mode,pair in methods.items():
     for crop in ((0,0,fw,fh),(1,1,fw-1,fh-1)):
      render('stock_grp_complete_cloak_detected_and_emp_effects',dict(asset_sha256=provenance[path],frame=f,mirror=mirror,method=mode,crop=list(crop)),pair[int(mirror)],h,rows,crop,mirror,mode,arg=4)
 # Texture method12: real stock texture frame blit into the private native scratch
 # buffer, then original mask copy, and restoration of the bitmap global.
 path='unit\\protoss\\zealot.grp';mh,mrows,_=decode_frame(grps[path],0);mh=mh[:4]+(pointers[path]+mh[4],);fw,fh=mh[2:4]
 for tf in range(21):
  th,trows,_=decode_frame(grps['unit\\protoss\\texture.grp'],tf)
  for crop in ((0,0,fw,fh),(1,1,fw-1,fh-1),(fw//2,fh//2,1,1)):
   left,top,cw,ch=crop;assert left+cw<=th[2] and top+ch<=th[3]
   for mirror in (False,True):
    u.mem_write(RECT,struct.pack('<4I',*crop));u.mem_write(FRAME,struct.pack('<4BI',*mh));u.mem_write(0x501021,mapping);w(0x6D0C40,BITMAP);u.mem_write(PIXELS-16,guard+baseline+guard);temp=bytes([0xA5]*65536);u.mem_write(0x59C2B0,temp)
    ex=call(0x40E880 if mirror else 0x40E800,7,9,(FRAME,RECT,tf));texture=bytearray(temp)
    for yy in range(ch):
     for xx in range(cw):
      v=trows[top+yy][left+xx]
      if v is not None:texture[yy*cw+xx]=mapping[v]
    want=bytearray(baseline)
    for yy in range(ch):
     for xx in range(cw):
      sx=fw-1-left-xx if mirror else left+xx
      if mrows[top+yy][sx] is not None:want[(9+yy)*WIDTH+7+xx]=texture[yy*cw+(cw-1-xx if mirror else xx)]
    record('stock_warp_texture_full_two_stage_draw_and_restore',dict(main_grp_sha256=provenance[path],texture_grp_sha256=provenance['unit\\protoss\\texture.grp'],texture_frame=tf,mirror=mirror,crop=list(crop),transparent_texture_scratch_byte=0xA5),dict(bitmap_sha256=sha(bytes(u.mem_read(PIXELS-16,len(baseline)+32))),scratch_sha256=sha(bytes(u.mem_read(0x59C2B0,len(temp)))),bitmap_pointer=r(0x6D0C40)),dict(bitmap_sha256=sha(guard+want+guard),scratch_sha256=sha(texture),bitmap_pointer=BITMAP),ex)
 # Size outline draws only those original frame edges retained by clipping.
 h=synthetic([[1]*31 for _ in range(17)])
 for crop in ((0,0,31,17),(1,1,29,15),(0,1,30,15),(1,0,30,16),(0,0,1,1),(30,16,1,1)):
  left,top,cw,ch=crop
  for color in (0,1,255):
   u.mem_write(RECT,struct.pack('<4I',*crop));u.mem_write(FRAME,struct.pack('<4BI',*h));w(0x6D237A,color,1);u.mem_write(PIXELS-16,guard+baseline+guard);ex=call(0x40E9D0,7,9,(FRAME,RECT,0));want=bytearray(baseline)
   for yy in range(ch):
    for xx in range(cw):
     if (top==0 and yy==0) or (left==0 and xx==0) or (left+cw==31 and xx==cw-1) or (top+ch==17 and yy==ch-1):want[(9+yy)*WIDTH+7+xx]=color
   record('native_size_outline_preserves_clipped_frame_edges',dict(crop=list(crop),color=color),dict(bitmap_sha256=sha(bytes(u.mem_read(PIXELS-16,len(baseline)+32))),global_line_color=r(0x6D0C50,1)),dict(bitmap_sha256=sha(guard+want+guard),global_line_color=color),ex)
 # Method installation: all original slots, mirror selection, dirty flag and
 # method17's packed parameter48/timer2 initialization.
 dispatch={}
 for method in range(18):
  dispatch[method]=dict(update=r(0x503044+method*8),normal=r(0x5030D4+method*12),mirrored=r(0x5030D8+method*12))
 def image_fixture(method,param=0,timer=0,mirror=False,animation=0):
  b=bytearray(0x50);struct.pack_into('<H',b,8,154 if method==17 else 237);b[0x0A]=method;struct.pack_into('<H',b,0x0C,2 if mirror else 0);struct.pack_into('<H',b,0x10,12348);struct.pack_into('<H',b,0x12,12360);b[0x16]=animation;b[0x30]=param;b[0x31]=timer;struct.pack_into('<I',b,0x2C,pointers['unit\\protoss\\zealot.grp' if method==17 else 'unit\\terran\\ughost.grp']);struct.pack_into('<I',b,0x34,dispatch[method]['mirrored' if mirror else 'normal']);struct.pack_into('<I',b,0x38,dispatch[method]['update']);return b
 def set_method(b,method):
  b[0x0A]=method;flag=struct.unpack_from('<H',b,0x0C)[0];struct.pack_into('<I',b,0x34,dispatch[method]['mirrored' if flag&2 else 'normal']);struct.pack_into('<I',b,0x38,dispatch[method]['update']);struct.pack_into('<H',b,0x0C,flag|1)
  if method==17:struct.pack_into('<H',b,0x30,0x230)
 for method in range(18):
  for mirror in (False,True):
   b=image_fixture(0,37,21,mirror);u.mem_write(IMAGE,bytes(b));want=bytearray(b);set_method(want,method);ex=call(0x4116E0,IMAGE,args=(method+256,));record('native_all_draw_method_installation_low_byte_and_callbacks',dict(method_argument=method+256,mirror=mirror),dict(image_sha256=sha(bytes(u.mem_read(IMAGE,len(b))))),dict(image_sha256=sha(want)),ex)
 def callback_expected(b,status,signal):
  want=bytearray(b);method=want[0x0A]
  if want[0x31]:want[0x31]-=1;return want,status,signal
  if method in (2,5):
   want[0x30]=(want[0x30]+1)&255;want[0x31]=3
   if want[0x30]>=8:set_method(want,method+1);status=(status&~0x300)|0x300
  elif method in (4,7):
   old=want[0x30];want[0x30]=(old-1)&255;want[0x31]=3
   if old==0:set_method(want,0);status&=~0x300
  elif method==17:
   want[0x31]=2
   if want[0x30]<63:want[0x30]+=1
   else:
    if want[0x16]!=1:want[0x16]=1;struct.pack_into('<H',want,0x12,12371);struct.pack_into('<H',want,0x14,0);want[0x17]=0
    signal|=1
  else:raise AssertionError(method)
  struct.pack_into('<H',want,0x0C,struct.unpack_from('<H',want,0x0C)[0]|1);return want,status,signal
 for method in (2,5,4,7,17):
  for param in (0,1,7,8,48,62,63,127,128,255):
   for timer in (0,1,2,3,255):
    for mirror in (False,True):
     for animation in (0,1) if method==17 else (0,):
      b=image_fixture(method,param,timer,mirror,animation);status=0xA5A5A5A5;signal=0x80;u.mem_write(IMAGE,bytes(b));u.mem_write(UNIT,bytes(0x150));w(UNIT+0xDC,status);w(UNIT+0x4F,signal,1);want,ws,wq=callback_expected(b,status,signal);ex=call(dispatch[method]['update'],IMAGE)
      record('native_cloak_decloak_warp_callback_state_and_real_callees',dict(method=method,param=param,timer=timer,mirror=mirror,animation=animation,image_id=154 if method==17 else 237,stock_warp_iscript=192,stock_warp_death_pc=12369,authored_existing_image_context=True),dict(image_sha256=sha(bytes(u.mem_read(IMAGE,len(b)))),unit_status=r(UNIT+0xDC),order_signal=r(UNIT+0x4F,1)),dict(image_sha256=sha(want),unit_status=ws,order_signal=wq),ex)
 # Canonical transitions compose successive callback calls, not gameplay frames.
 for initial in (2,5,4,7,17):
  b=image_fixture(initial,48 if initial==17 else 7 if initial in (4,7) else 0,2 if initial==17 else 0);status=0;signal=0x80;u.mem_write(IMAGE,bytes(b));u.mem_write(UNIT,bytes(0x150));w(UNIT+0x4F,signal,1)
  for step in range(60):
   if b[0x0A] not in (2,5,4,7,17):break
   want,ws,wq=callback_expected(b,status,signal);ex=call(dispatch[b[0x0A]]['update'],IMAGE)
   record('composed_canonical_image_callback_sequence',dict(initial_method=initial,callback_index=step),dict(image_sha256=sha(bytes(u.mem_read(IMAGE,len(b)))),unit_status=r(UNIT+0xDC),order_signal=r(UNIT+0x4F,1)),dict(image_sha256=sha(want),unit_status=ws,order_signal=wq),ex);b,status,signal=want,ws,wq
 for method in range(18):
  for mirror in (False,True):
   parent=image_fixture(method,63,3);child=image_fixture(0,37,21,mirror);u.mem_write(PARENT,bytes(parent));u.mem_write(IMAGE,bytes(child));want=bytearray(child)
   if 2<=method<=7:set_method(want,method);want[0x30:0x32]=parent[0x30:0x32]
   ex=call(0x40EF80,IMAGE,args=(PARENT,));record('native_parent_cloak_copy_exact_method_interval',dict(parent_method=method,child_mirror=mirror),dict(image_sha256=sha(bytes(u.mem_read(IMAGE,len(child))))),dict(image_sha256=sha(want)),ex)
 for old_method in range(18):
  for mirror in (False,True):
   for entry in (0x40ED00,0x40ED30):
    b=image_fixture(old_method,37,21,mirror);u.mem_write(IMAGE,bytes(b));want=bytearray(b);new=2 if entry==0x40ED00 else 4 if old_method<4 else 7;set_method(want,new);struct.pack_into('<H',want,0x30,0x300 if entry==0x40ED00 else 0x308);ex=call(entry,IMAGE)
    record('native_cloak_decloak_initializers_and_old_method_selection',dict(entry=hex(entry),old_method=old_method,mirror=mirror),dict(image_sha256=sha(bytes(u.mem_read(IMAGE,len(b))))),dict(image_sha256=sha(want)),ex)
 # Original bounds -> original sprite draw -> installed original effect. This
 # is an isolated existing sprite, not the display device/world render traversal.
 path='unit\\terran\\ughost.grp';gh,grows,_=decode_frame(grps[path],0);fx,fy,fw,fh=gh[:4];gw,ght=struct.unpack_from('<2H',grps[path],2)
 for method in (0,3,5,6,8,17):
  for mirror in (False,True):
   for sx,sy in ((-10,-10),(0,0),(31,31),(629,395),(700,410)):
    for gate in ('dirty','clean_grid_zero','clean_grid_one','hidden'):
     flag=(2 if mirror else 0)|(1 if gate=='dirty' else 0)|(0x40 if gate=='hidden' else 0);b=image_fixture(method,48 if method==17 else 4,0,mirror);struct.pack_into('<H',b,8,237);struct.pack_into('<H',b,0x0C,flag);struct.pack_into('<I',b,0x2C,pointers[path]);struct.pack_into('<I',b,0x3C,SPRITE);u.mem_write(IMAGE,bytes(b));u.mem_write(SPRITE,bytes(0x40));w(SPRITE+0x20,IMAGE);w(SPRITE+0x0A,3,1)
     wx=sx-(gw//2-fw-fx if mirror else fx-gw//2);wy=sy-fy+ght//2;w(SPRITE+0x14,wx,2);w(SPRITE+0x16,wy,2);w(0x543218,0);w(0x54321C,0);u.mem_write(0x6D06D0,bytes([1 if gate=='clean_grid_one' else 0]*1200));u.mem_write(0x501021,mapping);u.mem_write(0x666460,stock_brightness);u.mem_write(PIXELS-16,guard+baseline+guard)
     bounds=call(0x40EE70,IMAGE);left,top=max(-sx,0),max(-sy,0);dx,dy=max(sx,0),max(sy,0);cw=min(fw+min(sx,0),640-dx);ch=min(fh+min(sy,0),400-dy);expected_bounds=[sx,sy,dx,dy,left,top,cw,ch];assert list(struct.unpack('<8h',u.mem_read(IMAGE+0x1C,16)))==expected_bounds
     ex=call(0x415A80,SPRITE);want=bytearray(b);struct.pack_into('<8h',want,0x1C,*expected_bounds);struct.pack_into('<H',want,0x0C,flag&~1);expected_map=bytearray(mapping);expected_map[8:16]=player[3*8:4*8];want_pixels=bytearray(baseline);drawn=gate in ('dirty','clean_grid_one') and cw>0 and ch>0
     if drawn:
      stages=['cloak'] if method==3 else ['blend','threshold'] if method==5 else ['blend'] if method==6 else ['raw'] if method==8 else ['warp_flash'] if method==17 else ['normal']
      for stage in stages:
       for yy in range(ch):
        for xx in (range(cw-1,-1,-1) if mirror else range(cw)):
         source_x=fw-1-left-xx if mirror else left+xx;v=grows[top+yy][source_x]
         if v is None:continue
         dest=(dy+yy)*WIDTH+dx+xx
         if stage=='normal':want_pixels[dest]=expected_map[v]
         elif stage in ('cloak','raw'):want_pixels[dest]=want_pixels[(dest+(-1 if mirror else 1)*(stock_brightness[v] if stage=='cloak' else v))%(WIDTH*HEIGHT)]
         elif stage=='blend':want_pixels[dest]=blend[v*256+want_pixels[dest]]
         elif stage=='warp_flash':want_pixels[dest]=fire[48*256+want_pixels[dest]]
         elif stock_brightness[expected_map[v]]>4:want_pixels[dest]=expected_map[v]
     record('native_bounds_sprite_draw_clipping_dirty_hidden_and_real_dispatch',dict(asset_sha256=provenance[path],frame=0,method=method,mirror=mirror,unclipped_screen_position=[sx,sy],gate=gate,owner=3,attached_main_image_pointer=0),dict(bitmap_sha256=sha(bytes(u.mem_read(PIXELS-16,len(baseline)+32))),image_sha256=sha(bytes(u.mem_read(IMAGE,len(b)))),mapping_sha256=sha(bytes(u.mem_read(0x501021,256))),current_owner_color=r(0x5D6E38)),dict(bitmap_sha256=sha(guard+want_pixels+guard),image_sha256=sha(want),mapping_sha256=sha(expected_map),current_owner_color=3),dict(complete_return=True,original_instructions=bounds['original_instructions']+ex['original_instructions'],calls=bounds['calls']+ex['calls']))
 # Native palette memory setters and simple/advanced cycle leaves. Clock/device
 # scheduling is a distinct native prefix and never replaced with a stub.
 u.mem_write(0x6D2B88,bytes([0xAA]*1024));u.mem_write(PAL,palette)
 for first,count in ((0,256),(0,0),(8,12),(128,1),(255,1)):
  before=bytes(u.mem_read(0x6D2B88,1024));want=bytearray(before);want[first*4:(first+count)*4]=palette[:count*4];ex=call(0x4CD040,first+256,count+65536,(PAL,));record('native_palette_set_copies_input_to_target_low_width_args',dict(first_argument=first+256,count_argument=count+65536,input_sha256=sha(palette)),dict(target_sha256=sha(bytes(u.mem_read(0x6D2B88,1024))),input_sha256=sha(bytes(u.mem_read(PAL,1024)))),dict(target_sha256=sha(want),input_sha256=sha(palette)),ex)
 cycle_base=0x6D2F88
 for idx in range(8):
  for low,high in ((0,0),(0,1),(1,7),(32,63),(128,255),(255,255)):
   row=cycle_base+idx*16;u.mem_write(cycle_base,bytes([0xAA]*128));before=bytearray(u.mem_read(row,16));before[3]=low;before[5]=high;u.mem_write(row,bytes(before));u.mem_write(0x6D2388,palette);want=bytearray(palette);want[low*4:(high+1)*4]=palette[high*4:(high+1)*4]+palette[low*4:high*4];ex=call(0x4CCCF0,idx)
   record('native_palette_simple_cycle_inclusive_right_rotate',dict(cycle_index=idx,low=low,high=high,stock_badlands_wpe=True),dict(palette_sha256=sha(bytes(u.mem_read(0x6D2388,1024))),cycle_sha256=sha(bytes(u.mem_read(row,16)))),dict(palette_sha256=sha(want),cycle_sha256=sha(before)),ex)
 for idx in range(8):
  for speed in (0,1,2,255):
   u.mem_write(cycle_base,bytes([0xA7]*128));before=bytes(u.mem_read(cycle_base,128));want=bytearray(before);off=idx*16;want[off+1:off+6]=bytes([speed,speed,3,0,11]);struct.pack_into('<I',want,off+8,PAL);want[off+12]=5;ex=call(0x4CD000,idx+256,speed+256,(3+256,11+256,PAL,5+256));ex2=call(0x4CD070,idx+256);want[off]=1
   record('native_palette_cycle_set_enable_exact_fields_and_low_bytes',dict(cycle_index=idx,speed=speed,padding_fixture=0xA7),dict(cycles_sha256=sha(bytes(u.mem_read(cycle_base,128)))),dict(cycles_sha256=sha(want)),dict(complete_return=True,original_instructions=ex['original_instructions']+ex2['original_instructions'],calls=ex['calls']+ex2['calls']))
 for idx in range(8):
  for n in (1,2,5,16):
   for phase in (0,n-1):
    row=cycle_base+idx*16;u.mem_write(cycle_base,bytes(128));w(row+3,3,1);w(row+5,11,1);w(row+4,phase,1);w(row+8,PAL);w(row+12,n,1);u.mem_write(0x6D2388,palette);u.mem_write(PAL,palette);want=bytearray(palette);next_phase=(phase+1)&255
    if next_phase==n:next_phase=0
    for i in range(3,12):want[i*4:i*4+4]=palette[((next_phase+i-3)%n)*4:((next_phase+i-3)%n)*4+4]
    ex=call(0x4CCC60,idx);record('native_palette_advanced_cycle_phase_and_repeated_pattern',dict(cycle_index=idx,count=n,old_phase=phase,low=3,high=11,pattern_first_stock_entries=n),dict(palette_sha256=sha(bytes(u.mem_read(0x6D2388,1024))),phase=r(row+4,1)),dict(palette_sha256=sha(want),phase=next_phase),ex)
 for val in (0,1,59,60,61,99,100,101,139,140,141,255,0xFFFFFFFF):
  ex=call(0x4CF930,val);record('native_brightness_option_unsigned_bounds',dict(argument=val),dict(stored=r(0x50F2EC)),dict(stored=val if 60<=val<=140 else 100),ex)
 u.mem_write(cycle_base,bytes([0xA5]*128));ex=call(0x4CCF00);record('native_cycle_reset_real_memset',{},dict(cycles_sha256=sha(bytes(u.mem_read(cycle_base,128)))),dict(cycles_sha256=sha(bytes(128))),ex)
 # The badlands definition is initialized native PE data at50894c. Other
 # tileset table entries point to runtime-populated BSS; their zero-file bytes
 # are not treated as the stock post-loader cycling definitions.
 definitions=bytes(u.mem_read(r(0x5089B0),36));assert definitions[:4]==bytes([7,0,0,0]);stock_cycles=[];pos=4
 while definitions[pos+4]:
  ptr,speed,lo,hi,n=struct.unpack_from('<I4B',definitions,pos);stock_cycles.append(dict(pointer=ptr,speed=speed,low=lo,high=hi,count=n));pos+=8
 assert len(stock_cycles)==3 and all(row['pointer']==0 for row in stock_cycles)
 for marker in (0,0xA5):
  u.mem_write(cycle_base,bytes([marker]*128));w(0x5264BC,0,2);ex=call(0x46A390);want=bytearray(128)
  for i,row in enumerate(stock_cycles):want[i*16:i*16+6]=bytes([1,row['speed'],row['speed'],row['low'],0,row['high']])
  record('native_badlands_cycle_bootstrap_real_selector_and_stock_definitions',dict(tileset=0,initial_cycle_byte=marker,native_definition_va='0x50894c',native_definition_sha256=sha(definitions)),dict(cycles_sha256=sha(bytes(u.mem_read(cycle_base,128)))),dict(cycles_sha256=sha(want)),ex)
 for counter in (0,1,2,8):
  call(0x46A390);u.mem_write(0x6D2388,palette)
  for i in range(3):w(cycle_base+i*16+2,counter,1)
  want_cycles=bytearray(u.mem_read(cycle_base,128));want_palette=bytearray(palette);update=counter==1
  for i,row in enumerate(stock_cycles[:2] if update else stock_cycles):
   want_cycles[i*16+2]=8 if update else (counter-1)&255
   if update:lo,hi=row['low'],row['high'];want_palette[lo*4:(hi+1)*4]=palette[hi*4:(hi+1)*4]+palette[lo*4:hi*4]
  ex=call(0x4CCD30,stop_before=0x4CF8C0)
  record('native_badlands_two_pass_cycle_driver_complete_or_upload_prefix',dict(counter=counter,first_pass_limit=128,second_pass_limit=256),dict(cycles_sha256=sha(bytes(u.mem_read(cycle_base,128))),palette_sha256=sha(bytes(u.mem_read(0x6D2388,1024))),stopped_before_upload=not ex['complete_return']),dict(cycles_sha256=sha(want_cycles),palette_sha256=sha(want_palette),stopped_before_upload=update),ex)
 call(0x46A390);u.mem_write(0x6D2388,palette);w(cycle_base+2*16+2,1,1);want_cycles=bytearray(u.mem_read(cycle_base,128));want_cycles[2*16+2]=8;want_palette=bytearray(palette);lo,hi=248,254;want_palette[lo*4:(hi+1)*4]=palette[hi*4:(hi+1)*4]+palette[lo*4:hi*4];ex=call(0x4CCBC0,cycle_base+32,2,(256,),stop_before=0x4CF8C0)
 record('native_badlands_high_palette_second_pass_update_prefix',dict(cycle_index=2,low=248,high=254,limit=256),dict(cycles_sha256=sha(bytes(u.mem_read(cycle_base,128))),palette_sha256=sha(bytes(u.mem_read(0x6D2388,1024)))),dict(cycles_sha256=sha(want_cycles),palette_sha256=sha(want_palette)),ex)
 # Positive update runs actual cycle leaves and stops before the device upload.
 for advanced in (False,True):
  for active in (0,1):
   for counter in (0,1,2,255):
    u.mem_write(cycle_base,bytes(128));w(cycle_base,active,1);w(cycle_base+1,3,1);w(cycle_base+2,counter,1);w(cycle_base+3,3,1);w(cycle_base+5,11,1);w(cycle_base+8,PAL if advanced else 0);w(cycle_base+12,5,1);u.mem_write(PAL,palette);u.mem_write(0x6D2388,palette);want=bytearray(palette);new_counter=(counter-1)&255 if active else counter;phase=0
    if active and new_counter==0:
     new_counter=3
     if advanced:
      phase=1
      for i in range(3,12):j=(phase+i-3)%5;want[i*4:i*4+4]=palette[j*4:j*4+4]
     else:want[12:48]=palette[44:48]+palette[12:44]
    ex=call(0x4CCBC0,cycle_base,0,(128,),stop_before=0x4CF8C0)
    record('native_palette_schedule_counter_and_update_before_device_upload',dict(advanced=advanced,active=active,counter=counter,speed=3,low=3,high=11),dict(palette_sha256=sha(bytes(u.mem_read(0x6D2388,1024))),counter=r(cycle_base+2,1),phase=r(cycle_base+4,1),stopped_before_upload=not ex['complete_return']),dict(palette_sha256=sha(want),counter=new_counter,phase=phase,stopped_before_upload=bool(active and counter==1)),ex)
 warp_ids=[i for i in range(999) if images_dat[999*8+i]==17]
 warp_scripts=sorted(set(struct.unpack_from('<I',images_dat,999*10+i*4)[0] for i in warp_ids));assert warp_scripts==[192] and script[12369:12372]==bytes([5,1,22])
 complete=sum(v['execution']['complete_return'] for v in cases)
 result=dict(target_sha256=source.HASH,target_version='1.17.0.1',original_code_executed=True,complete_game_executed=False,probe_script_sha256=sha(Path(__file__).read_bytes()),source_dependencies_sha256=deps,asset_manifest_sha256=sha(a.asset_manifest.read_bytes()),effect_manifest_sha256=sha(a.effect_manifest.read_bytes()),pixel_manifest_sha256=sha(a.pixel_manifest.read_bytes()),assets_sha256=provenance,stock_image_draw_method_counts=dict(Counter(images_dat[999*8:999*9])),stock_warp_in_image_ids=warp_ids,stock_warp_in_iscript_ids=warp_scripts,stock_warp_in_death_program=dict(header_offset=12348,death_pc=12369,first_commands=['wait1','end']),native_badlands_cycle_definition=dict(va='0x50894c',sha256=sha(definitions),active_mask=7,cycles=stock_cycles),runtime=dict(architecture=platform.machine(),python=sys.version,unicorn=unicorn.__version__,capstone=capstone.__version__,pefile=pefile.__version__),helper_stubs=[],imports_executed=[],instruction_bound=2000000,cases_passed=len(cases),complete_original_function_cases=complete,prefix_cases=len(cases)-complete,executed_instruction_witnesses=len(visited),cases=cases,limits=['Private 640x480 guarded indexed bitmap and declared stock resources, no RGB display or complete renderer','Existing CImage/CUnit/Sprite fixture state is authored, no unit/image allocation executes','Native texture scratch transparency is supplied private state, not proven stock initialization','Blend/fire leading lookup row supplied private0x5a; native loader allocation initialization unproven','Callbacks measured as isolated invocations, not gameplay/display frame cadence','Warp callback executes real stock192 Death wait1 only; later VM end/lifecycle is outside this batch','Palette scheduling positive update stops before unchanged device-upload entry4cf8c0; no import or helper replaced','RGB gamma/fade and device palette submission remain static boundaries'])
 a.out.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(dict(cases_passed=len(cases),complete=complete,prefix=len(cases)-complete,families=dict(Counter(v['case'] for v in cases)),pcs=len(visited),script_sha256=result['probe_script_sha256'])))
if __name__=='__main__':main()
