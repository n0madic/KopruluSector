#!/usr/bin/env python3
"""Record exact-hash campaign-special order consumers witnesses."""
import argparse, hashlib, json, struct
from pathlib import Path
import capstone, pefile
HASH = '94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0'
BLOCKS = {
    'primary_order_stage3_dispatch': (0x439640, 0x43968A),
    'iscript_heading_move_target': (0x40CF00, 0x40CF50),
    'acquisition_range': (0x41F320, 0x41F42D),
    'attack_candidate_bucket': (0x432280, 0x432383),
    'flag_pickup_message': (0x421760, 0x4217E6),
    'capture_flag_mode': (0x447CB0, 0x447CBD),
    'image_owner_remap': (0x4116D0, 0x4116E0),
    'disable_doodad': (0x428010, 0x428145),
    'enable_doodad': (0x4281F0, 0x428304),
    'door_type_predicate': (0x4290A0, 0x4290D5),
    'powerup_worker_predicate': (0x431980, 0x4319C8),
    'powerup_pickup': (0x4270F0, 0x4271C3),
    'nearby_first_matching_wrapper': (0x442B20, 0x442B5D),
    'nearby_first_matching': (0x442B60, 0x442BBA),
    'prepend_order': (0x429B20, 0x429B63),
    'hold_target_selector': (0x432060, 0x432065),
    'hold_target_acquisition': (0x431B70, 0x431C18),
    'door_close': (0x428150, 0x42819D),
    'door_open': (0x4281A0, 0x4281EC),
    'trap_idle': (0x428490, 0x42859D),
    'trap_hide': (0x4285A0, 0x428652),
    'trap_reveal': (0x4288A0, 0x4288F0),
    'rescue_passive': (0x48D120, 0x48D2C7),
    'rescue_feedback': (0x446B30, 0x446B62),
    'powerup_idle': (0x427290, 0x4272BC),
    'powerup_worker_find': (0x431C20, 0x431C3B),
    'unlock_primary_then_done': (0x429D00, 0x429D0F),
    'mark_ai_regions_changed': (0x48DD40, 0x48DD4B),
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
            'Animation signals, runtime controller and spatial inputs are distinguished from the original state producers where composed; positive spell/creep/external sound imports remain excluded.'])
    destinations = struct.unpack('<100I', pe.get_data(0x39BCC, 400))
    mapping = list(pe.get_data(0x39D5C, 187))
    selected = {}
    for order in (97, 161, 167, 168, 169, 170, 171, 172, 173):
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
