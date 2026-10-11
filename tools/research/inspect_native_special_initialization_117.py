#!/usr/bin/env python3
"""Record exact-hash campaign-special initialization witnesses."""
import argparse, hashlib, json, struct
from pathlib import Path
import capstone, pefile
HASH = '94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0'
BLOCKS = {
    'image_explicit_flip': (0x40F280, 0x40F33F),
    'iscript_overlay_sprite': (0x410320, 0x410371),
    'thingy_create': (0x417B80, 0x417BF4),
    'thingy_visibility_update': (0x417E50, 0x417EE6),
    'special_vision_update': (0x423260, 0x423328),
    'doodad_type_predicate': (0x425CA0, 0x425CC1),
    'special_completion_post': (0x424DE0, 0x424F36),
    'special_record_creation': (0x469460, 0x46955D),
    'trap_disabled_predicate': (0x429160, 0x4291A9),
    'doodad_status_set': (0x41D4C0, 0x41D4E1),
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
            'Initialization alone does not establish later trap firing, powerup pickup or rescue transitions.'])
    a.out.parent.mkdir(parents=True, exist_ok=True); a.out.write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps(dict(blocks=len(witnesses), instructions=sum(len(v['instructions']) for v in witnesses.values()), extractor_sha256=result['extractor_sha256'])))
if __name__ == '__main__': main()
