#!/usr/bin/env python3
"""Execute complete original GRP bounds, LO offsets, directions, rows and depth keys.
Retail assets stay in scratch. Exhaustive stock-derived outputs are compared then
hashed for evidence rather than copied as a bulk retail coordinate dataset.
"""
import argparse,hashlib,json,platform,struct,sys
from collections import Counter
from pathlib import Path
import capstone,pefile
import inspect_native_image_geometry_117 as source
import inspect_native_image_initialization_117 as image_source
import inspect_native_primary_orders_117 as order_source
import inspect_native_unit_creation_117 as creation_source
import inspect_native_training_lifecycle_117 as lifecycle_source
IMAGE,SPRITE,OUT,POOL=0x800000,0x802000,0x803000,0x805000
STACK,STOP,ASSETS,SCRIPT=0x900000,0xA00000,0xC00000,0xB00000
MASK=0xFFFFFFFF

def main():
 p=argparse.ArgumentParser(description=__doc__)
 for k in ('exe','data','manifest','assets','asset-manifest'):p.add_argument('--'+k,type=Path,required=True)
 p.add_argument('--out',type=Path);p.add_argument('--execute-original-code',action='store_true');a=p.parse_args()
 raw=a.exe.read_bytes();assert hashlib.sha256(raw).hexdigest()==source.HASH
 deps={Path(m.__file__).name:hashlib.sha256(Path(m.__file__).read_bytes()).hexdigest() for m in (source,image_source,order_source,creation_source,lifecycle_source)}
 assert deps['inspect_native_image_initialization_117.py']=='cb3cc883fe51a3109910928a51fd0c2a65251bf23782bec1cebb093c722342a6'
 assert deps['inspect_native_primary_orders_117.py']=='1888da84f424eff2157e68db2af5604e940de4cb6d84d3f29fe11543c8e0e839'
 assert deps['inspect_native_unit_creation_117.py']=='991cd331c288f3a0351729407801eebccf11d6db0516624235382cbdc8fb758e'
 assert deps['inspect_native_training_lifecycle_117.py']=='6c17063b98c6bb730a3906c7694c191c10228b7b41a971fe15a5bb1d90bc6b61'
 if not a.execute_original_code:print(json.dumps(dict(target_sha256=source.HASH,reviewed_blocks=len(source.BLOCKS),original_code_executed=False)));return
 assert a.out and platform.machine().lower() in ('x86_64','amd64')
 manifest=json.loads(a.manifest.read_text());am=json.loads(a.asset_manifest.read_text());assert am['retail_manifest_sha256']==hashlib.sha256(a.manifest.read_bytes()).hexdigest()
 blobs={};provenance={}
 for row in am['images']+am['lo_files']:
  path=row['path'];b=(a.assets/path.replace('\\','/')).read_bytes();assert hashlib.sha256(b).hexdigest()==row['sha256'];blobs[path]=b;provenance[path]=row['sha256']
 def stock(path):
  b=(a.data/path.replace('\\','/')).read_bytes();assert hashlib.sha256(b).hexdigest()==manifest['effective'][path]['sha256'];return b
 images_dat=stock('arr\\images.dat');script=stock('scripts\\iscript.bin')
 import unicorn
 from unicorn import Uc,UC_ARCH_X86,UC_MODE_32,UC_HOOK_CODE
 from unicorn.x86_const import UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_EDX,UC_X86_REG_ESP,UC_X86_REG_EIP
 pe=pefile.PE(data=raw);assert pe.OPTIONAL_HEADER.ImageBase==0x400000 and pe.FILE_HEADER.Machine==0x14C
 u=Uc(UC_ARCH_X86,UC_MODE_32);u.mem_map(0x400000,(pe.OPTIONAL_HEADER.SizeOfImage+4095)&~4095);u.mem_write(0x400000,pe.get_memory_mapped_image())
 for address,size in ((IMAGE,0x40000),(STACK,0x10000),(STOP,4096),(SCRIPT,0x10000),(ASSETS,0x600000)):u.mem_map(address,size)
 def w(addr,val,size=4):u.mem_write(addr,(val&((1<<(size*8))-1)).to_bytes(size,'little'))
 def r(addr,size=4):return int.from_bytes(u.mem_read(addr,size),'little')
 pointers={};at=ASSETS
 for path,b in blobs.items():u.mem_write(at,b);pointers[path]=at;at+=(len(b)+4095)&~4095
 assert at<ASSETS+0x600000
 for row in am['images']:w(0x5436F8+4*row['image_id'],pointers[row['path']])
 for row in am['lo_files']:
  address=(0x596400+4*(row['kind']*999+row['image_id'])) if row['kind']<5 else 0x59B210+4*row['image_id']
  w(address,pointers[row['path']])
 offset=0;columns=[]
 for i in range(14):
  destination,width,count=struct.unpack('<3I',pe.get_data(0x1011A8+i*12,12));n=width*count;part=images_dat[offset:offset+n];assert len(part)==n;u.mem_write(destination,part);columns.append((destination,width,count,part));offset+=n
 assert offset==len(images_dat)
 u.mem_write(SCRIPT,script);w(0x54330C,SCRIPT);w(0x543300,0);w(0x543304,0)
 cases=[];visited=set();approved=set();static_only={'image_resource_load_static','image_resource_release_static'}
 ranges=[v for k,v in source.BLOCKS.items() if k not in static_only]
 entries={v[0] for v in ranges}
 def call(entry,ecx=IMAGE,edx=0,args=()):
  done=False;steps=0;calls=[]
  def hook(machine,pc,size,unused):
   nonlocal done,steps
   if pc==STOP:done=True;machine.emu_stop();return
   if (pc,size) not in approved:
    assert any(lo<=pc and pc+size<=hi for lo,hi in ranges),('Outside reviewed helper',hex(pc),calls[-8:]);approved.add((pc,size))
   visited.add(pc);steps+=1
   if pc in entries:calls.append(hex(pc))
  h=u.hook_add(UC_HOOK_CODE,hook);sp=STACK+0xF000;u.mem_write(sp,struct.pack('<'+'I'*(len(args)+1),STOP,*(v&MASK for v in args)))
  u.reg_write(UC_X86_REG_ESP,sp);u.reg_write(UC_X86_REG_ECX,ecx);u.reg_write(UC_X86_REG_EDX,edx);u.reg_write(UC_X86_REG_EAX,0)
  try:u.emu_start(entry,STOP+1,count=100000)
  except Exception as e:raise RuntimeError((hex(entry),hex(u.reg_read(UC_X86_REG_EIP)),calls[-10:])) from e
  finally:u.hook_del(h)
  assert done and u.reg_read(UC_X86_REG_ESP)==sp+4*(len(args)+1),(hex(entry),'incomplete original return')
  return dict(complete_return=True,original_instructions=steps,return_eax=u.reg_read(UC_X86_REG_EAX),calls=calls)
 def packed(value):return json.dumps(value,separators=(',',':'),sort_keys=True).encode()
 def record(name,inputs,actual,expected,ex,compact=False):
  assert actual==expected,(name,inputs,actual,expected)
  if compact:actual=hashlib.sha256(packed(actual)).hexdigest();expected=hashlib.sha256(packed(expected)).hexdigest()
  cases.append(dict(case=name,inputs=inputs,actual=actual,expected=expected,passed=True,output_representation='SHA256 of measured/expected JSON' if compact else 'measured scalar state',execution=ex))
 def image_fixture(ident=239,frame=0,flags=0,x=200,y=200,dx=0,dy=0):
  u.mem_write(IMAGE,bytes(0x1000));u.mem_write(SPRITE,bytes(0x100));w(IMAGE+8,ident,2);w(IMAGE+0x1A,frame,2);w(IMAGE+0xC,flags,2);w(IMAGE+0x3C,SPRITE);w(IMAGE+0x2C,r(0x5436F8+ident*4));w(IMAGE+0xE,dx,1);w(IMAGE+0xF,dy,1);w(SPRITE+0x14,x,2);w(SPRITE+0x16,y,2);w(SPRITE+0x18,IMAGE);w(0x543218,0);w(0x54321C,0)
 def signed(v,n):v&=(1<<n)-1;return v-(1<<n) if v&(1<<(n-1)) else v
 def bounds_model(raw_grp,frame,flags,x,y,dx,dy,cx,cy,old_y):
  gw,gh=struct.unpack_from('<2H',raw_grp,2);fx,fy,fw,fh=struct.unpack_from('<4B',raw_grp,6+frame*8)
  wx=signed(signed(x,16)+signed(dx,8)+(gw//2-fw-fx if flags&2 else fx-gw//2),16)
  wy=signed(old_y if flags&4 else signed(y,16)+signed(dy,8)+fy-gh//2,16)
  sx=signed(wx-cx,32);sy=signed(wy-cy,32);vw=fw+min(sx,0);vh=fh+min(sy,0)
  return [wx,wy,signed(max(sx,0),16),signed(max(sy,0),16),signed(max(-sx,0),16),signed(max(-sy,0),16),signed(min(vw,640-max(sx,0)),16),signed(min(vh,400-max(sy,0)),16)]
 unique={row['path']:row for row in am['images']}
 for path,row in sorted(unique.items()):
  b=blobs[path];frames,width,height=struct.unpack_from('<3H',b);assert frames==row['frames']
  for frame in range(frames):
   for mirror in (0,1):
    flags=2*mirror;image_fixture(row['image_id'],frame,flags,200,200,-7,11);ex=call(0x40EE70)
    actual=list(struct.unpack('<8h',u.mem_read(IMAGE+0x1C,16)));want=bounds_model(b,frame,flags,200,200,-7,11,0,0,0)
    record('all_recorded_grp_frames_native_bounds',dict(asset_sha256=row['sha256'],frame=frame,mirror=mirror),actual,want,ex,True)
  for frame in sorted({0,frames-1}):
   for flags in (0,2,4,6):
    for x,y,cx,cy in ((5,5,0,0),(630,395,0,0),(900,800,1000,900),(32767,32767,0,0),(0xFFFF,0x8000,-5,32767),(200,200,210,210)):
     image_fixture(row['image_id'],frame,flags,x,y,-128,127);w(IMAGE+0x1E,-55,2);w(0x543218,cx);w(0x54321C,cy);ex=call(0x40EE70)
     actual=list(struct.unpack('<8h',u.mem_read(IMAGE+0x1C,16)));want=bounds_model(b,frame,flags,x,y,-128,127,cx,cy,-55)
     record('grp_clip_mirror_hold_y_and_word_wrap',dict(asset_sha256=row['sha256'],frame=frame,flags=flags,position=[x,y],camera=[cx,cy],image_offsets=[-128,127],old_world_y=-55),actual,want,ex,True)
 # Ordinary LO uses GRP frame; shield LO uses direction and ignores mirror.
 for row in am['lo_files']:
  b=blobs[row['path']];frames,points=struct.unpack_from('<2I',b)
  if row['kind']==5:continue
  for frame in range(frames):
   off=struct.unpack_from('<I',b,8+frame*4)[0];assert off+2*points<=len(b)
   for point in range(points):
    dx,dy=struct.unpack_from('<2b',b,off+2*point)
    for mirror in (0,1):
     image_fixture(row['image_id'],frame,mirror*2);ex=call(0x40F5D0,args=(OUT,row['kind'],point));actual=list(struct.unpack('<2i',u.mem_read(OUT,8)))
     record('all_recorded_ordinary_lo_points_native_offset',dict(image_id=row['image_id'],kind=row['kind'],asset_sha256=row['sha256'],frame=frame,point=point,mirror=mirror),actual,[-dx if mirror else dx,dy],ex,True)
 shields={row['path']:row for row in am['lo_files'] if row['kind']==5}
 for path,row in shields.items():
  b=blobs[path];frames,points=struct.unpack_from('<2I',b)
  for direction in range(frames):
   off=struct.unpack_from('<I',b,8+4*direction)[0]
   for point in range(points):
    dx,dy=struct.unpack_from('<2b',b,off+2*point)
    for mirror in (0,1):
     image_fixture(row['image_id'],0x7777,mirror*2);w(IMAGE+0xB,direction,1);ex=call(0x40F6A0,args=(OUT,point));actual=list(struct.unpack('<2i',u.mem_read(OUT,8)))
     record('all_recorded_shield_lo_direction_points',dict(image_id=row['image_id'],asset_sha256=row['sha256'],direction=direction,ignored_grp_frame=0x7777,point=point,mirror=mirror),actual,[dx,dy],ex,True)
 # Raw field widths and update/flip semantics with stock direction/drawing columns.
 gfx=columns[1][3];methods=columns[5][3]
 for row in am['images']:
  ident=row['image_id']
  for entry,values in ((0x411580,range(256)),(0x40F1C0,list(range(33))+[256,0x123,MASK])):
   for value in values:
    flags=8 if gfx[ident] else 0;image_fixture(ident,0,flags);w(IMAGE+0xB,0xFE,1);w(IMAGE+0xA,methods[ident],1);ex=call(entry,args=(value,))
    q=((value&255)+4)>>3 if entry==0x411580 else value&255;flip=q>16;index=(32-q)&255 if flip else q
    actual=[r(IMAGE+0xB,1),r(IMAGE+0xC,2),r(IMAGE+0x1A,2),r(IMAGE+0x34),r(IMAGE+0x38),r(IMAGE+0x30,2)]
    method=methods[ident];draw=r(0x5030D4+method*12+(4 if flip else 0));update=r(0x503044+method*8)
    want=[index,flags|1|(2 if flip else 0),index,draw,update,0x230 if method==17 else 0] if flags else[0xFE,0,0,0,0,0]
    record('stock_direction_columns_complete_native_helpers',dict(image_id=ident,entry=hex(entry),input=value,stock_direction_enabled=bool(flags),drawing_method=method),actual,want,ex)
 # Pure native depth key includes the low WORD index, unlike the simplified OpenBW key.
 for elevation in range(256):
  for y in (-32768,-1,0,31,32,4095,32767):
   for flags in (0,0x10,0x30):
    for index in (0,106,0xFFFF):
     w(SPRITE+0xD,elevation,1);w(SPRITE+0x16,y,2);w(SPRITE+0xE,flags,1);w(SPRITE+0x10,index,2);ex=call(0x4160B0,SPRITE)
     key=(elevation<<13)|(y if elevation<=4 else 0);key=((key<<5)|(flags&0x10))&MASK;key=((key<<9)|index)&MASK
     record('sprite_native_packed_depth_key_all_elevation_bytes',dict(elevation=elevation,y=y,flags=flags,index=index),ex['return_eax'],key,ex)
 # The follow-main operation reads the main image, never the child's own frame.
 for row in am['lo_files']:
  if row['kind']!=2:continue
  b=blobs[row['path']];frames,points=struct.unpack_from('<2I',b)
  for frame in range(frames):
   off=struct.unpack_from('<I',b,8+4*frame)[0];dx,dy=struct.unpack_from('<2b',b,off)
   for mirror in (0,1):
    for same in (False,True):
     image_fixture(row['image_id'],frame,mirror*2);main=IMAGE+0x100;u.mem_write(main,bytes(u.mem_read(IMAGE,0x40)));w(SPRITE+0x18,main)
     want=[signed(-dx if mirror else dx,8),dy];w(IMAGE+0x1A,0x7777,2);w(IMAGE+0xC,0x80,2)
     w(IMAGE+0xE,want[0] if same else 99,1);w(IMAGE+0xF,want[1] if same else -99,1)
     ex=call(0x410380);actual=[signed(r(IMAGE+0xE,1),8),signed(r(IMAGE+0xF,1),8),r(IMAGE+0xC,2)]
     record('all_recorded_main_special_lo_follow_and_dirty',dict(image_id=row['image_id'],asset_sha256=row['sha256'],main_frame=frame,main_mirror=mirror,child_frame=0x7777,offset_already_equal=same),actual,want+[0x80 if same else 0x81],ex,True)
 row=next(v for v in am['lo_files'] if v['image_id']==234 and v['kind']==2);lo=blobs[row['path']]
 def first_lo(frame,mirror=False):
  at=struct.unpack_from('<I',lo,8+4*frame)[0];x,y=struct.unpack_from('<2b',lo,at);return [signed(-x if mirror else x,8),y]
 for existing in (0,0x80):
  for value in (0,1,2,255,256,257):
   for equal in (False,True):
    image_fixture(234,68,2);main=IMAGE+0x100;u.mem_write(main,bytes(u.mem_read(IMAGE,0x40)));w(SPRITE+0x18,main);offsets=first_lo(68,True)
    original=offsets if equal else [99,-99];w(IMAGE+0xC,existing,2);w(IMAGE+0xE,original[0],1);w(IMAGE+0xF,original[1],1);ex=call(0x40F3A0,args=(value,))
    requested=value&255;changed=(existing>>7)!=requested;want_flags=((existing&~0x80)|((requested&1)<<7)) if changed else existing
    want_offsets=offsets if changed and requested else original
    if changed and requested and original!=offsets:want_flags|=1
    record('follow_enable_low_byte_toggle_and_idempotence',dict(existing_flags=existing,request=value,offset_already_equal=equal,synthetic_raw_byte_argument=True),[r(IMAGE+0xC,2),signed(r(IMAGE+0xE,1),8),signed(r(IMAGE+0xF,1),8)],[want_flags]+want_offsets,ex)
 # A self-main fixture discriminates opposite sequencing of the two direction APIs.
 for entry,values in ((0x411580,(0,64,128,192,252)),(0x40F1C0,(0,8,16,24,32))):
  for value in values:
   image_fixture(234,68,0x88);w(IMAGE+0xB,0xFE,1);w(IMAGE+0xA,methods[234],1);ex=call(entry,args=(value,))
   q=((value&255)+4)>>3 if entry==0x411580 else value&255;flip=q>16;index=32-q if flip else q
   offsets=first_lo(68,False) if entry==0x411580 else first_lo(index,flip)
   record('follow_direction_update_order_self_main_fixture',dict(entry=hex(entry),argument=value,old_frame=68,synthetic_self_main=True),[r(IMAGE+0x1A,2),signed(r(IMAGE+0xE,1),8),signed(r(IMAGE+0xF,1),8)],[index]+offsets,ex)
 for entry,value,q in ((0x411580,192,24),(0x40F1C0,24,24)):
  for unchanged in (False,True):
   for base in (0,0xFFF8,0xFFFF):
    image_fixture(239,0x4321,8|(2 if unchanged else 0));w(IMAGE+0xB,8 if unchanged else 0xFE,1);w(IMAGE+0xA,methods[239],1);w(IMAGE+0x18,base,2);w(IMAGE+0x34,0x12345678);w(IMAGE+0x38,0x76543210)
    ex=call(entry,args=(value,));actual=[r(IMAGE+0xC,2),r(IMAGE+0x1A,2),r(IMAGE+0x34),r(IMAGE+0x38)]
    want=[10,0x4321,0x12345678,0x76543210] if unchanged else[11,(base+8)&0xFFFF,r(0x5030D8+methods[239]*12),r(0x503044+methods[239]*8)]
    record('direction_idempotent_fields_and_word_frame_wrap',dict(entry=hex(entry),argument=value,already_equal=unchanged,base_frame=base,old_frame=0x4321),actual,want,ex)
 def chain(nodes,head,tail):
  w(head,nodes[0] if nodes else 0);w(tail,nodes[-1] if nodes else 0)
  for i,node in enumerate(nodes):w(node,nodes[i-1] if i else 0);w(node+4,nodes[i+1] if i+1<len(nodes) else 0)
 def read_chain(head,tail):
  node=r(head);last=0;nodes=[]
  while node:
   assert len(nodes)<32 and r(node)==last;nodes.append(node);last=node;node=r(node+4)
  assert r(tail)==last;return nodes
 # Native row moves insert after a nonempty destination head and dirty every image.
 def tile_row(y,height):return max(0,min(height-1,int(signed(y,16)/32)))
 for height in (4,7):
  for old_y in (-32768,-33,-1,0,31,32,63,96,32767):
   for new_y in (-32768,-33,-1,0,31,32,63,96,32767):
    for position in (0,1,2):
     image_fixture(239,0,0,100,old_y);w(0x5264B6,height,2);u.mem_write(0x5C51D0,bytes(0x800));source_row=tile_row(old_y,height);target_row=tile_row(new_y,height)
     a1,b1,d1,e1=SPRITE+0x100,SPRITE+0x140,SPRITE+0x180,SPRITE+0x1C0
     original=[a1,b1];original.insert(position,SPRITE);chain(original,0x5C51D0+4*source_row,0x5C55D0+4*source_row)
     if target_row!=source_row:chain([d1,e1],0x5C51D0+4*target_row,0x5C55D0+4*target_row)
     image_nodes=[IMAGE,IMAGE+0x100,IMAGE+0x200];chain(image_nodes,SPRITE+0x1C,SPRITE+0x20)
     for i,node in enumerate(image_nodes):w(node+0xC,(0,2,0x80)[i],2)
     ex=call(0x417330,SPRITE,args=(101,new_y));want={source_row:[v for v in original if v!=SPRITE],target_row:[d1,SPRITE,e1]} if target_row!=source_row else {source_row:original}
     actual=dict(rows={str(row):read_chain(0x5C51D0+4*row,0x5C55D0+4*row) for row in range(height)},position=[r(SPRITE+0x14,2),r(SPRITE+0x16,2)],image_flags=[r(node+0xC,2) for node in image_nodes])
     expected=dict(rows={str(row):want.get(row,[]) for row in range(height)},position=[101,new_y&0xFFFF],image_flags=[1,3,0x81])
     record('sprite_rows_signed_division_clipping_and_after_head_order',dict(map_height_tiles=height,old_y=old_y,new_y=new_y,old_list_position=position,unchanged_x=False),actual,expected,ex)
 # Full allocation wrappers with real stock Init/underlay VM and three distinct layer policies.
 for entry,policy in ((0x4151F0,'before_main'),(0x415260,'before_head'),(0x4152D0,'after_main')):
  for populated in (False,True):
   for capacity in (0,1,2,4):
    image_fixture(239,68,8,200,200);w(SPRITE+0xE,0x20,1);w(0x543300,0);w(0x543304,0);w(0x673930,0);w(0x673938,0);w(0x67393C,1)
    old=[IMAGE+0x200,IMAGE,IMAGE+0x300] if populated else []
    chain(old,SPRITE+0x1C,SPRITE+0x20);w(SPRITE+0x18,IMAGE if populated else 0)
    for node in old:w(node+0x3C,SPRITE);w(node+0x2C,r(0x5436F8+239*4));w(node+8,239,2)
    pool=[POOL+0x40*i for i in range(capacity)];u.mem_write(POOL,bytes(0x1000));chain(pool,0x544694,0x592898)
    ex=call(entry,SPRITE,args=(239,-5,7,8));result_node=ex['return_eax'];nodes=read_chain(SPRITE+0x1C,SPRITE+0x20);ids=[r(node+8,2) for node in nodes]
    new=[POOL,POOL+0x40] if capacity>=2 else ([POOL] if capacity else [])
    at=old.index(IMAGE) if populated and policy=='before_main' else (old.index(IMAGE)+1 if populated and policy=='after_main' else 0)
    expected_nodes=old[:at]+new+old[at:];expected_ids=[240 if node==POOL+0x40 else 239 for node in expected_nodes]
    actual=dict(return_node=result_node,main=r(SPRITE+0x18),nodes=nodes,ids=ids,free_pool=read_chain(0x544694,0x592898))
    expected=dict(return_node=POOL if capacity else 0,main=IMAGE if populated else(POOL if capacity else 0),nodes=expected_nodes,ids=expected_ids,free_pool=pool[len(new):])
    record('stock_init_underlay_and_original_layer_allocation_policies',dict(entry=hex(entry),policy=policy,existing_image_list=populated,private_free_capacity=capacity,stock_image_id=239,original_rng_enabled=False),actual,expected,ex)
 # Inclusive first-match search follows the established image list, not numeric ID order.
 for ids in ([],[300],[400,200,300],[200,400,200]):
  for low,high in ((0,65535),(200,200),(200,300),(301,399),(400,200)):
   image_fixture();nodes=[IMAGE+0x100*i for i in range(len(ids))];chain(nodes,SPRITE+0x1C,SPRITE+0x20)
   for node,ident in zip(nodes,ids):w(node+8,ident,2)
   ex=call(0x416940,SPRITE,args=(low,high));want=next((node for node,ident in zip(nodes,ids) if low<=ident<=high),0)
   record('image_id_range_search_inclusive_first_list_match',dict(ids=ids,low=low,high=high),ex['return_eax'],want,ex)
 result=dict(target_sha256=source.HASH,target_version='1.17.0.1',original_code_executed=True,complete_game_executed=False,probe_script_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),source_dependencies_sha256=deps,asset_manifest_sha256=hashlib.sha256(a.asset_manifest.read_bytes()).hexdigest(),retail_manifest_sha256=hashlib.sha256(a.manifest.read_bytes()).hexdigest(),images_dat_sha256=hashlib.sha256(images_dat).hexdigest(),iscript_sha256=hashlib.sha256(script).hexdigest(),assets_sha256=provenance,runtime=dict(architecture=platform.machine(),python=sys.version,unicorn=unicorn.__version__,capstone=capstone.__version__,pefile=pefile.__version__),helper_stubs=[],imports_executed=[],instruction_bound=100000,cases_passed=len(cases),complete_original_function_cases=len(cases),prefix_cases=0,executed_instruction_witnesses=len(visited),cases=cases,limits=['Prepared asset corpus, not all retail image resources','Private image/sprite/field fixtures; no played campaign or renderer','Resource loader/import wrappers are static only','Stock-derived exhaustive outputs are compared and hashed; retail coordinate/pixel payloads stay in scratch'])
 a.out.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(dict(cases_passed=len(cases),families=dict(Counter(v['case'] for v in cases)),pcs=len(visited),script_sha256=result['probe_script_sha256'])))
if __name__=='__main__':main()
