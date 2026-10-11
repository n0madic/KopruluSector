#!/usr/bin/env python3
"""Read exact-hash populated natural Hallucination witnesses as data."""
import argparse,hashlib,json,struct
from pathlib import Path
import pefile,capstone
TARGET_HASH='94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0'
BLOCKS={'hallucination_order':(0x430BD0,0x430C68),'hallucination_creator':(0x430ED0,0x430F5E),'hallucination_initializer':(0x430960,0x4309FE),'hallucination_placement_show':(0x430FA0,0x431028),'hallucination_status_death':(0x430C70,0x430CDA),'sprite_image_direction':(0x415F00,0x415F25),'owner_hallucination_draw_function':(0x4171C0,0x4171F7)}
STATIC_ONLY={'network_tbl_binding':(0x43F0D0,0x43F0F6),'primary_order148_wrapper':(0x43972D,0x439735)}
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--exe',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
 raw=a.exe.read_bytes();assert hashlib.sha256(raw).hexdigest()==TARGET_HASH;pe=pefile.PE(data=raw)
 assert pe.OPTIONAL_HEADER.ImageBase==0x400000 and pe.FILE_HEADER.Machine==0x14c
 md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);witnesses={}
 for name,(lo,hi)in dict(BLOCKS,**STATIC_ONLY).items():
  code=pe.get_data(lo-0x400000,hi-lo);ins=list(md.disasm(code,lo));assert sum(i.size for i in ins)==len(code)
  witnesses[name]={'start_preferred_va':hex(lo),'end_exclusive_preferred_va':hex(hi),'bytes_sha256':hashlib.sha256(code).hexdigest(),'instructions':[{'address':hex(i.address),'bytes':i.bytes.hex(),'mnemonic':i.mnemonic,'operands':i.op_str}for i in ins]}
 mapbyte=pe.get_data(0x439D5C+148-1-0x400000,1)[0];wrapper=struct.unpack('<I',pe.get_data(0x439BCC+mapbyte*4-0x400000,4))[0]
 path=pe.get_data(0x4EC2D0-0x400000,32).split(b'\0',1)[0].decode('ascii');assert path=='rez\\network.tbl' and wrapper==0x43972D
 r={'target_sha256':TARGET_HASH,'target_version':'1.17.0.1','inspector_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),'original_code_executed':False,'static_only_witnesses':list(STATIC_ONLY),'primary_order148_dispatch':{'stage':3,'order_minus':1,'byte_mapping_va':'0x439d5c','mapped_byte':mapbyte,'destination_table_va':'0x439bcc','wrapper':hex(wrapper),'handler':'0x430bd0'},'network_tbl_binding':{'path':path,'path_va':'0x4ec2d0','pointer_global':'0x665de0','writer':'0x43f0f0','loader':'0x4c13e0','native_loader_executed':False},'witnesses':witnesses}
 a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_text(json.dumps(r,indent=2)+'\n');print(json.dumps({'witnesses':len(witnesses),'instructions':sum(len(w['instructions'])for w in witnesses.values())}))
if __name__=='__main__':main()
