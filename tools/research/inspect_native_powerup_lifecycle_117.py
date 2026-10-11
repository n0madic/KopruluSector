#!/usr/bin/env python3
"""Record exact-hash carried-powerup drop and timed-return witnesses."""
import argparse, hashlib, json, struct
from pathlib import Path
import capstone, pefile
HASH = '94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0'
BLOCKS = {
    'powerup_drop': (0x4272C0, 0x4273B5),
    'powerup_timed_return': (0x427210, 0x42728F),
    'flag_return_message': (0x4216E0, 0x421751),
    'capture_flag_mode': (0x447CB0, 0x447CBD),
    'tile_included_excluded_flags': (0x413220, 0x41326B),
}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--exe', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True)
    a = p.parse_args(); raw = a.exe.read_bytes(); assert hashlib.sha256(raw).hexdigest() == HASH
    pe = pefile.PE(data=raw); assert pe.OPTIONAL_HEADER.ImageBase == 0x400000 and pe.FILE_HEADER.Machine == 0x14C
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32); witnesses = {}
    for name, (lo, hi) in BLOCKS.items():
        code = pe.get_data(lo - 0x400000, hi - lo); ins = list(md.disasm(code, lo))
        assert sum(i.size for i in ins) == len(code), name
        witnesses[name] = dict(preferred_va=hex(lo), rva=hex(lo-0x400000), end_exclusive=hex(hi),
            instructions=[dict(va=hex(i.address), rva=hex(i.address-0x400000), file_offset=hex(pe.get_offset_from_rva(i.address-0x400000)), bytes=i.bytes.hex(), mnemonic=i.mnemonic, operands=i.op_str) for i in ins])
    result = dict(target_sha256=HASH, target_version='1.17.0.1', original_code_executed=False,
        extractor_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(), witnesses=witnesses,
        dependency_boundaries=['Runtime owner/controller, finite pools and terrain/vision are explicit caller inputs.',
            'Pickup, carrier relocation, drop, Show and timed origin-return are composed from actual callees; finite pools and prepared flat terrain remain explicit inputs.',
            'Positive CTF-mode message formatting/display and the full primary-order dispatcher/cadence remain separate.'])
    destinations = struct.unpack('<100I', pe.get_data(0x39BCC, 400))
    mapping = list(pe.get_data(0x39D5C, 187))
    selected = {}
    for order in (17, 97):
        case_va = destinations[mapping[order-1]]
        case_bytes = pe.get_data(case_va-0x400000, 8)
        decoded = list(md.disasm(case_bytes, case_va))
        assert len(decoded) == 3 and decoded[-1].mnemonic == 'jmp'
        selected[str(order)] = dict(case_va=hex(case_va), target_va=decoded[-1].op_str,
            instructions=[dict(va=hex(i.address), bytes=i.bytes.hex(), mnemonic=i.mnemonic, operands=i.op_str) for i in decoded])
    result['selected_primary_order_dispatch'] = dict(stage=3, input_offset=1, mapping_va='0x439d5c',
        destinations_va='0x439bcc', mapping=mapping, destinations=[hex(x) for x in destinations], selected=selected,
        boundary='Static dispatch witness only; probes issue the actual order and call the selected complete handler, not the full primary dispatcher.')
    a.out.parent.mkdir(parents=True, exist_ok=True); a.out.write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps(dict(blocks=len(witnesses), instructions=sum(len(v['instructions']) for v in witnesses.values()), extractor_sha256=result['extractor_sha256'])))
if __name__ == '__main__': main()
