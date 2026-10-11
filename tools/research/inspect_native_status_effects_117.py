#!/usr/bin/env python3
"""Read exact-hash populated periodic spell-effect witnesses as data."""
import argparse,hashlib,json,struct
from pathlib import Path
import pefile,capstone
TARGET_HASH='94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0'
BLOCKS={
 'Plague_area':(0x43DB50,0x43DB66),
 'Plague_candidate':(0x43DE10,0x43DE43),
 'Plague_apply':(0x43E0A0,0x43E0D9),
 'Plague_tick_damage':(0x43DDB0,0x43DDE9),
 'Plague_clear':(0x43DDF0,0x43DE0B),
 'Ensnare_area':(0x43DC90,0x43DCD3),
 'Ensnare_candidate':(0x43DF10,0x43DF3B),
 'Ensnare_apply':(0x43E0E0,0x43E11F),
 'Acid_area':(0x43DBF0,0x43DC06),
 'Acid_candidate':(0x43DE50,0x43DE9D),
 'Acid_apply':(0x43DCE0,0x43DDA5),
 'Acid_clear':(0x43DEA0,0x43DED2),
 'Stasis_area':(0x430880,0x4308C3),
 'Stasis_candidate':(0x430920,0x430952),
 'Stasis_apply':(0x431090,0x4310EC),
 'Stasis_clear':(0x4310F0,0x431155),
 'disable_for_spell':(0x434C60,0x434D06),
 'remove_disabling_overlay':(0x4343B0,0x434459),
 'spell_attack_notification':(0x4203D0,0x42051F),
 'has_exact_unit_overlay':(0x422B60,0x422B9A),
}
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--exe',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
 raw=a.exe.read_bytes();assert hashlib.sha256(raw).hexdigest()==TARGET_HASH;pe=pefile.PE(data=raw)
 assert pe.OPTIONAL_HEADER.ImageBase==0x400000 and pe.FILE_HEADER.Machine==0x14c
 md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);witnesses={}
 for name,(lo,hi)in BLOCKS.items():
  code=pe.get_data(lo-0x400000,hi-lo);ins=list(md.disasm(code,lo));assert sum(i.size for i in ins)==len(code)
  witnesses[name]={'start_preferred_va':hex(lo),'end_exclusive_preferred_va':hex(hi),'bytes_sha256':hashlib.sha256(code).hexdigest(),'instructions':[{'address':hex(i.address),'bytes':i.bytes.hex(),'mnemonic':i.mnemonic,'operands':i.op_str}for i in ins]}
 r={'target_sha256':TARGET_HASH,'target_version':'1.17.0.1','inspector_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),'original_code_executed':False,'witnesses':witnesses}
 a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_text(json.dumps(r,indent=2)+'\n');print(json.dumps({'witnesses':len(witnesses),'instructions':sum(len(w['instructions'])for w in witnesses.values())}))
if __name__=='__main__':main()
