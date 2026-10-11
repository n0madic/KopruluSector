#!/usr/bin/env python3
"""Run original indexed GRP blitters against private guarded bitmaps and stock lookup bytes.
No guest loader, Windows API or full renderer executes. Resource pixels stay in scratch;
measured buffers are compared with independent RLE predictions and stored as hashes.
"""
import argparse,hashlib,json,platform,struct,sys
from collections import Counter
from pathlib import Path
import capstone,pefile
import inspect_native_pixel_blitters_117 as source
PRIVATE,PIXELS,ASSETS,STACK,STOP=0x800000,0x810000,0x900000,0x1200000,0x1300000
FRAME,RECT,BITMAP,SYNTH=PRIVATE,PRIVATE+0x40,PRIVATE+0x80,PRIVATE+0x1000
WIDTH,HEIGHT=320,320

def sha(b):return hashlib.sha256(b).hexdigest()
def pcx(raw):
 assert raw[0]==10 and raw[2:4]==bytes([1,8]) and raw[65]==1
 x0,y0,x1,y1=struct.unpack_from('<4H',raw,4);width,height=x1-x0+1,y1-y0+1;stride=struct.unpack_from('<H',raw,66)[0]
 assert stride>=width and width>0 and height>0
 at=128;out=bytearray()
 while len(out)<stride*height:
  v=raw[at];at+=1
  if v&0xC0==0xC0:n=v&63;v=raw[at];at+=1;assert n;out.extend([v]*n)
  else:out.append(v)
 assert len(out)==stride*height
 return width,height,b''.join(out[y*stride:y*stride+width] for y in range(height))

def decode_frame(raw,frame):
 fx,fy,width,height,offset=struct.unpack_from('<4BI',raw,6+8*frame);rows=[];packets=Counter()
 for y in range(height):
  at=offset+struct.unpack_from('<H',raw,offset+2*y)[0];row=[]
  while len(row)<width:
   v=raw[at];at+=1
   if v&128:n=v&127;values=[None]*n;packets['transparent']+=1
   elif v&64:n=v&63;values=[raw[at]]*n;at+=1;packets['repeat']+=1
   else:n=v;values=list(raw[at:at+n]);at+=n;packets['literal']+=1
   assert n and at<=len(raw);row.extend(values)
  assert len(row)==width;rows.append(row)
 return (fx,fy,width,height,offset),rows,packets

def main():
 p=argparse.ArgumentParser(description=__doc__)
 for k in ('exe','assets','asset-manifest','pixel-assets','pixel-manifest'):p.add_argument('--'+k,type=Path,required=True)
 p.add_argument('--out',type=Path);p.add_argument('--execute-original-code',action='store_true');a=p.parse_args()
 raw=a.exe.read_bytes();assert sha(raw)==source.HASH;source_hash=sha(Path(source.__file__).read_bytes())
 if not a.execute_original_code:print(json.dumps(dict(original_code_executed=False,reviewed_blocks=len(source.BLOCKS),target_sha256=source.HASH)));return
 assert a.out and platform.machine().lower() in ('x86_64','amd64')
 am=json.loads(a.asset_manifest.read_text());pm=json.loads(a.pixel_manifest.read_text());assert pm['retail_manifest_sha256']==am['retail_manifest_sha256']
 grps={};metadata={};provenance={}
 for row in am['images']:
  path=row['path'];b=(a.assets/path.replace('\\','/')).read_bytes();assert sha(b)==row['sha256'];grps[path]=b;metadata[path]=row;provenance[path]=row['sha256']
 palettes={};palette_shapes={}
 for path,row in pm['assets'].items():
  b=(a.pixel_assets/path.replace('\\','/')).read_bytes();assert sha(b)==row['sha256'];provenance[path]=row['sha256']
  if path.endswith('.pcx'):
   width,height,values=pcx(b);palettes[path]=values;palette_shapes[path]=[width,height]
  else:assert path.endswith('.wpe') and len(b)==1024
 import unicorn
 from unicorn import Uc,UC_ARCH_X86,UC_MODE_32,UC_HOOK_CODE
 from unicorn.x86_const import UC_X86_REG_ECX,UC_X86_REG_EDX,UC_X86_REG_EAX,UC_X86_REG_EIP,UC_X86_REG_ESP
 pe=pefile.PE(data=raw);assert pe.OPTIONAL_HEADER.ImageBase==0x400000 and pe.FILE_HEADER.Machine==0x14C
 u=Uc(UC_ARCH_X86,UC_MODE_32);u.mem_map(0x400000,(pe.OPTIONAL_HEADER.SizeOfImage+4095)&~4095);u.mem_write(0x400000,pe.get_memory_mapped_image())
 for address,size in ((PRIVATE,0x10000),(PIXELS,0x40000),(ASSETS,0x800000),(STACK,0x10000),(STOP,4096)):u.mem_map(address,size)
 def w(addr,val,size=4):u.mem_write(addr,(val&((1<<(size*8))-1)).to_bytes(size,'little'))
 def r(addr,size=4):return int.from_bytes(u.mem_read(addr,size),'little')
 pointers={};at=ASSETS
 for path,b in {**grps,**palettes}.items():
  pointers[path]=at;u.mem_write(at,b);at+=(len(b)+4095)&~4095
 assert at<ASSETS+0x800000
 # Native allocation reserves a leading 256-byte row, then decodes PCX after it.
 # Its contents are not initialized by the reviewed native wrapper. The fixture
 # supplies 0x5a explicitly and never labels row zero as a stock decoded row.
 banks={};bank_pointers={}
 for path,b in palettes.items():
  if path.startswith('tileset\\badlands\\') and path.split('\\')[-1] in ['ofire.pcx','gfire.pcx','bfire.pcx','bexpl.pcx','trans50.pcx','red.pcx','green.pcx']:
   banks[path]=bytes([0x5A]*256)+b;bank_pointers[path]=at;u.mem_write(at,banks[path]);at+=(len(banks[path])+4095)&~4095
 assert at<ASSETS+0x800000
 player=palettes['game\\tunit.pcx'];selection=palettes['game\\tselect.pcx'];shift=palettes['tileset\\badlands\\shift.pcx'];dark=palettes['tileset\\badlands\\dark.pcx']
 assert len(player)==128 and len(selection)==24 and len(shift)==256 and len(dark)==8192
 u.mem_write(0x529056,player[:96]);u.mem_write(0x5290B6,palettes['game\\tminimap.pcx'][:12]);u.mem_write(0x594054,selection);u.mem_write(0x59C1B0,shift)
 shadow=dark[18*256:19*256];u.mem_write(0x66EF20,shadow);w(0x508840,bank_pointers['tileset\\badlands\\ofire.pcx'])
 w(0x6D0C40,BITMAP);w(BITMAP,WIDTH,2);w(BITMAP+2,HEIGHT,2);w(BITMAP+4,PIXELS)
 visited=set();approved=set();ranges=[v for k,v in source.BLOCKS.items() if k not in source.STATIC_ONLY];entries={v[0] for v in ranges};cases=[]
 def call(entry,x=0,y=0,args=()):
  done=False;steps=0;calls=[]
  def hook(machine,pc,size,unused):
   nonlocal done,steps
   if pc==STOP:done=True;machine.emu_stop();return
   if (pc,size) not in approved:
    assert any(lo<=pc and pc+size<=hi for lo,hi in ranges),('Outside reviewed function',hex(pc),calls[-8:]);approved.add((pc,size))
   visited.add(pc);steps+=1
   if pc in entries:calls.append(hex(pc))
  h=u.hook_add(UC_HOOK_CODE,hook);sp=STACK+0xF000;u.mem_write(sp,struct.pack('<'+'I'*(len(args)+1),STOP,*(v&0xFFFFFFFF for v in args)));u.reg_write(UC_X86_REG_ESP,sp);u.reg_write(UC_X86_REG_ECX,x);u.reg_write(UC_X86_REG_EDX,y);u.reg_write(UC_X86_REG_EAX,0)
  try:u.emu_start(entry,STOP+1,count=1000000)
  except Exception as e:raise RuntimeError((hex(entry),hex(u.reg_read(UC_X86_REG_EIP)),calls[-10:])) from e
  finally:u.hook_del(h)
  assert done and u.reg_read(UC_X86_REG_ESP)==sp+4*(len(args)+1)
  return dict(complete_return=True,original_instructions=steps,calls=calls,return_eax=u.reg_read(UC_X86_REG_EAX))
 def record(name,inputs,actual,expected,ex):
  assert actual==expected,(name,inputs,actual,expected)
  cases.append(dict(case=name,inputs=inputs,actual=actual,expected=expected,passed=True,execution=ex))
 baseline=bytes(i&255 for i in range(WIDTH*HEIGHT));guard=bytes([0xE7]*16);mapping=bytes(range(256))
 def render(name,inputs,entry,header,rows,crop,mirror=False,mode='normal',lookup=None,arg=0,x=7,y=9,expected_mapping=None):
  fw,fh=header[2:4];left,top,cw,ch=crop;assert 0<=left<=fw and 0<=top<=fh and left+cw<=fw and top+ch<=fh and cw>=1
  assert x+cw<=WIDTH and y+ch<=HEIGHT
  u.mem_write(RECT,struct.pack('<4I',left,top,cw,ch));u.mem_write(FRAME,struct.pack('<4BI',*header[:4],header[4]));u.mem_write(0x501021,mapping);w(0x671A88,0,1)
  u.mem_write(PIXELS-16,guard+baseline+guard);before_color=r(0x5D6E38)
  ex=call(entry,x,y,args=(FRAME,RECT,arg));out=bytes(u.mem_read(PIXELS-16,len(baseline)+32));want=bytearray(guard+baseline+guard)
  for yy in range(ch):
   for xx in range(cw):
    sx=fw-1-left-xx if mirror else left+xx;value=rows[top+yy][sx]
    if value is None:continue
    dest=16+(y+yy)*WIDTH+x+xx;background=want[dest]
    want[dest]=lookup[value*256+background] if mode=='blend' else lookup[background] if mode in ('shadow','background') else lookup[value] if lookup is not None else mapping[value]
  after_map=bytes(u.mem_read(0x501021,256));expected_map=mapping if expected_mapping is None else expected_mapping
  actual=dict(indexed_bitmap_sha256=sha(out),global_mapping_sha256=sha(after_map),current_color=r(0x5D6E38));expected=dict(indexed_bitmap_sha256=sha(want),global_mapping_sha256=sha(expected_map),current_color=before_color)
  record(name,inputs,actual,expected,ex)
 # Every recorded GRP frame, both normal/mirrored, with the full background/guard checked.
 packet_totals=Counter();opaque_zero=0;decoded_pixels=0
 for path,b in sorted(grps.items()):
  frames=struct.unpack_from('<H',b)[0]
  for frame in range(frames):
   header,rows,packets=decode_frame(b,frame);packet_totals.update(packets);opaque_zero+=sum(value==0 for row in rows for value in row);decoded_pixels+=sum(value is not None for row in rows for value in row)
   h=header[:4]+(pointers[path]+header[4],);fw,fh=header[2:4]
   for mirror in (False,True):
    render('all_recorded_grp_frames_complete_native_indexed_blit',dict(asset_sha256=provenance[path],frame=frame,mirror=mirror),0x4071C0 if mirror else 0x405E1E,h,rows,(0,0,fw,fh),mirror)
   if frame in {0,frames-1} and fw>1 and fh>1:
    for mirror in (False,True):
     for crop in ((1,1,fw-1,fh-1),(fw//2,fh//2,1,1),(0,0,fw,0)):
      render('stock_grp_crop_and_zero_height_private_buffer',dict(asset_sha256=provenance[path],frame=frame,mirror=mirror,crop=list(crop)),0x4071C0 if mirror else 0x405E1E,h,rows,crop,mirror)
 def synthetic(rows):
  width=len(rows[0]);height=len(rows);assert 0<width<=255 and all(len(row)==width for row in rows)
  lines=[]
  for row in rows:
   encoded=bytearray();i=0
   while i<width:
    if row[i] is None:
     n=1
     while i+n<width and row[i+n] is None and n<127:n+=1
     encoded.append(128|n);i+=n
    elif i+1<width and row[i+1]==row[i]:
     n=2
     while i+n<width and row[i+n]==row[i] and n<63:n+=1
     encoded.extend([64|n,row[i]]);i+=n
    else:
     vals=[row[i]];i+=1
     while i<width and row[i] is not None and len(vals)<63 and not(i+1<width and row[i+1]==row[i]):vals.append(row[i]);i+=1
     encoded.append(len(vals));encoded.extend(vals)
   lines.append(encoded)
  offsets=[];at=2*height
  for line in lines:offsets.append(at);at+=len(line)
  data=struct.pack('<'+'H'*height,*offsets)+b''.join(lines);assert len(data)<0xE000;u.mem_write(SYNTH,data)
  return (0,0,width,height,SYNTH)
 # Literal/repeat color zero is an opaque index; transparency is a packet property.
 rows=[[None]*3+[0]*4+list(range(64))+[None]*5+[8]*63+[255]*63+[None]*6]
 h=synthetic(rows)
 for mirror in (False,True):
  for left,width in ((0,len(rows[0])),(1,2),(3,3),(6,7),(7,62),(70,4),(74,62),(136,62),(198,5)):
   render('authored_rle_packet_boundaries_and_opaque_zero',dict(mirror=mirror,left=left,width=width,synthetic_resource=True),0x4071C0 if mirror else 0x405E1E,h,rows,(left,0,width,1),mirror)
 # Stock player lookup consumes only the first 96 decoded tunit bytes (12 owners).
 for value in range(12):
  for high in (0,256):
   u.mem_write(0x501021,mapping);w(0x671A88,0,1);ex=call(0x41D740,value+high)
   want=bytearray(mapping);want[8:16]=player[value*8:(value+1)*8]
   record('stock_player_color_setter_low_byte_and_global_indices',dict(argument=value+high,color_selector_value=0,stock_tunit_rows=12),dict(mapping_sha256=sha(bytes(u.mem_read(0x501021,256))),color=r(0x5D6E38)),dict(mapping_sha256=sha(want),color=value),ex)
 # Mode 2 chooses self/ally/enemy palette classes, then first matching color assignment.
 for viewer in range(12):
  for owner in range(12):
   for allied in (0,1):
    for assigned in (list(range(8)),list(reversed(range(8))),[1]*8):
     for i,value in enumerate(assigned):w(0x5264FC+4*i,value)
     w(0x508D50,viewer);w(0x671A88,2,1);u.mem_write(0x534914,bytes(144));w(0x534914+viewer*12+owner,allied,1);u.mem_write(0x501021,mapping)
     ex=call(0x41D740,owner);desired=2 if owner==viewer else 7 if allied else 0;selected=next((i for i,v in enumerate(assigned) if v==desired),desired);want=bytearray(mapping);want[8:16]=player[selected*8:(selected+1)*8]
     record('mode2_player_color_relationship_and_first_color_assignment',dict(viewer=viewer,owner=owner,alliance_byte=allied,assigned_palette_ids=assigned),dict(mapping_sha256=sha(bytes(u.mem_read(0x501021,256))),color=r(0x5D6E38)),dict(mapping_sha256=sha(want),color=selected),ex)
 # Full player override wrappers select, draw, then restore old mode-0 color state.
 rows=[list(range(1,33))];h=synthetic(rows)
 for old in (0,5,11):
  for owner in range(12):
   for mirror in (False,True):
    mapping=bytes(range(8))+player[old*8:(old+1)*8]+bytes(range(16,256));w(0x5D6E38,old);lookup=bytearray(mapping);lookup[8:16]=player[owner*8:(owner+1)*8]
    render('stock_player_override_complete_draw_and_restore',dict(old_color=old,new_owner=owner,mirror=mirror),0x40E990 if mirror else 0x40E950,h,rows,(0,0,32,1),mirror,lookup=lookup,arg=owner)
 mapping=bytes(range(256));w(0x5D6E38,0)
 for color in range(3):
  lookup=bytearray(mapping);lookup[1:9]=selection[color*8:(color+1)*8]
  render('stock_selection_color_override_and_restore',dict(selection_class=color,normal_dispatch_for_both_mirror_slots=True),0x40E900,h,rows,(0,0,32,1),lookup=lookup,arg=color)
 for mirror in (False,True):render('stock_shift_lookup_entire_global_map_and_restore',dict(mirror=mirror,stock_shift_bank=0),0x40EAC0 if mirror else 0x40EA60,h,rows,(0,0,32,1),mirror,lookup=shift,arg=0)
 # Background remaps and masks preserve transparent packets and ignore opaque source color.
 for foreground in (0,18,255):
  rows=[[None]*3+[foreground]*63+[None]*5+list(range(64))];h=synthetic(rows)
  for mirror in (False,True):
   render('stock_dark_row18_shadow_background_lookup',dict(foreground=foreground,mirror=mirror,lookup_row_is_explicit_fixture=True),0x40730E if mirror else 0x406950,h,rows,(0,0,len(rows[0]),1),mirror,'shadow',shadow)
   for bank in (1,32,63):
    lookup=banks['tileset\\badlands\\ofire.pcx'][bank*256:(bank+1)*256]
    render('stock_fire_wrapper_low_byte_row_and_mask',dict(foreground=foreground,mirror=mirror,bank=bank,argument=bank+256),0x40EB40 if mirror else 0x40EB20,h,rows,(0,0,len(rows[0]),1),mirror,'background',lookup,arg=bank+256)
 # Every stock blend row is tested against every possible destination index. Native
 # matrix address is foreground*256+background. Row zero is explicit private padding.
 for path,bank in banks.items():
  for foreground in range(len(bank)//256):
   for mirror in (False,True):
    for width,x in ((255,0),(1,255)):
     rows=[[foreground]*width];h=synthetic(rows)
     render('all_recorded_stock_blend_rows_all_background_indices',dict(asset_sha256=provenance[path],foreground_index=foreground,mirror=mirror,background_range=[0,254] if width==255 else[255,255],leading_row_private_fixture=foreground==0),0x406F03 if mirror else 0x4067F6,h,rows,(0,0,width,1),mirror,'blend',bank,arg=bank_pointers[path],x=x,y=0)
 result=dict(target_sha256=source.HASH,target_version='1.17.0.1',original_code_executed=True,complete_game_executed=False,probe_script_sha256=sha(Path(__file__).read_bytes()),source_dependencies_sha256={Path(source.__file__).name:source_hash},asset_manifest_sha256=sha(a.asset_manifest.read_bytes()),pixel_manifest_sha256=sha(a.pixel_manifest.read_bytes()),assets_sha256=provenance,palette_decoded_shapes=palette_shapes,retail_rle_packet_counts=dict(packet_totals),retail_opaque_zero_count=opaque_zero,retail_nontransparent_pixel_count=decoded_pixels,runtime=dict(architecture=platform.machine(),python=sys.version,unicorn=unicorn.__version__,capstone=capstone.__version__,pefile=pefile.__version__),helper_stubs=[],imports_executed=[],instruction_bound=1000000,cases_passed=len(cases),complete_original_function_cases=len(cases),prefix_cases=0,executed_instruction_witnesses=len(visited),cases=cases,limits=['Private guarded indexed bitmap; no complete renderer, RGB display or gameplay execution','Only recorded GRP and badlands lookup resource corpus','Native loader/pointer relocation/PCX decode wrappers are static only','Native palette allocation reserves but does not itself initialize leading row; row zero is supplied fixture','Shadow row18 selection is supplied fixture, not proof of native bootstrap selection','GRP/lookup pixels remain private; measured whole-buffer/state hashes are evidence'])
 a.out.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(dict(cases_passed=len(cases),families=dict(Counter(v['case'] for v in cases)),pcs=len(visited),opaque_zero=opaque_zero,script_sha256=result['probe_script_sha256'])))
if __name__=='__main__':main()
