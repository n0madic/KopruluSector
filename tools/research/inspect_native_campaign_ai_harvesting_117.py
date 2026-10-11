#!/usr/bin/env python3
"""Inspect exact-hash WorkerAI mineral selection and connected harvest consumers."""
import argparse
import hashlib
import json
from pathlib import Path
import capstone
import pefile
import inspect_native_workers_117 as worker_source
import inspect_native_ai_construction_117 as construction_source
import inspect_native_harvesting_117 as harvest_source
TARGET_HASH = worker_source.TARGET_HASH
BLOCKS = {
    'worker_ai_decisions': worker_source.BLOCKS['worker_ai_decisions'],
    'worker_resource_order_selection': worker_source.BLOCKS['worker_resource_order_selection'],
    'worker_fallback_neutral_mineral_search': (0x431fe0, 0x432058),
    'mineral_candidate_geometry_visibility_height_distance': (0x432420, 0x43250c),
    'mineral_candidate_unmarked_and_unoccupied': (0x432510, 0x432556),
    'mineral_surrounding_tiles_unwalkable': (0x412c90, 0x412e1a),
    'mineral_worker_distance_with_ground_regions': (0x48e9b0, 0x48eb86),
    'short_region_path_endpoint_distance': (0x48ec20, 0x48ec43),
    'movement_wait_retry_state15': (0x42bfd0, 0x42c022),
    'moving_worker_alternative_mineral_search_radius384': (0x431f70, 0x431fd6),
    'resource_busy_byte_getter': (0x4242f0, 0x4242f8),
}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    data = args.exe.read_bytes()
    assert hashlib.sha256(data).hexdigest() == TARGET_HASH
    pe = pefile.PE(data=data)
    assert pe.OPTIONAL_HEADER.ImageBase == 0x400000
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    witnesses = {}
    for name, (lo, hi) in BLOCKS.items():
        rows = list(md.disasm(pe.get_data(lo - 0x400000, hi - lo), lo))
        assert rows and rows[-1].address + rows[-1].size == hi
        witnesses[name] = [dict(va=hex(row.address), rva=hex(row.address - 0x400000),
            file_offset=hex(pe.get_offset_from_rva(row.address - 0x400000)),
            bytes=row.bytes.hex(), mnemonic=row.mnemonic, operands=row.op_str) for row in rows]
    result = dict(target_sha256=TARGET_HASH, version='1.17.0.1', image_base='0x400000',
        script_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        scope='Static original instructions; no original execution',
        labels='Analytical labels, not original debug symbols',
        source_dependencies_sha256={Path(module.__file__).name: hashlib.sha256(Path(module.__file__).read_bytes()).hexdigest()
            for module in (worker_source, construction_source, harvest_source)},
        witnesses=witnesses)
    args.out.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(dict(bodies=len(witnesses), instructions=sum(map(len, witnesses.values())))))

if __name__ == '__main__':
    main()
