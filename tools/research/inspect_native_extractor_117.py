#!/usr/bin/env python3
"""Inspect exact original paid Extractor/Geyser conversion and cancellation."""
import argparse,hashlib,json
from pathlib import Path
import capstone,pefile
import inspect_native_carried_construction_117 as carried
import inspect_native_image_initialization_117 as initialized
import inspect_native_zerg_construction_117 as zerg
import inspect_native_creep_117 as creep
from inspect_native_special_producers_117 import HASH
BLOCKS={
 'build_refinery_on_geyser':(0x41B3F0,0x41B4AD),
 'drone_extractor_branch':(0x43BF80,0x43BFFF),
 'placement_failure_nonlocal_notice':(0x4465E0,0x44664A),
 'remove_queued_order_head':(0x42A140,0x42A17F),
 'placement_ui_mode_getter':(0x47CD70,0x47CD78),
 'extractor_creep_restore_scan':(0x4C0960,0x4C0A78),
 'extractor_creep_false_callback':(0x4265D0,0x4265D5),
 'geyser_overlay_below_main_image':(0x4152D0,0x415335),
 'refinery_release_restore_geyser':initialized.BLOCKS['unit_release_special_or_hidden'],
 'cancel_zerg_construction':zerg.BLOCKS['cancel_zerg_construction'],
 'drone_build26':zerg.BLOCKS['drone_build26'],
 'drone_to_building':zerg.BLOCKS['drone_to_zerg_building'],
 'carried_object_getter':zerg.BLOCKS['carried_powerup_reference'],
 'drop_powerup':(0x4272C0,0x4273B5),
 'show_powerup_idle':(0x427290,0x4272BC),
 'zerg_build_self45':(0x43B6E0,0x43B8E3),
 **{name:creep.BLOCKS[name] for name in ('creep_provider_type','creep_new_provider_wrapper','creep_footprint_stamp','creep_map_record_update','creep_map_variant_callback','creep_stamp_one_wrapper','crt_rand','crt_get_thread_context','crt_get_thread_context_noexit')},
}
def main():
 p=argparse.ArgumentParser(description=__doc__)
 for name in ('exe','out'):p.add_argument('--'+name,type=Path,required=True)
 a=p.parse_args();raw=a.exe.read_bytes();assert hashlib.sha256(raw).hexdigest()==HASH;pe=pefile.PE(data=raw);md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);witnesses={}
 for name,(lo,hi) in BLOCKS.items():
  code=pe.get_data(lo-0x400000,hi-lo);ins=list(md.disasm(code,lo));assert sum(x.size for x in ins)==len(code)
  witnesses[name]=dict(preferred_va=hex(lo),rva=hex(lo-0x400000),end_exclusive=hex(hi),instructions=[dict(va=hex(i.address),rva=hex(i.address-0x400000),file_offset=hex(pe.get_offset_from_rva(i.address-0x400000)),bytes=i.bytes.hex(),mnemonic=i.mnemonic,operands=i.op_str) for i in ins],direct_calls=[dict(va=hex(i.address),target=i.op_str) for i in ins if i.mnemonic=='call' and i.op_str.startswith('0x')])
 result=dict(target_sha256=HASH,target_version='1.17.0.1',extractor_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),original_code_executed=False,witnesses=witnesses,source_dependencies_sha256={Path(m.__file__).name:hashlib.sha256(Path(m.__file__).read_bytes()).hexdigest() for m in (carried,initialized,zerg,creep)},scope='Static original Extractor/carry/restore and completion-creep contract; selected positive branches require separate original-code probes')
 a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(dict(blocks=len(witnesses),instructions=sum(len(v['instructions']) for v in witnesses.values()))))
if __name__=='__main__':main()
