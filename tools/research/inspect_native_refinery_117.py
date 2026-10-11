#!/usr/bin/env python3
"""Inspect exact original Terran Refinery/Geyser paid construction contracts."""
import argparse,hashlib,json
from pathlib import Path
import capstone,pefile
import inspect_native_extractor_117 as extractor
import inspect_native_construction_117 as construction
import inspect_native_gas_harvesting_117 as gas
import inspect_native_harvesting_117 as harvesting
HASH=extractor.HASH
BLOCKS={
 'build_refinery_on_geyser':extractor.BLOCKS['build_refinery_on_geyser'],
 'geyser_overlay_below_main_image':extractor.BLOCKS['geyser_overlay_below_main_image'],
 'placement_ui_mode_getter':extractor.BLOCKS['placement_ui_mode_getter'],
 'refinery_release_restore_geyser':extractor.BLOCKS['refinery_release_restore_geyser'],
 'clear_refinery_waiting_workers':(0x423400,0x42347B),
 **{name:construction.BLOCKS[name] for name in ('terran_place_building_order','terran_self_build_order','terran_construction_order','queued_build_validate_and_pay','construction_ai_assignment','cancel_construction','building_three_quarter_refund','build_progress_hp_and_completion','terran_stop_construction_restore_worker')},
 **{name:gas.BLOCKS[name] for name in ('completed_owned_refinery_predicate','harvest_gas_order','wait_for_gas_order','gas_exit_placement_and_angle','resource_wait_list_unlink')},
 **{name:harvesting.BLOCKS[name] for name in ('resource_to_worker_cargo','cargo_deposit')},
}
def main():
 p=argparse.ArgumentParser(description=__doc__)
 for name in ('exe','out'):p.add_argument('--'+name,type=Path,required=True)
 a=p.parse_args();raw=a.exe.read_bytes();assert hashlib.sha256(raw).hexdigest()==HASH;pe=pefile.PE(data=raw);md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);witnesses={}
 for name,(lo,hi) in BLOCKS.items():
  code=pe.get_data(lo-0x400000,hi-lo);ins=list(md.disasm(code,lo));assert sum(x.size for x in ins)==len(code)
  witnesses[name]=dict(preferred_va=hex(lo),rva=hex(lo-0x400000),end_exclusive=hex(hi),instructions=[dict(va=hex(i.address),rva=hex(i.address-0x400000),file_offset=hex(pe.get_offset_from_rva(i.address-0x400000)),bytes=i.bytes.hex(),mnemonic=i.mnemonic,operands=i.op_str) for i in ins],direct_calls=[dict(va=hex(i.address),target=i.op_str) for i in ins if i.mnemonic=='call' and i.op_str.startswith('0x')])
 result=dict(target_sha256=HASH,target_version='1.17.0.1',extractor_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),original_code_executed=False,witnesses=witnesses,source_dependencies_sha256={Path(m.__file__).name:hashlib.sha256(Path(m.__file__).read_bytes()).hexdigest() for m in (extractor,construction,gas,harvesting)},scope='Static original paid Refinery/carry/gas/restoration contract; selected positive branches require separate original-code probes')
 a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(dict(blocks=len(witnesses),instructions=sum(len(v['instructions']) for v in witnesses.values()))))
if __name__=='__main__':main()
