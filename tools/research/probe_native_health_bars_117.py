#!/usr/bin/env python3
"""Probe original bar arithmetic and indexed-buffer consumers without imports."""
import argparse,hashlib,json,platform,struct,sys
from collections import Counter
from pathlib import Path
import pefile,capstone
import inspect_native_health_bars_117 as source
from probe_native_pixel_blitters_117 import pcx
PRIVATE,PIXELS,STACK,STOP=0x800000,0x810000,0x900000,0xA00000
RECT,BITMAP,OUT,UNIT,SPRITE=PRIVATE,PRIVATE+0x40,PRIVATE+0x80,PRIVATE+0x100,PRIVATE+0x300
IMAGE,BODY,SCRIPT,ASSETS=PRIVATE+0x500,PRIVATE+0x700,0xB00000,0xC00000
WIDTH,HEIGHT=640,480
def sha(b):return hashlib.sha256(b).hexdigest()
def trunc(a,b):return (abs(a)//abs(b))*(-1 if (a<0)!=(b<0) else 1)
def signed32(v):v&=0xFFFFFFFF;return v-0x100000000 if v&0x80000000 else v
def round_model(maximum,current,width,hp):
 percent=trunc(signed32(current*100),maximum) if maximum else 100
 fill=max(3,trunc(signed32(percent*(width-1 if hp else width)),100));rem=fill%3
 return percent,fill-rem+(3 if rem>1 else 0)
def main():
 p=argparse.ArgumentParser(description=__doc__)
 for k in ('exe','data','assets','palette-manifest','image-manifest'):p.add_argument('--'+k,type=Path,required=True)
 p.add_argument('--out',type=Path);p.add_argument('--execute-original-code',action='store_true');a=p.parse_args();raw=a.exe.read_bytes();assert sha(raw)==source.HASH;deps=source.dependencies();deps[Path(source.__file__).name]=sha(Path(source.__file__).read_bytes());deps['probe_native_pixel_blitters_117.py']=sha((Path(__file__).parent/'probe_native_pixel_blitters_117.py').read_bytes());assert deps['probe_native_pixel_blitters_117.py']=='5cc11182fa6d44a99333d6252c6511e82180b857444a6fbff1834f79ccf869e2'
 if not a.execute_original_code:print(json.dumps(dict(original_code_executed=False,target_sha256=source.HASH)));return
 assert a.out and platform.machine().lower() in ('x86_64','amd64');pm=json.loads(a.palette_manifest.read_text());resource=(a.assets/'game/thpbar.pcx').read_bytes();assert sha(resource)==pm['assets']['game\\thpbar.pcx']['sha256'];pw,ph,palette=pcx(resource);assert pw==19 and ph==1
 import unicorn
 from unicorn import Uc,UC_ARCH_X86,UC_MODE_32,UC_HOOK_CODE
 from unicorn.x86_const import UC_X86_REG_ECX,UC_X86_REG_EDX,UC_X86_REG_EAX,UC_X86_REG_EIP,UC_X86_REG_ESP,UC_X86_REG_EFLAGS
 pe=pefile.PE(data=raw);u=Uc(UC_ARCH_X86,UC_MODE_32);u.mem_map(0x400000,(pe.OPTIONAL_HEADER.SizeOfImage+4095)&~4095);u.mem_write(0x400000,pe.get_memory_mapped_image())
 for addr,n in ((PRIVATE,0x10000),(PIXELS,0x60000),(STACK,0x10000),(STOP,4096),(SCRIPT,0x10000),(ASSETS,0x100000),(0xD00000,0x100000)):u.mem_map(addr,n)
 def w(addr,v,n=4):u.mem_write(addr,(v&((1<<(n*8))-1)).to_bytes(n,'little'))
 def r(addr,n=4):return int.from_bytes(u.mem_read(addr,n),'little')
 for path,base,count in (('units.dat',0x5014F0,54),('sprites.dat',0x501408,6),('images.dat',0x5011A8,14)):
  blob=(a.data/'arr'/path).read_bytes();assert sha(blob)=={'units.dat':'da2ee6f116b77329048ec6be9e02721a527fde23d4b05b1b1336e634490abcba','sprites.dat':'3e5ea29bf9937558d0a3a302cd85852c220cd126ec049c0da176c77739bb4a71','images.dat':'6b6123e2cc6771d96f914fc9bb92d34da17cc3319dc02e2db0cd10b4f3d2efdc'}[path];at=0
  for i in range(count):
   dest,width,entries=struct.unpack('<3I',pe.get_data(base-0x400000+12*i,12));part=blob[at:at+width*entries];assert len(part)==width*entries;u.mem_write(dest,part);at+=len(part)
  assert at==len(blob)
 script=(a.data/'scripts/iscript.bin').read_bytes();assert sha(script)=='cd35e04eaa8ea95cbbccc980bfaf4cae236f4b18b620dfb4a765822ec32c1cd0';u.mem_write(SCRIPT,script);w(0x54330C,SCRIPT);w(0x543300,UNIT);w(0x543304,UNIT)
 im=json.loads(a.image_manifest.read_text());grp_pointers={};grp_headers={};at=ASSETS
 for row in im['images']:
  path=row['path'];b=(a.assets/path.replace('\\','/')).read_bytes();assert sha(b)==row['sha256']
  if path not in grp_pointers:u.mem_write(at,b);grp_pointers[path]=at;at+=(len(b)+4095)&~4095
  w(0x5436F8+row['image_id']*4,grp_pointers[path]);grp_headers[row['image_id']]=b[:14]
 assert at<ASSETS+0x100000
 u.mem_write(0x534590,bytes(552));u.mem_write(0x53660C,bytes(180))
 u.mem_write(0x66CB5C,palette);w(0x6D319C,2);w(0x50F310,0);w(0x6D0C40,BITMAP);w(BITMAP,WIDTH,2);w(BITMAP+2,HEIGHT,2);w(BITMAP+4,PIXELS)
 visited=set();approved=set();ranges=list(source.BLOCKS.values());entries={v[0] for v in ranges};cases=[]
 def call(entry,ecx=0,edx=0,args=()):
  done=False;steps=0;calls=[]
  def hook(machine,pc,n,unused):
   nonlocal done,steps
   if pc==STOP:done=True;machine.emu_stop();return
   if (pc,n) not in approved:assert any(lo<=pc and pc+n<=hi for lo,hi in ranges),('Outside reviewed function',hex(pc),calls[-8:]);approved.add((pc,n))
   visited.add(pc);steps+=1
   if pc in entries:calls.append(hex(pc))
  h=u.hook_add(UC_HOOK_CODE,hook);sp=STACK+0xF000;u.mem_write(sp,struct.pack('<'+'I'*(len(args)+1),STOP,*(v&0xFFFFFFFF for v in args)));u.reg_write(UC_X86_REG_ESP,sp);u.reg_write(UC_X86_REG_ECX,ecx);u.reg_write(UC_X86_REG_EDX,edx);u.reg_write(UC_X86_REG_EAX,0);u.reg_write(UC_X86_REG_EFLAGS,2)
  try:u.emu_start(entry,STOP+1,count=2000000)
  except Exception as e:raise RuntimeError((hex(entry),hex(u.reg_read(UC_X86_REG_EIP)),calls[-8:])) from e
  finally:u.hook_del(h)
  assert done and u.reg_read(UC_X86_REG_ESP)==sp+4*(len(args)+1)
  return dict(complete_return=True,original_instructions=steps,calls=calls,return_eax=u.reg_read(UC_X86_REG_EAX))
 def record(name,inputs,actual,expected,ex):
  assert actual==expected,(name,inputs,actual,expected);cases.append(dict(case=name,inputs=inputs,actual=actual,expected=expected,passed=True,execution=ex))
 # Rounding is independent of the unit allocator, renderer and palette upload.
 for hp,entry in ((True,0x4648A0),(False,0x464920)):
  for width in range(1,256):
   for maximum,current in ((0,0),(1,0),(1,1),(100,0),(100,1),(100,33),(100,50),(100,66),(100,99),(100,100),(100,101),(100,255),(255,254),(100,-1),(100,-101)):
    ex=call(entry,maximum,current,(width,OUT));percent,fill=round_model(maximum,current,width,hp);record('native_bar_percent_truncation_minimum_and_multiple3_round',dict(hp_minus_one=hp,width=width,maximum=maximum,current=current,synthetic_raw_integer_profile=True),dict(percent=signed32(ex['return_eax']),fill=r(OUT)),dict(percent=percent,fill=fill),ex)
 baseline=bytes((i*17+(i//WIDTH)*31)&255 for i in range(WIDTH*HEIGHT));guard=bytes([0xE7]*16)
 def draw_model(which,crop,fill,x,y,scheme,want):
  left,top,cw,ch=crop
  if ch!=(5 if which=='health' else 7):return
  filled=min(cw,max(0,fill-left));blank=cw-filled
  for row in range(ch):
   for xx in range(cw):
    index=r(scheme+1+row,1) if xx<filled else r((0x5085EC if which=='health' else 0x50860C)+row,1);want[(y+row)*WIDTH+x+xx]=palette[index]
  first=x+(3-left%3 if left and left%3 else 0)
  for xx in range(first,x+cw,3):
   for row in range(ch):want[(y+row)*WIDTH+xx]=palette[18]
 for which,entry,height,schemes in (('health',0x4649A0,5,(0x5085D0,0x5085D6,0x5085DC,0x5085E4)),('shield',0x464AB0,7,(0x5085F4,0x5085FC,0x508604))):
  for left in (0,1,2,3,4,11):
   for width in (1,2,3,19,31,63):
    for fill in (0,1,3,12,30,100):
     for scheme in schemes:
      for ch in (height,height-1):
       crop=(left,0,width,ch);u.mem_write(RECT,struct.pack('<4I',*crop));u.mem_write(PIXELS-16,guard+baseline+guard);ex=call(entry,7,9,(RECT,fill,scheme));want=bytearray(baseline);draw_model(which,crop,fill,7,9,scheme,want)
       record('native_health_energy_shield_row_colors_fill_clip_and_separators',dict(which=which,crop=list(crop),fill=fill,class_table_va=hex(scheme)),dict(bitmap_sha256=sha(bytes(u.mem_read(PIXELS-16,len(baseline)+32)))),dict(bitmap_sha256=sha(guard+want+guard)),ex)
 # Original virtual-header pool preparation and complete stock initialization.
 # Marine sprite130 and its native selection circle define the actual bar box.
 for x,y in ((200,200),(0,0),(635,395),(800,600)):
  u.mem_write(IMAGE,bytes(0x40));u.mem_write(0x592BA8,bytes(14));u.mem_write(UNIT,bytes(0x150));u.mem_write(SPRITE,bytes(0x40));w(UNIT+0x64,0,2);w(UNIT+0x0C,SPRITE);w(SPRITE+8,130,2);w(SPRITE+0x14,x,2);w(SPRITE+0x16,y,2);w(0x6A6070,UNIT);w(0x543218,0);w(0x54321C,0)
  prep=call(0x40F810,IMAGE,args=(0,));ex=call(0x40F6E0,IMAGE,args=(SPRITE,));native_width=r(0x51EE36+130,1);rem=(native_width-1)-trunc(native_width-1,3)*3;fw=max(19,(native_width-rem)&255);fh=5;circle=r(0x51F3CE+130,1);dy=(grp_headers[561+circle][9]//2+8+r(0x51F556+130,1))&255;dy=dy-256 if dy&128 else dy
  header=bytearray(14);struct.pack_into('<3H',header,0,1,fw,fh);header[8]=fw;header[9]=fh;want=bytearray(0x40);struct.pack_into('<H',want,8,560);struct.pack_into('<H',want,0x0A,11);struct.pack_into('<H',want,0x0C,1);want[0x0F]=dy&255;struct.pack_into('<3H',want,0x10,31126,31164,0);want[0x17]=124;struct.pack_into('<I',want,0x2C,0x592BA8);struct.pack_into('<I',want,0x30,UNIT);struct.pack_into('<I',want,0x34,r(0x503158));struct.pack_into('<I',want,0x38,r(0x50309C));struct.pack_into('<I',want,0x3C,SPRITE)
  wx=x-fw//2;wy=y+dy-fh//2;sx=max(wx,0);sy=max(wy,0);left=max(-wx,0);top=max(-wy,0);vw=min(fw+min(wx,0),640-sx);vh=min(fh+min(wy,0),400-sy);struct.pack_into('<8h',want,0x1C,wx,wy,sx,sy,left,top,vw,vh)
  record('native_marine_health_image_virtual_header_and_stock_init_wait125',dict(sprite_id=130,unit_type=0,sprite_position=[x,y],stock_iscript=289,stock_init_pc=31162,header_pool_index=0),dict(image_sha256=sha(bytes(u.mem_read(IMAGE,0x40))),header_sha256=sha(bytes(u.mem_read(0x592BA8,14)))),dict(image_sha256=sha(want),header_sha256=sha(header)),dict(complete_return=True,original_instructions=prep['original_instructions']+ex['original_instructions'],calls=prep['calls']+ex['calls']))
 def shield_top_model(crop,fill,x,y,want):
  left,top,cw,ch=crop
  if ch!=7:return
  filled=min(cw,max(0,fill-left));blank=cw-filled;fill_x=x;blank_x=x+filled
  for row in range(4):
   for xx in range(filled):want[(y+row)*WIDTH+fill_x+xx]=palette[r(0x508614+row,1)]
   for xx in range(blank):want[(y+row)*WIDTH+blank_x+xx]=palette[r(0x508618+row,1)]
   if left==0:
    n=1 if row==1 else 2
    if filled:filled-=n;fill_x+=n
    elif blank:blank-=n;blank_x+=n
  first=x+(3-left%3 if left and left%3 else 0)+3
  for xx in range(first,x+cw,3):
   for row in range(4):want[(y+row)*WIDTH+xx]=palette[18]
  if left==0:
   edge_x=x
   for row in range(4):
    want[(y+row)*WIDTH+edge_x:(y+row)*WIDTH+edge_x+2]=bytes([palette[18]]*2);edge_x+=1 if row==1 else 2
 for left in (0,1,2,3,4,11):
  for width in (3,19,31,63):
   for fill in (3,12,30,100):
    crop=(left,0,width,7);u.mem_write(RECT,struct.pack('<4I',*crop));u.mem_write(PIXELS-16,guard+baseline+guard);ex=call(0x464BC0,7,9,(RECT,fill));want=bytearray(baseline);shield_top_model(crop,fill,7,9,want)
    record('native_shield_blue_overlay_bevel_and_separators',dict(crop=list(crop),fill=fill),dict(bitmap_sha256=sha(bytes(u.mem_read(PIXELS-16,len(baseline)+32)))),dict(bitmap_sha256=sha(guard+want+guard)),ex)
 # Complete method11 uses unit data and current unit fields, not GRP pixels.
 for ident in (0,1,7,8,12,40,64,65,67,68,72,73,106,131):
  raw_max=r(0x520708+ident*4);max_hp=raw_max>>8;max_shield=r(0x520540+ident*2,2);has_shield=bool(r(0x520458+ident,1));flags=r(0x521660+ident*4);native_width=r(0x51EE36+130,1)
  for fraction in (0,1,33,66,100):
   current_hp=raw_max*fraction//100;current_shield=max_shield*256*fraction//100
   for own in (False,True):
    for hallucination in (False,True):
     for crop in ((0,0,19,13 if has_shield else 11),(1,1,17,12 if has_shield else 10)):
      left,top,cw,ch=crop;u.mem_write(UNIT,bytes(0x150));u.mem_write(SPRITE,bytes(0x40));w(UNIT+0x64,ident,2);w(UNIT+0x0C,SPRITE);w(UNIT+8,current_hp);w(UNIT+0x60,current_shield);w(UNIT+0x4C,3,1);w(UNIT+0xDC,0x40000000 if hallucination else 0);w(UNIT+0xA2,100*256,2);w(UNIT+0x110,675 if hallucination else 900,2);w(UNIT+0x112,250,2);w(UNIT+0x114,150,1);w(SPRITE+8,130,2);w(0x508D50,3 if own else 4);before=bytes(u.mem_read(UNIT,0x150));u.mem_write(RECT,struct.pack('<4I',*crop));u.mem_write(PIXELS-16,guard+baseline+guard);ex=call(0x464D20,7,9,(0,RECT,UNIT));want=bytearray(baseline)
      hp=(current_hp+255)>>8;hp_max=max_hp or hp or 1;percent,filled=round_model(hp_max,hp,native_width,True);hp_height=min(ch,7 if has_shield else 5);scheme=(0x5085F4 if has_shield else 0x5085D0)+(0 if percent>=66 else 8 if has_shield and percent>=33 else 6 if not has_shield and percent>=33 else 16 if has_shield else 12)
      draw_model('shield' if has_shield else 'health',(left,top,cw,hp_height),filled,7,9,scheme,want)
      if has_shield:
       shield_percent,shield_fill=round_model(max_shield,current_shield>>8,native_width,False);shield_top_model((left,top,cw,hp_height),shield_fill,7,9,want)
      offset=8 if has_shield else 6;is_caster=bool(flags&0x200000);meter=(is_caster and not hallucination) or hallucination or ident==40
      if own and meter and ch-offset>0:
       energy=is_caster and not hallucination;maximum=250 if energy and flags&0x40 else 200 if energy else 1350 if hallucination else 1800;current=100 if energy else 675 if hallucination else 900;_,fill=round_model(maximum,current,native_width,False);draw_model('health',(left,top+offset,cw,ch-offset),fill,7,9+offset,0x5085E4,want)
      record('complete_native_unit_bars_stock_properties_hp_shield_energy_timed_and_matrix',dict(unit_type=ident,hp_fraction_percent=fraction,shield_fraction_percent=fraction,owner=3,viewer=3 if own else 4,hallucination=hallucination,crop=list(crop),sprite_id=130,authored_existing_unit_profile=True,matrix_hp_field=250,matrix_timer=150),dict(bitmap_sha256=sha(bytes(u.mem_read(PIXELS-16,len(baseline)+32))),unit_sha256=sha(bytes(u.mem_read(UNIT,0x150)))),dict(bitmap_sha256=sha(guard+want+guard),unit_sha256=sha(before)),ex)
 # Matrix state alone does not alter this original display consumer. The actual
 # whole unit is compared unchanged for each run; this is not damage semantics.
 for matrix_hp,matrix_timer in ((0,0),(1,1),(250,150),(65535,255)):
  u.mem_write(UNIT,bytes(0x150));u.mem_write(SPRITE,bytes(0x40));w(UNIT+0x64,0,2);w(UNIT+0x0C,SPRITE);w(UNIT+8,20*256);w(UNIT+0x4C,3,1);w(UNIT+0x112,matrix_hp,2);w(UNIT+0x114,matrix_timer,1);w(SPRITE+8,130,2);w(0x508D50,3);crop=(0,0,19,5);u.mem_write(RECT,struct.pack('<4I',*crop));u.mem_write(PIXELS-16,guard+baseline+guard);before=bytes(u.mem_read(UNIT,0x150));ex=call(0x464D20,7,9,(0,RECT,UNIT));_,filled=round_model(40,20,r(0x51EE36+130,1),True);want=bytearray(baseline);draw_model('health',crop,filled,7,9,0x5085D6,want)
  record('native_marine_display_matrix_fields_discriminating_delta',dict(matrix_hp=matrix_hp,matrix_timer=matrix_timer),dict(bitmap_sha256=sha(bytes(u.mem_read(PIXELS-16,len(baseline)+32))),unit_sha256=sha(bytes(u.mem_read(UNIT,0x150)))),dict(bitmap_sha256=sha(guard+want+guard),unit_sha256=sha(before)),ex)
 # Energy extension executes the real upgrade accessor on both native banks.
 for ident in (1,8,12,45,46,60,63,67,71):
  for upgraded in (False,True):
   u.mem_write(0x534590,bytes([int(upgraded)]*552));u.mem_write(0x53660C,bytes([int(upgraded)]*180));u.mem_write(UNIT,bytes(0x150));u.mem_write(SPRITE,bytes(0x40));w(UNIT+0x64,ident,2);w(UNIT+0x0C,SPRITE);w(UNIT+8,r(0x520708+ident*4));w(UNIT+0x60,r(0x520540+ident*2,2)*256);w(UNIT+0xA2,200*256,2);w(UNIT+0x4C,3,1);w(SPRITE+8,130,2);w(0x508D50,3);has_shield=bool(r(0x520458+ident,1));crop=(0,0,19,13 if has_shield else 11);u.mem_write(RECT,struct.pack('<4I',*crop));u.mem_write(PIXELS-16,guard+baseline+guard);ex=call(0x464D20,7,9,(0,RECT,UNIT));want=bytearray(baseline);_,filled=round_model(r(0x520708+ident*4)>>8,r(0x520708+ident*4)>>8,r(0x51EE36+130,1),True);draw_model('shield' if has_shield else 'health',(0,0,19,7 if has_shield else 5),filled,7,9,0x5085F4 if has_shield else 0x5085D0,want)
   if has_shield:_,sf=round_model(r(0x520540+ident*2,2),r(0x520540+ident*2,2),r(0x51EE36+130,1),False);shield_top_model((0,0,19,7),sf,7,9,want)
   if r(0x521660+ident*4)&0x200000:
    maximum=250 if upgraded or r(0x521660+ident*4)&0x40 else 200;_,fill=round_model(maximum,200,r(0x51EE36+130,1),False);offset=8 if has_shield else 6;draw_model('health',(0,offset,19,5),fill,7,9+offset,0x5085E4,want)
   record('native_owned_energy_meter_real_stock_type_and_upgrade_banks',dict(unit_type=ident,all_upgrade_levels=int(upgraded),energy_fixed_point=200*256,sprite_id=130,authored_existing_profile=True),dict(bitmap_sha256=sha(bytes(u.mem_read(PIXELS-16,len(baseline)+32)))),dict(bitmap_sha256=sha(guard+want+guard)),ex)
 u.mem_write(0x534590,bytes(552));u.mem_write(0x53660C,bytes(180))
 # The actual selected health-image pool entry is consumed, linked to the
 # existing sprite and rendered through the original sprite dispatcher.
 # tunit is a graphics input, so read it through the declared health scratch.
 # It is extracted separately from the existing safe pixel asset manifest.
 player_path=a.assets/'game/tunit.pcx'
 assert player_path.exists(),'Extract game\\tunit.pcx into the declared health assets'
 player_raw=player_path.read_bytes();assert sha(player_raw)=='b488309e58eb8b26f8b19c4078ee3743dae79772bb3b47681f1f5c2f60b21ee1';player=pcx(player_raw)[2];u.mem_write(0x529056,player[:96]);w(0x671A88,0,1)
 for x,y in ((200,200),(0,0),(635,370),(800,600)):
  u.mem_write(IMAGE,bytes(0x40));u.mem_write(BODY,bytes(0x40));u.mem_write(0x592BA8,bytes(14));u.mem_write(UNIT,bytes(0x150));u.mem_write(SPRITE,bytes(0x40));w(UNIT+0x64,0,2);w(UNIT+0x0C,SPRITE);w(UNIT+8,20*256);w(UNIT+0x4C,3,1);w(SPRITE+8,130,2);w(SPRITE+0x0A,3,1);w(SPRITE+0x0E,8,1);w(SPRITE+0x14,x,2);w(SPRITE+0x16,y,2);w(SPRITE+0x18,BODY);w(SPRITE+0x1C,BODY);w(SPRITE+0x20,BODY);w(0x6A6070,UNIT);w(0x543218,0);w(0x54321C,0);w(0x59289C,IMAGE);w(0x592BA0,IMAGE);prep=call(0x40F810,IMAGE,args=(0,));attach=call(0x40EB60,SPRITE);before=bytearray(u.mem_read(IMAGE,0x40));bounds=list(struct.unpack('<8h',before[0x1C:0x2C]));wx,wy,dx,dy,left,top,cw,ch=bounds;u.mem_write(PIXELS-16,guard+baseline+guard);ex=call(0x415A80,SPRITE);want=bytearray(baseline)
  if cw>0 and ch>0:_,filled=round_model(40,20,r(0x51EE36+130,1),True);draw_model('health',(left,top,cw,min(ch,5)),filled,dx,dy,0x5085D6,want)
  struct.pack_into('<H',before,0x0C,struct.unpack_from('<H',before,0x0C)[0]&~1)
  record('native_selected_health_pool_attach_list_and_full_sprite_dispatch',dict(sprite_position=[x,y],unit_type=0,sprite_id=130,selected_index=0,authored_existing_unit_and_sprite=True,existing_body_has_zero_visible_dimensions=True),dict(pool_head=r(0x59289C),pool_tail=r(0x592BA0),sprite_first_image=r(SPRITE+0x20),sprite_last_image=r(SPRITE+0x1C),returned_image=attach['return_eax'],body_next=r(BODY),image_previous=r(IMAGE+4),image_sha256=sha(bytes(u.mem_read(IMAGE,0x40))),bitmap_sha256=sha(bytes(u.mem_read(PIXELS-16,len(baseline)+32)))),dict(pool_head=0,pool_tail=0,sprite_first_image=BODY,sprite_last_image=IMAGE,returned_image=IMAGE,body_next=IMAGE,image_previous=BODY,image_sha256=sha(before),bitmap_sha256=sha(guard+want+guard)),dict(complete_return=True,original_instructions=prep['original_instructions']+attach['original_instructions']+ex['original_instructions'],calls=prep['calls']+attach['calls']+ex['calls']))
 # Damage/position callers mark existing bars; they do not create an image.
 nodes=[IMAGE,IMAGE+0x80,IMAGE+0x100]
 for methods in ([],[0],[11],[0,11,11],[11,0,11]):
  for has_bar_flag in (False,True):
   u.mem_write(SPRITE,bytes(0x40));w(SPRITE+0x0E,8 if has_bar_flag else 0,1);w(SPRITE+0x1C,nodes[0] if methods else 0);expected=[]
   for i,method in enumerate(methods):u.mem_write(nodes[i],bytes(0x40));w(nodes[i]+4,nodes[i+1] if i+1<len(methods) else 0);w(nodes[i]+0x0A,method,1);w(nodes[i]+0x0C,0x82,2);expected.append(0x83 if method==11 else 0x82)
   before=bytes(u.mem_read(SPRITE,0x40));ex=call(0x416BB0,SPRITE);record('native_dirty_marks_all_existing_health_images_without_flag_guard_or_allocation',dict(image_methods=methods,sprite_has_bar_flag=has_bar_flag),dict(image_flags=[r(nodes[i]+0x0C,2) for i in range(len(methods))],sprite_sha256=sha(bytes(u.mem_read(SPRITE,0x40)))),dict(image_flags=expected,sprite_sha256=sha(before)),ex)
 # Stock method15 uses its frame header, not the blackx pixel packets. Compose
 # original bounds and dispatcher for every effective frame and both mirrors.
 blackx=next(row for row in im['images'] if row['image_id']==583)
 grp=grp_pointers[blackx['path']];canvas_w=r(grp+2,2);canvas_h=r(grp+4,2)
 for frame in range(blackx['frames']):
  fh=grp+6+frame*8;fx,fy,fw,fheight=struct.unpack('<4B',bytes(u.mem_read(fh,4)))
  for mirror in (False,True):
   for x,y in ((200,200),(0,0),(635,395),(800,600)):
    u.mem_write(SPRITE,bytes(0x40));u.mem_write(IMAGE,bytes(0x40));w(SPRITE+0x0A,3,1);w(SPRITE+0x14,x,2);w(SPRITE+0x16,y,2);w(SPRITE+0x18,IMAGE);w(SPRITE+0x20,IMAGE);w(SPRITE+0x1C,IMAGE);w(IMAGE+8,583,2);w(IMAGE+0x0A,15,1);w(IMAGE+0x0C,3 if mirror else 1,2);w(IMAGE+0x1A,frame,2);w(IMAGE+0x2C,grp);w(IMAGE+0x34,r(0x5030D4+15*12));w(IMAGE+0x38,r(0x503044+15*8));w(IMAGE+0x3C,SPRITE);w(0x543218,0);w(0x54321C,0);w(0x6D237A,197,1);w(0x5D6E38,11);w(0x671A88,0,1)
    prep=call(0x40EE70,IMAGE);u.mem_write(PIXELS-16,guard+baseline+guard);ex=call(0x415A80,SPRITE);want=bytearray(baseline);wx=x+(canvas_w//2-fw-fx if mirror else fx-canvas_w//2);wy=y+fy-canvas_h//2;dx=max(wx,0);dy=max(wy,0);left=max(-wx,0);top=max(-wy,0);cw=min(fw+min(wx,0),640-dx);ch=min(fheight+min(wy,0),400-dy)
    if cw>0 and ch>0:
     for yy in range(ch):
      for xx in range(cw):
       if (top==0 and yy==0) or (left==0 and xx==0) or (left+cw==fw and xx==cw-1) or (top+ch==fheight and yy==ch-1):want[(dy+yy)*WIDTH+dx+xx]=197
    record('stock_size_outline_all_frames_mirrors_bounds_and_sprite_dispatch',dict(image_id=583,grp_sha256=blackx['sha256'],frame=frame,mirror=mirror,sprite_position=[x,y],outline_color=197),dict(bitmap_sha256=sha(bytes(u.mem_read(PIXELS-16,len(baseline)+32))),image_dirty=r(IMAGE+0x0C,2)&1),dict(bitmap_sha256=sha(guard+want+guard),image_dirty=0),dict(complete_return=True,original_instructions=prep['original_instructions']+ex['original_instructions'],calls=prep['calls']+ex['calls']))
 # The original selection wrapper accepts an already-existing circle, attaches
 # a real health pool entry once, and release returns both images to real pools.
 for x,y in ((200,200),(0,0),(635,370),(800,600)):
  u.mem_write(IMAGE,bytes(0x40));u.mem_write(BODY,bytes(0x40));u.mem_write(0x592BA8,bytes(14));u.mem_write(UNIT,bytes(0x150));u.mem_write(SPRITE,bytes(0x40));w(UNIT+0x64,0,2);w(UNIT+0x0C,SPRITE);w(SPRITE+8,130,2);w(SPRITE+0x0E,1,1);w(SPRITE+0x14,x,2);w(SPRITE+0x16,y,2);w(SPRITE+0x18,BODY);w(SPRITE+0x1C,BODY);w(SPRITE+0x20,BODY);w(BODY+8,561,2);w(BODY+0x30,6,1);w(BODY+0x3C,SPRITE);w(0x6A6070+5*4,UNIT);w(0x59289C,IMAGE);w(0x592BA0,IMAGE);w(0x592BA4,0);w(0x594050,0);w(0x543218,0);w(0x54321C,0);w(0x6D0BF5,0,1);u.mem_write(0x6D06D0,bytes(1280));prep=call(0x40F810,IMAGE,args=(0,));attach=call(0x417110,SPRITE,args=(5,6));second=call(0x417110,SPRITE,args=(7,2));bounds=struct.unpack('<8h',bytes(u.mem_read(IMAGE+0x1C,16)));ex=call(0x415A00,SPRITE);wx,wy,dx,dy,left,top,cw,ch=bounds;dirty=bytearray(1280)
  if cw>0 and ch>0:
   for yy in range(dy//16,(dy+ch+15)//16+1):
    for xx in range(dx//16,(dx+cw+15)//16+1):dirty[yy*40+xx]=1
  record('native_selected_health_attach_once_and_both_pool_release',dict(sprite_position=[x,y],selected_index_first=5,selected_index_second=7,existing_zero_dimension_circle=True,real_empty_release_pools=True),dict(sprite_flags=r(SPRITE+0x0E,1),selected_index=r(SPRITE+0x0B,1),sprite_first=r(SPRITE+0x20),sprite_last=r(SPRITE+0x1C),health_free_head=r(0x59289C),health_free_tail=r(0x592BA0),circle_free_head=r(0x592BA4),circle_free_tail=r(0x594050),returned_circle_color=ex['return_eax']&255,dirty_grid_sha256=sha(bytes(u.mem_read(0x6D06D0,1280))),dirty_flag=r(0x6D0BF5,1)),dict(sprite_flags=0,selected_index=5,sprite_first=0,sprite_last=0,health_free_head=IMAGE,health_free_tail=IMAGE,circle_free_head=BODY,circle_free_tail=BODY,returned_circle_color=6,dirty_grid_sha256=sha(dirty),dirty_flag=2 if cw>0 and ch>0 else 0),dict(complete_return=True,original_instructions=sum(z['original_instructions'] for z in (prep,attach,second,ex)),calls=sum((z['calls'] for z in (prep,attach,second,ex)),[])))
 # Original detected-cloak owner setup rewrites the eight team-color rows of
 # the trans50 bank. Parent methods outside5..7 leave the bank untouched.
 blend_path=a.assets/'tileset/badlands/trans50.pcx';blend_raw=blend_path.read_bytes();assert sha(blend_raw)==pm['assets']['tileset\\badlands\\trans50.pcx']['sha256'];blend=bytes([0x5A]*256)+pcx(blend_raw)[2];assert len(blend)==65536;bank=0xD00000;w(0x508890,bank)
 assigned=list(reversed(range(8)))
 for i,color in enumerate(assigned):w(0x5264FC+i*4,color)
 for method in range(18):
  for owner in range(12):
   for mode in (0,1,2):
    for allied in (0,1,2):
     u.mem_write(bank,blend);w(0x671A88,mode,1);w(0x508D50,3);u.mem_write(0x534914,bytes(144));w(0x534914+3*12+owner,allied,1);w(0x5D6E38,11);ex=call(0x416BD0,method+256,owner+256);want=bytearray(blend)
     if 5<=method<=7:
      desired=2 if owner==3 else 7 if allied else 0;color=assigned.index(desired) if mode==2 else owner
      for i in range(8):source_color=player[color*8+i];want[(8+i)*256:(9+i)*256]=want[source_color*256:(source_color+1)*256]
     record('native_detected_cloak_parent_method_and_owner_blend_rows',dict(parent_method_argument=method+256,owner_argument=owner+256,raw_color_mode=mode,viewer=3,alliance_byte=allied,color_assignments=assigned,leading_row_fixture_byte=0x5A),dict(bank_sha256=sha(bytes(u.mem_read(bank,len(blend)))),current_color_cache=r(0x5D6E38)),dict(bank_sha256=sha(want),current_color_cache=11),ex)
 result=dict(target_sha256=source.HASH,target_version='1.17.0.1',original_code_executed=True,complete_game_executed=False,probe_script_sha256=sha(Path(__file__).read_bytes()),source_dependencies_sha256=deps,palette_manifest_sha256=sha(a.palette_manifest.read_bytes()),image_manifest_sha256=sha(a.image_manifest.read_bytes()),assets_sha256={'game\\thpbar.pcx':sha(resource),'game\\tunit.pcx':sha(player_raw),'tileset\\badlands\\trans50.pcx':sha(blend_raw),'arr\\units.dat':sha((a.data/'arr/units.dat').read_bytes()),'arr\\sprites.dat':sha((a.data/'arr/sprites.dat').read_bytes()),'arr\\images.dat':sha((a.data/'arr/images.dat').read_bytes()),'scripts\\iscript.bin':sha(script)},runtime=dict(architecture=platform.machine(),python=sys.version,unicorn=unicorn.__version__,capstone=capstone.__version__,pefile=pefile.__version__),helper_stubs=[],imports_executed=[],instruction_bound=2000000,cases_passed=len(cases),complete_original_function_cases=len(cases),prefix_cases=0,executed_instruction_witnesses=len(visited),cases=cases,limits=['Private indexed buffers; no complete renderer, RGB observation or game executes','Authored parameter states and stock color bytes; no CUnit allocation executes','Bar type profiles use declared Marine sprite130; only14 stock unit-type DAT profiles, not all campaign units','Existing main/body image in pool attachment has zero visible dimensions; populated body rendering outside that profile','Detected blend bank has explicit private leading row0x5a; native initialization is not claimed'])
 a.out.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(dict(cases_passed=len(cases),families=dict(Counter(v['case'] for v in cases)),pcs=len(visited),script_sha256=result['probe_script_sha256'])))
if __name__=='__main__':main()
