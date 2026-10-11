#!/usr/bin/env python3
"""Record exact-hash mission UNIT loader/default/relationship witnesses."""
import argparse, hashlib, json, struct
from pathlib import Path
import capstone, pefile
HASH = '94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0'
BLOCKS = {
    'unit_section_loader': (0x468F30, 0x469125),
    'unit_record_creation': (0x469460, 0x46955D),
    'unit_record_mode_filter': (0x469600, 0x46964F),
    'start_location_consumer': (0x46A280, 0x46A31E),
    'unit_record_properties': (0x46A420, 0x46A601),
    'ums_profile_predicate': (0x447DB0, 0x447DD5),
    'computer_slot_predicate': (0x479610, 0x479623),
    'addon_relationship_record': (0x469360, 0x4693CC),
    'nydus_relationship_record': (0x4693D0, 0x469433),
    'addon_relationship_resolver': (0x469810, 0x4698D8),
    'nydus_relationship_resolver': (0x4698E0, 0x46996B),
    'relationship_remove': (0x46A320, 0x46A365),
    'live_visibility_finalize': (0x46A370, 0x46A390),
    'visibility_unit_finalize': (0x438C20, 0x438CC3),
    'special_disabled_unit_predicate': (0x429160, 0x4291A9),
    'unit_doodad_state_set': (0x41D4C0, 0x41D4E1),
    'setup_players_copy_all': (0x479BF0, 0x479C31),
    'setup_player_copy_one': (0x479C40, 0x479C79),
    'computer_slot_clear': (0x479C80, 0x479C8C),
    'computer_slot_set': (0x478A20, 0x478A30),
    'creation_error_notification': (0x425020, 0x42504C),
    'network_string_get': (0x43F0C0, 0x43F0CD),
    'tbl_string_get': (0x4C13B0, 0x4C13D7),
    'owner_message_gate': (0x46AE50, 0x46AEBD),
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
        dependency_boundaries=['Positive outer loader allocates/frees through Storm imports; relationships allocate/free through the same imports.',
            'Controller and computer-slot tables are caller inputs; this witness alone does not establish their earlier player-profile normalization.'])
    a.out.parent.mkdir(parents=True, exist_ok=True); a.out.write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps(dict(blocks=len(witnesses), instructions=sum(len(v['instructions']) for v in witnesses.values()), extractor_sha256=result['extractor_sha256'])))
if __name__ == '__main__': main()
