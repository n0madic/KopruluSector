#!/usr/bin/env python3
"""Inspect original portrait/audio/dialog consumers and private resource contracts."""
import argparse,hashlib,json,struct
from pathlib import Path
import capstone,pefile
HASH='94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0'
BLOCKS={
 'portrait_resolution':(0x4A65D0,0x4A660B),'portrait_rng':(0x4A6E00,0x4A6E5C),
 'portrait_idle':(0x4A6B40,0x4A6C41),'portrait_talking':(0x4A6C50,0x4A6D2B),
 'portrait_decode_gate':(0x4A6700,0x4A675E),'portrait_mode':(0x4A6E90,0x4A6F0D),
 'portrait_timer_callback':(0x4A7030,0x4A7188),'portrait_talking_request':(0x4A7200,0x4A72F1),
 'portrait_refresh':(0x4A7420,0x4A74B1),'dialog_dirty':(0x4C2840,0x4C2853),
 'dialog_hide':(0x4C22F0,0x4C2381),'dialog_hit':(0x4C29A0,0x4C29CC),
 'dialog_relative_point':(0x4C29D0,0x4C29FF),'rectangle_hit':(0x4C2A00,0x4C2A27),
 'event_cancel':(0x4C58F0,0x4C595B),'selection_sound':(0x447190,0x447216),
 'yes_sound':(0x447130,0x44718B),'wav_chunk':(0x49D7C0,0x49D809),
 'wav_parse':(0x49D830,0x49D8FA),'audio_gate':(0x49D810,0x49D82B),
 'sound_duration':(0x49DB30,0x49DB3A),'sound_whole_seconds':(0x446740,0x446758),
 'duration_arithmetic_fragment':(0x49DAA0,0x49DAAB),
 'byte_table_lookup':(0x4C13B0,0x4C13D7),'error_table_getter':(0x43F0C0,0x43F0CD),
 'status_text_getter':(0x4A7D70,0x4A7D7D),'error_table_bootstrap_static':(0x43F0D0,0x43F0F6),
 'status_text_bootstrap_static':(0x4A7EB0,0x4A7ECF),
 'channel_select':(0x49DB60,0x49DD69),'sound_play_gate':(0x49E4E0,0x49E740),
 'selection_wrapper':(0x446C30,0x446E81),'neutral_resource_sound_type':(0x4243E0,0x424415),
 'critter_sound_type':(0x429070,0x42909B),'selection_initialize':(0x446800,0x44692D),
 'sound_preload_list':(0x49E3F0,0x49E460),'sound_duration_producer_static':(0x49DA70,0x49DAB6),
 'portrait_bootstrap_static':(0x4A6A40,0x4A6B3F),'portrait_request_static':(0x4A6F10,0x4A702C),
 'portrait_trigger_static':(0x4A7320,0x4A73DA),'sound_queue_static':(0x49E000,0x49E08B),
 'ui_event_create_static':(0x4C5960,0x4C5A19),'dialog_dirty_platform_static':(0x4C4AE0,0x4C4BBB),
}
DESCRIPTORS={'units':(0x5014F0,54),'portdata':(0x501368,6),'sfxdata':(0x5013C0,5)}
def sha(b):return hashlib.sha256(b).hexdigest()
def tbl(b):
 n=struct.unpack_from('<H',b)[0];offs=struct.unpack_from('<'+'H'*n,b,2)
 return [b[x:b.index(0,x)] for x in offs]
def rows(b,widths,count):
 result=[];off=0
 for width in widths:
  result.append(list(struct.unpack_from('<'+{1:'B',2:'H',4:'I'}[width]*count,b,off)));off+=width*count
 assert off==len(b);return result
def bins(b):
 result=[];seen=set();at=0
 while True:
  assert at not in seen and at+86<=len(b);seen.add(at)
  values=struct.unpack_from('<L6H4LH5L4HLL4HLL',b,at)
  nxt,x1,y1,x2,y2,width,height,unknown,string,flags,unknown2,ident,kind=values[:13]
  assert kind<=14
  text=b[string:b.index(0,string)] if string else b''
  result.append(dict(offset=at,type=kind,identifier=ident,raw_bounds=[x1,y1,x2,y2],width=width,height=height,flags=flags,string_bytes=len(text),string_sha256=sha(text),control_bytes={str(v):text.count(v) for v in range(32) if v in text},responsive=list(values[17:21])))
  at=values[22] if kind==0 else nxt
  if not at:break
 return result
def smk(b):
 assert b[:4] in (b'SMK2',b'SMK4') and len(b)>=104
 width,height,count,interval,flags=struct.unpack_from('<IIIiI',b,4);actual_count=count+bool(flags&1);tree=struct.unpack_from('<I',b,52)[0]
 sizes=struct.unpack_from('<'+'I'*actual_count,b,104);frame_flags=b[104+4*actual_count:104+5*actual_count];start=104+5*actual_count+tree
 assert len(frame_flags)==actual_count and start+sum(x&~3 for x in sizes)<=len(b)
 return dict(signature=b[:4].decode(),width=width,height=height,header_frames=count,ring_frame=bool(flags&1),table_frames=actual_count,interval=interval,timebase_denominator=100000,timebase_numerator=abs(interval) if interval<0 else interval*100,tree_bytes=tree,frame_data_offset=start,frame_payload_bytes=sum(x&~3 for x in sizes),trailing_bytes=len(b)-start-sum(x&~3 for x in sizes),audio_rates=[struct.unpack_from('<I',b,72+4*i)[0]&0xffffff for i in range(7)],frame_size_sha256=sha(b[104:104+4*actual_count]),frame_flags_sha256=sha(frame_flags))
def wav(b):
 assert b[:4]==b'RIFF' and b[8:12]==b'WAVE';at=12;chunks=[]
 while at+8<=len(b):
  tag=b[at:at+4];n=struct.unpack_from('<I',b,at+4)[0];assert at+8+n<=len(b);chunks.append(dict(tag=tag.decode('latin1'),offset=at,payload_offset=at+8,bytes=n));at+=8+n
 # Native chunk walker uses exact sizes without an odd-byte alignment increment.
 fmt=next(r for r in chunks if r['tag']=='fmt ');data=next(r for r in chunks if r['tag']=='data');assert fmt['bytes']>=16
 fields=list(struct.unpack_from('<HHIIHH',b,fmt['payload_offset']))
 return dict(chunks=chunks,format=fields,data_offset=data['payload_offset'],data_bytes=data['bytes'],remaining_bytes=len(b)-at,pcm_duration_ms=data['bytes']*1000//fields[3] if fields[3] else None,native_duration_if_same_device_format_ms=((data['bytes']*1000)&0xFFFFFFFF)//fields[3] if fields[3] else None)
def main():
 p=argparse.ArgumentParser(description=__doc__)
 for k in ('exe','assets','asset-manifest','out'):p.add_argument('--'+k,type=Path,required=True)
 a=p.parse_args();raw=a.exe.read_bytes();assert sha(raw)==HASH;pe=pefile.PE(data=raw);md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);blocks={}
 for key,(lo,hi) in BLOCKS.items():
  blob=pe.get_data(lo-0x400000,hi-lo);ins=list(md.disasm(blob,lo));assert sum(i.size for i in ins)==len(blob),key
  blocks[key]=[dict(va=hex(i.address),rva=hex(i.address-0x400000),file_offset=hex(pe.get_offset_from_rva(i.address-0x400000)),bytes=i.bytes.hex(),mnemonic=i.mnemonic,operands=i.op_str) for i in ins]
 descriptors={key:[list(struct.unpack('<3I',pe.get_data(base-0x400000+i*12,12))) for i in range(count)] for key,(base,count) in DESCRIPTORS.items()}
 am=json.loads(a.asset_manifest.read_text());resources={}
 for path,row in am['assets'].items():
  b=(a.assets/path.replace('\\','/')).read_bytes();assert sha(b)==row['sha256'];extra={}
  if path.lower().endswith('.wav'):extra['wav']=wav(b)
  elif path.lower().endswith('.smk'):extra['smk']=smk(b)
  elif path.lower().endswith('.bin'):extra['bin_widgets']=bins(b)
  elif path.lower().endswith('.tbl'):extra['strings']=[dict(index=i,bytes=len(t),sha256=sha(t)) for i,t in enumerate(tbl(b),1)]
  resources[path]=dict(provenance=row,**extra)
 ports=rows((a.assets/'arr/portdata.dat').read_bytes(),[4,4,1,1,1,1],110);sfx=rows((a.assets/'arr/sfxdata.dat').read_bytes(),[4,1,1,2,1],1144)
 result=dict(target_sha256=HASH,original_code_executed=False,extractor_sha256=sha(Path(__file__).read_bytes()),asset_manifest_sha256=sha(a.asset_manifest.read_bytes()),blocks=blocks,dat_descriptors=descriptors,portrait_rng_mapped_pe_initial_words=list(struct.unpack('<3I',pe.get_data(0x50D7CC-0x400000,12))),portrait_rows=[dict(id=i,idle_string=ports[0][i],talking_string=ports[1][i],idle_weights=[r[i] for r in ports[2:]]) for i in range(110)],sfx_rows=[dict(id=i,file=sfx[0][i],priority=sfx[1][i],flags=sfx[2][i],portrait_adjustment=(sfx[3][i]+32768)%65536-32768,minimum_volume=sfx[4][i]) for i in range(1144)],resources=resources,imports=[dict(va=hex(imp.address),library=d.dll.decode(),name=imp.name.decode() if imp.name else '#'+str(imp.ordinal)) for d in pe.DIRECTORY_ENTRY_IMPORT for imp in d.imports],limits=['Resource decoding is host structural inspection, not original playback','Raw BIN bounds are file inputs, not proof of native loader relocation/normalization','SMK compressed pixels and audio are not decoded by the original EXE in this inspector'])
 a.out.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(dict(blocks=len(blocks),instructions=sum(map(len,blocks.values())),resources=len(resources),sha256=result['extractor_sha256'])))
if __name__=='__main__':main()
