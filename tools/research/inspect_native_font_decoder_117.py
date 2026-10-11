#!/usr/bin/env python3
"""Inspect original encrypted-font format and deterministic crypto leaves."""
import argparse,hashlib,json
from pathlib import Path
import capstone,pefile
HASH='94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0'
BLOCKS={
 'idea_block_transform':(0x464FB0,0x46518F),'idea_inverse_schedule':(0x465190,0x4652BC),
 'idea_expand_schedule':(0x4652C0,0x46533B),'idea_context_clear':(0x465340,0x465355),
 'idea_context_transform':(0x465360,0x46537E),'idea_context_setup':(0x465380,0x4653F1),
 'idea_multiplicative_inverse':(0x465400,0x465467),
 'hash_context_clear':(0x49CFF0,0x49D005),'hash_snapshot':(0x49D010,0x49D044),
 'hash_update64_wrapper':(0x49D050,0x49D0A1),'hash_context_init':(0x49D0B0,0x49D0E9),
 'hash_core':(0x49D0F0,0x49D361),'native_memset':(0x4D0C60,0x4D0CF0),
 'font_decode_static':(0x4119F0,0x411C3C),'font_key_prepare_static':(0x411C40,0x411E3E),
 'fixed_key_prepare_static':(0x448F40,0x448F92),'font_key_acquire_static':(0x4635D0,0x463702),
 'crt_rand_srand_static':(0x4D14D4,0x4D1505),'crt_tls_import_prefix_static':(0x4D7464,0x4D746C),
 'crt_time_import_prefix_static':(0x4D11E1,0x4D11F0),
}
def sha(b):return hashlib.sha256(b).hexdigest()
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--exe',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args();raw=a.exe.read_bytes();assert sha(raw)==HASH;pe=pefile.PE(data=raw);md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);blocks={}
 for key,(lo,hi) in BLOCKS.items():
  b=pe.get_data(lo-0x400000,hi-lo);ins=list(md.disasm(b,lo));assert sum(i.size for i in ins)==len(b),(key,len(b),sum(i.size for i in ins));blocks[key]=[dict(va=hex(i.address),rva=hex(i.address-0x400000),file_offset=hex(pe.get_offset_from_rva(i.address-0x400000)),bytes=i.bytes.hex(),mnemonic=i.mnemonic,operands=i.op_str) for i in ins]
 result=dict(target_sha256=HASH,target_version='1.17.0.1',original_code_executed=False,extractor_sha256=sha(Path(__file__).read_bytes()),blocks=blocks,static_boundaries=['Full4119f0 calls411c40 unconditionally on accepted nonnull inputs','411c40 uses CRT srand/rand then time+srand; CRT TLS access imports at4d7466 and time imports at4d11ea','Pure SHA-state and IDEA helpers do not call external imports','Decoder/key orchestration is reproduced on host, not executed as a complete native function','No retail key/owner bytes or private derived key/hash/schedule are published'])
 a.out.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(dict(blocks=len(blocks),instructions=sum(map(len,blocks.values())),sha256=result['extractor_sha256'])))
if __name__=='__main__':main()
