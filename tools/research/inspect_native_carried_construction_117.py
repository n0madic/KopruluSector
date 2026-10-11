#!/usr/bin/env python3
"""Inspect exact original Terran retention versus Drone construction Drop/origin calls."""
import argparse,hashlib,json
from pathlib import Path
import capstone,pefile
import inspect_native_construction_117 as terran
import inspect_native_zerg_construction_117 as zerg
from inspect_native_special_producers_117 import HASH
BLOCKS={
 'terran_place_building':terran.BLOCKS['terran_place_building_order'],
 'terran_constructing':terran.BLOCKS['terran_construction_order'],
 'drone_build26':zerg.BLOCKS['drone_build26'],
 'drone_to_building':zerg.BLOCKS['drone_to_zerg_building'],
 'carried_object_getter':zerg.BLOCKS['carried_powerup_reference'],
 'drop_powerup':(0x4272C0,0x4273B5),
 'show_powerup_idle':(0x427290,0x4272BC),
}
def main():
 p=argparse.ArgumentParser(description=__doc__)
 for name in ('exe','out'):p.add_argument('--'+name,type=Path,required=True)
 a=p.parse_args();raw=a.exe.read_bytes();assert hashlib.sha256(raw).hexdigest()==HASH;pe=pefile.PE(data=raw);md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);witnesses={}
 for name,(lo,hi) in BLOCKS.items():
  code=pe.get_data(lo-0x400000,hi-lo);ins=list(md.disasm(code,lo));assert sum(x.size for x in ins)==len(code)
  witnesses[name]=dict(preferred_va=hex(lo),rva=hex(lo-0x400000),end_exclusive=hex(hi),instructions=[dict(va=hex(i.address),rva=hex(i.address-0x400000),file_offset=hex(pe.get_offset_from_rva(i.address-0x400000)),bytes=i.bytes.hex(),mnemonic=i.mnemonic,operands=i.op_str) for i in ins],direct_calls=[dict(va=hex(i.address),target=i.op_str) for i in ins if i.mnemonic=='call' and i.op_str.startswith('0x')])
 result=dict(target_sha256=HASH,target_version='1.17.0.1',extractor_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),original_code_executed=False,witnesses=witnesses,source_dependencies_sha256={Path(m.__file__).name:hashlib.sha256(Path(m.__file__).read_bytes()).hexdigest() for m in (terran,zerg)},scope='Static original construction/carry contract; positive branches require separate unchanged original-code probes')
 a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(dict(blocks=len(witnesses),instructions=sum(len(v['instructions']) for v in witnesses.values()))))
if __name__=='__main__':main()
