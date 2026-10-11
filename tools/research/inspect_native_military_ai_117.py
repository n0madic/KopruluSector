#!/usr/bin/env python3
"""Extract reviewed native military/group witnesses without executing the PE."""
import argparse
import hashlib
import json
from pathlib import Path
import capstone
import pefile

TARGET_HASH = "94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0"
BLOCKS = {
    "point_in_area": (0x417AA0,0x417AC8),
    "stim_speed_modifier": (0x428AF0,0x428B18),
    "unit_race": (0x428D60,0x428D83),
    "queued_order_id": (0x429D10,0x429D17),
    "region_center": (0x481A30,0x481A5C),
    "region_neighbors": (0x481AD0,0x481AF7),
    "pixel_region": (0x481B10,0x481B98),
    "unit_region": (0x481BA0,0x481BB0),
    "region_group": (0x481C90,0x481CA4),
    "regions_reachable": (0x4AB330,0x4AB41A),
    "units_regions_reachable": (0x4AB420,0x4AB447),
    "regional_slowest_unit": (0x4AFAF0,0x4AFB1D),
    "transport_pickup_wait": (0x4AFEC0,0x4AFF1A),
    "group_wait_for_member": (0x4AFF20,0x4B0024),
    "follower_ready": (0x4B09C0,0x4B0AE5),
    "stopped_rejoin_region": (0x4B0E10,0x4B0EB9),
    "ai_stim_decision": (0x4B1100,0x4B11AB),
    "guard_return_home": (0x4B2110,0x4B21F0),
}

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe",type=Path,required=True)
    parser.add_argument("--out",type=Path,required=True)
    args=parser.parse_args()
    raw=args.exe.read_bytes()
    if hashlib.sha256(raw).hexdigest()!=TARGET_HASH:
        parser.error("Unknown executable hash")
    pe=pefile.PE(data=raw)
    assert pe.OPTIONAL_HEADER.ImageBase==0x400000 and pe.FILE_HEADER.Machine==0x14C
    md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32)
    witnesses={}
    for label,(start,end) in BLOCKS.items():
        offset=pe.get_offset_from_rva(start-0x400000)
        code=raw[offset:offset+end-start]
        ins=list(md.disasm(code,start))
        decoded=sum(x.size for x in ins)
        witnesses[label]={"preferred_va":hex(start),"rva":hex(start-0x400000),
            "end_exclusive":hex(end),"requested_bytes":len(code),"decoded_bytes":decoded,
            "undecoded_tail":code[decoded:].hex(),
            "instructions":[{"va":hex(x.address),"rva":hex(x.address-0x400000),
                "file_offset":hex(pe.get_offset_from_rva(x.address-0x400000)),
                "bytes":x.bytes.hex(),"mnemonic":x.mnemonic,"operands":x.op_str} for x in ins]}
    result={"target_sha256":TARGET_HASH,"target_version":"1.17.0.1","original_code_executed":False,
            "extractor_sha256":hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),"witnesses":witnesses}
    args.out.parent.mkdir(parents=True,exist_ok=True)
    args.out.write_text(json.dumps(result,indent=2)+"\n")
    print(json.dumps({"witnesses":len(witnesses),"instructions":sum(len(x["instructions"]) for x in witnesses.values()),
                      "tails":{k:v["undecoded_tail"] for k,v in witnesses.items() if v["undecoded_tail"]}}))

if __name__=="__main__":main()
