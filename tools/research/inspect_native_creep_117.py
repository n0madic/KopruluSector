#!/usr/bin/env python3
"""Extract exact-hash native creep geometry, neighborhood, pool and Move consumers."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import capstone
import pefile
TARGET_HASH='94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0'
BLOCKS={
 'creep_ellipse_outside':(0x4C0A80,0x4C0A9A),
 'creep_neighbor_count':(0x4C0060,0x4C0244),
 'creep_sustaining_unit_predicate':(0x4C0830,0x4C08B8),
 'creep_sustaining_query_wrapper':(0x4C0250,0x4C02EC),
 'creep_sustaining_query_callback':(0x4B4170,0x4B4229),
 'creep_life_pool_initialize':(0x4B44A0,0x4B4544),
 'creep_life_allocate':(0x4B3F30,0x4B400C),
 'creep_life_hash_insert':(0x4B4430,0x4B4497),
 'creep_life_hash_remove':(0x4B4D70,0x4B4DC3),
 'creep_life_cancel_tile':(0x4B4930,0x4B4A07),
 'creep_life_cancel_unit_area':(0x4B4DD0,0x4B4EA8),
 'creep_old_provider_remove':(0x4B46E0,0x4B47EF),
 'creep_bounds':(0x4C04F0,0x4C066D),
 'creep_provider_type':(0x4C0790,0x4C07B7),
 'creep_tile_group_one':(0x4C07E0,0x4C0808),
 'creep_tile_physical_blocked':(0x4B45B0,0x4B45F0),
 'creep_tile_occupied':(0x4B4550,0x4B455D),
 'creep_tile_flags_mask':(0x413270,0x4132A8),
 'creep_source_rng26':(0x4B4920,0x4B492A),
 'creep_new_provider_wrapper':(0x43C220,0x43C318),
 'creep_footprint_stamp':(0x43C130,0x43C213),
 'creep_spread_complete':(0x4C0AA0,0x4C0B97),
 'creep_spread_one':(0x4C0E60,0x4C101A),
 'creep_map_record_update':(0x4C0CA0,0x4C0D8B),
 'creep_map_variant_callback':(0x4C0C40,0x4C0CA0),
 'creep_stamp_one_wrapper':(0x4C0DC0,0x4C0DFB),
 'creep_neighbors_walk':(0x4C03F0,0x4C046D),
 'creep_neighbor_graphic_update':(0x4C0470,0x4C04E9),
 'creep_neighbor_mask_callback':(0x4C0E00,0x4C0E58),
 'creep_mask_to_graphic_callback':(0x4C0D90,0x4C0DBD),
 'creep_mark_fog_dirty':(0x46E8C0,0x46E8C8),
 'creep_mask_transform_initialize':(0x4BFF60,0x4C000F),
 'creep_graphic_rank_initialize':(0x4C0010,0x4C005D),
 'crt_rand':(0x4D14D4,0x4D14F5),
 'crt_rand_pure_tail':(0x4D14D9,0x4D14F5),
 'crt_get_thread_context':(0x4D744C,0x4D7464),
 'crt_get_thread_context_noexit':(0x4D7464,0x4D74D3),
}
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--exe',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
 raw=a.exe.read_bytes();assert hashlib.sha256(raw).hexdigest()==TARGET_HASH
 pe=pefile.PE(data=raw);assert pe.OPTIONAL_HEADER.ImageBase==0x400000 and pe.FILE_HEADER.Machine==0x14C
 md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);witnesses={}
 for key,(lo,hi) in BLOCKS.items():
  code=pe.get_data(lo-0x400000,hi-lo);ins=list(md.disasm(code,lo));length=sum(x.size for x in ins)
  assert length==len(code),(key,hex(lo),length,len(code))
  witnesses[key]=dict(preferred_va=hex(lo),rva=hex(lo-0x400000),end_exclusive=hex(hi),instructions=[dict(va=hex(x.address),rva=hex(x.address-0x400000),bytes=x.bytes.hex(),mnemonic=x.mnemonic,operands=x.op_str) for x in ins])
 result=dict(target_sha256=TARGET_HASH,target_version='1.17.0.1',original_code_executed=False,extractor_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),witnesses=witnesses,
  creep_bounds_switch=dict(destinations=[hex(x) for x in struct.unpack('<3I',pe.get_data(0xC0670,12))],mapping=list(pe.get_data(0xC067C,16))),
  creep_provider_switch=dict(destinations=[hex(x) for x in struct.unpack('<3I',pe.get_data(0xC07B8,12))],mapping=list(pe.get_data(0xC07C4,16))),
  neighborhood_offsets=[list(struct.unpack('<2i',pe.get_data(0xF0FD8+i*8,8))) for i in range(9)],
  blocked_crt_imports=[dict(dll=e.dll.decode(),name=imp.name.decode() if imp.name else None,iat_va=hex(imp.address)) for e in pe.DIRECTORY_ENTRY_IMPORT for imp in e.imports if imp.address in (0x4EB17C,0x4EB264)],
  evidence_boundary='Original static instructions; CRT rand context acquisition and graphics/resource callbacks remain separate dependencies')
 a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_text(json.dumps(result,indent=2)+'\n')
 print(json.dumps(dict(blocks=len(witnesses),instructions=sum(len(x['instructions']) for x in witnesses.values()),extractor_sha256=result['extractor_sha256'])))
if __name__=='__main__':main()
