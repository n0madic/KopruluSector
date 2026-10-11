#!/usr/bin/env python3
"""Exact-hash indexed GRP blitters, color wrappers and static resource-load anchors."""
import argparse,hashlib,json,struct
from pathlib import Path
import pefile,capstone
HASH='94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0'
BLOCKS={
 'normal':(0x405E1E,0x405F64),'normal_mirrored':(0x4071C0,0x40730E),
 'blend':(0x4067F6,0x406950),'blend_mirrored':(0x406F03,0x407064),
 'shadow':(0x406950,0x406A84),'shadow_mirrored':(0x40730E,0x407451),
 'background_remap':(0x4066A1,0x4067F6),'background_remap_mirrored':(0x407064,0x4071C0),
 'selection_color':(0x40E900,0x40E950),
 'player_override':(0x40E950,0x40E98C),'player_override_mirrored':(0x40E990,0x40E9CD),
 'shift':(0x40EA60,0x40EAC0),'shift_mirrored':(0x40EAC0,0x40EB20),
 'fire':(0x40EB20,0x40EB3B),'fire_mirrored':(0x40EB40,0x40EB5B),
 'get_player_color':(0x41D540,0x41D546),'set_player_color':(0x41D740,0x41D7A5),
 'player_colors_load_static':(0x41D570,0x41D5D0),
 'blend_palettes_load_static':(0x466810,0x4668F0),
}
STATIC_ONLY={'player_colors_load_static','blend_palettes_load_static'}
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--exe',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
 raw=a.exe.read_bytes();assert hashlib.sha256(raw).hexdigest()==HASH;pe=pefile.PE(data=raw);md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);out={}
 for name,(lo,hi) in BLOCKS.items():
  b=pe.get_data(lo-0x400000,hi-lo);ins=list(md.disasm(b,lo));assert sum(i.size for i in ins)==len(b),(name,len(b),sum(i.size for i in ins))
  out[name]=[dict(va=hex(i.address),rva=hex(i.address-0x400000),bytes=i.bytes.hex(),mnemonic=i.mnemonic,operands=i.op_str) for i in ins]
 table=[dict(method=i,normal=hex(struct.unpack('<I',pe.get_data(0x1030D4+i*12,4))[0]),mirrored=hex(struct.unpack('<I',pe.get_data(0x1030D8+i*12,4))[0])) for i in range(18)]
 result=dict(target_sha256=HASH,target_version='1.17.0.1',original_code_executed=False,extractor_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),blocks=out,drawing_dispatch=table,static_only_blocks=sorted(STATIC_ONLY),evidence_boundary='Original indexed-buffer functions; loader static only; no Windows/game/render observation')
 a.out.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(dict(blocks=len(out),instructions=sum(len(v) for v in out.values()),sha256=result['extractor_sha256'])))
if __name__=='__main__':main()
