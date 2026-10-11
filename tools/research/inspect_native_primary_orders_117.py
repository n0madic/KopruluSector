#!/usr/bin/env python3
"""Read exact-hash 1.17 primary-order witnesses; never start the PE."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import capstone
import pefile

TARGET_HASH="94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0"
ORDERS_HASH="0b79cc64f1cd8b23c77419bdf1fb39fb91cde1359c1ad9cb73884d5cceb8f7c9"
BLOCKS={
    "allocate_order":(0x413C80,0x413CC6),
    "free_order":(0x413D50,0x413D66),
    "list_insert_after":(0x411900,0x411927),
    "list_insert_after_head":(0x411930,0x411968),
    "list_insert_before":(0x411970,0x411995),
    "list_remove":(0x4119A0,0x4119E2),
    "append_order_after":(0x4295D0,0x42964F),
    "clear_order_queue":(0x429AC0,0x429B14),
    "insert_order_before":(0x429E10,0x429E86),
    "prepend_position_order":(0x429D70,0x429D9F),
    "append_primary_order":(0x429F60,0x42A0B8),
    "issue_order_no_target":(0x42A2C0,0x42A2F4),
    "issue_order_fow_position":(0x42A300,0x42A33E),
    "issue_order_position":(0x42A340,0x42A380),
    "issue_order_unit":(0x42A380,0x42A3D4),
    "issue_order_packed_position_unit":(0x42A3E0,0x42A40E),
    "order_done":(0x42A5A0,0x42A60D),
    "activate_next_order":(0x42A610,0x42A842),
    "set_order_fow_position":(0x4179E0,0x417A10),
    "set_order_position":(0x417A10,0x417A41),
    "set_order_target":(0x417A50,0x417A9F),
    "reset_order_animation":(0x420910,0x42094F),
    "flingy_to_idle":(0x40CEB0,0x40CEC1),
    "sprite_to_idle_animation":(0x4176E0,0x417765),
    "stat_text_lookup":(0x4A7D70,0x4A7D7D),
    "tbl_lookup":(0x4C13B0,0x4C13D7),
    "order_exhaustion_notification_gate":(0x46AE50,0x46AE6E),
    "local_order_exhaustion_notification":(0x46AE70,0x46AEBD),
}

def stock_orders(raw):
    if len(raw)!=4158 or hashlib.sha256(raw).hexdigest()!=ORDERS_HASH:
        raise ValueError("Unknown effective retail orders.dat")
    # Verified against pinned PyMS OrdersDAT schema and the native 19 descriptors.
    return [{"id":i,"interruptible":raw[1323+i],"highlight":struct.unpack_from('<H',raw,3213+i*2)[0],
             "subunit_inheritance":raw[945+i]} for i in range(189)]

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--exe",type=Path,required=True);p.add_argument("--out",type=Path,required=True)
    p.add_argument("--orders-dat",type=Path,required=True)
    a=p.parse_args();raw=a.exe.read_bytes()
    if hashlib.sha256(raw).hexdigest()!=TARGET_HASH:p.error("Unknown executable hash")
    pe=pefile.PE(data=raw);assert pe.OPTIONAL_HEADER.ImageBase==0x400000 and pe.FILE_HEADER.Machine==0x14C
    md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32)
    def read(va,size):
        offset=pe.get_offset_from_rva(va-0x400000);return raw[offset:offset+size]
    witnesses={}
    for label,(start,end) in BLOCKS.items():
        data=read(start,end-start);ins=list(md.disasm(data,start));decoded=sum(x.size for x in ins)
        witnesses[label]={"preferred_va":hex(start),"rva":hex(start-0x400000),"end_exclusive":hex(end),
            "requested_bytes":len(data),"decoded_bytes":decoded,"undecoded_tail":data[decoded:].hex(),
            "instructions":[{"va":hex(x.address),"rva":hex(x.address-0x400000),
                "file_offset":hex(pe.get_offset_from_rva(x.address-0x400000)),"bytes":x.bytes.hex(),
                "mnemonic":x.mnemonic,"operands":x.op_str} for x in ins]}
    descriptors=[{"index":i,"descriptor_va":hex(0x501278+i*12),
        "target_va":hex(struct.unpack('<III',read(0x501278+i*12,12))[0]),
        "entry_width":struct.unpack('<III',read(0x501278+i*12,12))[1],
        "count":struct.unpack('<III',read(0x501278+i*12,12))[2]} for i in range(19)]
    rows=stock_orders(a.orders_dat.read_bytes())
    result={"target_sha256":TARGET_HASH,"target_version":"1.17.0.1","original_code_executed":False,
        "extractor_sha256":hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),"witnesses":witnesses,
        "idle_animation_switch":{"targets":[hex(x) for x in struct.unpack('<6I',read(0x417768,24))],
            "mapping":list(read(0x417780,14)),"input_animation_low":2},
        "order_dat_tables":{"interruptible":"0x51b200","highlight_u16":"0x51b980","subunit_inheritance":"0x51b080"},
        "native_dat_descriptors":descriptors,
        "stock_order_fields":{"sha256":ORDERS_HASH,"bytes":4158,"effective_archive":"BrooDat.mpq",
            "archive_path":"arr\\orders.dat","manifest":"retail-manifest.json","rows":rows,
            "format_source":"PyMS/FileFormats/DAT/OrdersDAT.py",
            "format_commit":"bfc5d3aad0b5614a5aff72c223f8efa00afddfa4"},
        "source_pins":{"GPTP":"ce321f0fa83174aee741b91f1b2eaec30300773e","Teippi":"05c006c2f74ad11285c39d37135aed03d1fb8806"}}
    a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_text(json.dumps(result,indent=2)+"\n")
    print(json.dumps({"witnesses":len(witnesses),"instructions":sum(len(x['instructions']) for x in witnesses.values()),
        "tails":{k:v['undecoded_tail'] for k,v in witnesses.items() if v['undecoded_tail']}}))

if __name__=="__main__":main()
