#!/usr/bin/env python3
"""Probe original 1.17 contour traversal and native signed-coordinate sorting.

Default mode is static. Explicit execution starts after allocation and stops
before free/shrink. Pure init/qsort helpers return completely; no imported
allocator or helper stub executes. Comparison model follows pinned OpenBW
geometry, with native LUTs and byte output independently checked.
"""

import argparse
from collections import Counter
import json
from pathlib import Path
import platform
import random
import struct

import capstone
import pefile
import inspect_terrain_assets as assets
import probe_native_region_generation_117 as generation
import probe_native_campaign_regions_117 as campaign

TARGET_HASH = generation.TARGET_HASH
CONTEXT, ARRAYS, STACK, STOP, WALK = 0x800000, 0x810000, 0x900000, 0xA00000, 0xB00000
BLOCKS = {
    "contour_builder": (0x48A7F0, 0x48AD39),
    "next_contour_start": (0x48AF50, 0x48B035),
    "contour_compare": (0x48AD40, 0x48AD7D),
    "contour_context_init": (0x488DF0, 0x488E09),
    "native_memset": (0x4D0C60, 0x4D0CF0),
    "native_qsort": (0x4D4490, 0x4D47DF),
    "native_shortsort": (0x4D47E0, 0x4D4857),
    "native_swap": (0x4D4860, 0x4D4897),
    "native_security_cookie": (0x4D9832, 0x4D983C),
}
TABLES = {"start_direction": (0x508EE8, "<16b", 16),
          "start_corner_mode": (0x508EF8, "<16b", 16),
          "start_coordinates": (0x508F08, "<16h", 32),
          "end_coordinates": (0x508F28, "<16h", 32),
          "walk_offsets": (0x508F48, "<4h", 8),
          "walk_deltas": (0x508F50, "<8b", 8),
          "diagonal_deltas": (0x508F58, "<8b", 8)}
LIMIT = 100000000


def contour_reference(walk):
    rows=[list(row) for row in walk]; h=len(rows); w=len(rows[0]); dirs=[(0,-1),(1,0),(0,1),(-1,0)]
    clut=[-1,-1,8,-1,8,8,-1,8,0,-1,8,0,7,8,-1,7]
    nlut=[8,-1,8,8,-1,8,-1,-1,7,-1,8,7,0,8,-1,0]
    diag=[(1,-1),(1,1),(-1,1),(-1,-1)]
    out=[[],[],[],[]]; nx=ny=0
    def flag(x,y):
        return rows[y][x] if 0<=x<w and 0<=y<h else 0
    def all_dirs(x,y):return flag(x,y)&15==0
    def walkable(x,y):return flag(x,y)&128==0
    def isdir(x,y,d):return flag(x,y)&(1<<d)==0
    def flip(x,y,d):
        assert 0<=x<w and 0<=y<h,(x,y,d)
        rows[y][x]^=1<<d
    iterations=0
    while True:
        x,y=nx,ny
        if x>=w:
            x=0;y=(y+1)%h
        x=x//4*4;sx,sy=x,y
        while all(all_dirs(x+o,y) for o in range(4)):
            x+=4
            if x==w:x=0;y=(y+1)%h
            if (x,y)==(sx,sy):return out,rows
        while not walkable(x,y) or all_dirs(x,y):
            x+=1;assert x<w
        nx,ny=x,y
        if isdir(x,y,0) and isdir(x,y,1) and not isdir(x,y,2):y+=1;x-=1
        mode=0 if not isdir(x,y,0) and not isdir(x,y,3) else 1
        d=next(d for d in range(4) if not isdir(x,y,d))
        k=d+4*mode;cx=x*8+clut[k*2];cy=y*8+clut[k*2+1];start=(cx,cy)
        flip(x,y,d)
        while True:
            iterations+=1;assert iterations<1000000
            nd=(d+1)&3;dx,dy=dirs[nd];flip(x,y,d);next_walk=0
            if isdir(x,y,nd):
                next_walk=1
                while not isdir(x+dx,y+dy,d):
                    x+=dx;y+=dy;flip(x,y,d)
                    if not isdir(x,y,nd):next_walk=0;break
            k=d+4*next_walk;ex=x*8+nlut[k*2];ey=y*8+nlut[k*2+1]
            f0=(d^2*mode^(~(2*d)&3))&3;f1=(d^2*(next_walk^(d&1))^1)&3
            if d in (0,1):flags=f0|4*(f1|4*(mode|2*next_walk))
            else:flags=f1|4*(f0|4*(next_walk|2*mode))
            coords=[cy,cx,ex] if d==0 else [cx,cy,ey] if d==1 else [cy,ex,cx] if d==2 else [cx,ey,cy]
            out[d].append(coords+[d,flags])
            if not next_walk:d=nd
            else:x+=diag[d][0];y+=diag[d][1];d=(d-1)&3
            cx,cy=ex,ey;mode=next_walk
            if (cx,cy)==start:break
        flip(x,y,d)



def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--static-out", type=Path)
    parser.add_argument("--out", type=Path)
    parser.add_argument("--terrain-report", type=Path)
    parser.add_argument("--campaign-regions-report", type=Path)
    parser.add_argument("--terrain-data", type=Path)
    parser.add_argument("--campaign-data", type=Path)
    parser.add_argument("--map-limit", type=int)
    parser.add_argument("--capacity", type=int, default=5000,
                        help="Explicit allocation-dependent buffer-capacity fixture (5000..16000)")
    parser.add_argument("--execute-original-code", action="store_true")
    args = parser.parse_args()
    if not 5000 <= args.capacity <= 16000:
        parser.error("Capacity fixture must fit the private contour buffers")
    data = args.exe.read_bytes()
    if assets.sha(data) != TARGET_HASH:
        parser.error("Unknown executable hash")
    pe = pefile.PE(data=data)
    base = pe.OPTIONAL_HEADER.ImageBase
    assert base == 0x400000 and pe.FILE_HEADER.Machine == 0x14C
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    witnesses, reviewed = {}, {}
    for name, (lo, hi) in BLOCKS.items():
        rows = list(md.disasm(pe.get_data(lo - base, hi - lo), lo))
        if not rows or rows[-1].address + rows[-1].size != hi:
            parser.error("Invalid reviewed instruction boundary: " + name)
        witnesses[name] = [{"va": hex(i.address), "bytes": i.bytes.hex(), "mnemonic": i.mnemonic,
                            "operands": i.op_str} for i in rows]
        reviewed.update({i.address: i.size for i in rows})
    tables = {name: list(struct.unpack(fmt, pe.get_data(va - base, size)))
              for name, (va, fmt, size) in TABLES.items()}
    if args.static_out:
        args.static_out.parent.mkdir(parents=True, exist_ok=True)
        args.static_out.write_text(json.dumps({"target_sha256": TARGET_HASH, "version": "1.17.0.1",
            "evidence": "static; analytical labels", "witnesses": witnesses, "tables": tables}, indent=2) + "\n")
    if not args.execute_original_code:
        print(json.dumps({"mode": "static only", "blocks": len(BLOCKS)}))
        return
    if args.out is None:
        parser.error("Execution requires --out")
    dependencies = {Path(module.__file__).name: assets.file_sha(Path(module.__file__))
                    for module in (assets, generation, campaign)}
    import unicorn
    from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_ESI,
        UC_X86_REG_EBX, UC_X86_REG_ESP, UC_X86_REG_EBP, UC_X86_REG_EIP)
    image = pe.get_memory_mapped_image()
    image_size = (pe.OPTIONAL_HEADER.SizeOfImage + 4095) & ~4095
    entries = {v[0]: name for name, v in BLOCKS.items()}
    cases = []

    def put(uc, address, value, width=4):
        uc.mem_write(address, (value & ((1 << (width * 8)) - 1)).to_bytes(width, "little"))

    def get(uc, address, width=4):
        return int.from_bytes(uc.mem_read(address, width), "little")

    def execute(uc, start, end, expected_sp, scope):
        count, trace, cache = 0, Counter(), {}
        def check(cpu, va, size, unused):
            nonlocal count
            if va == end:
                cpu.emu_stop()
                return
            key = (va, size)
            if key not in cache:
                cursor, length, names = va, 0, []
                block_end = min(va + size, end) if va < end < va + size else va + size
                while cursor < block_end:
                    width = reviewed.get(cursor)
                    if width is None or cursor + width > block_end:
                        raise RuntimeError("Unreviewed original basic block: " + hex(cursor))
                    if cursor in entries:
                        names.append(entries[cursor])
                    cursor += width
                    length += 1
                cache[key] = length, names
            length, names = cache[key]
            count += length
            trace.update(names)
        hook = uc.hook_add(unicorn.UC_HOOK_BLOCK, check)
        stop_hook = uc.hook_add(unicorn.UC_HOOK_CODE, lambda cpu, va, size, unused: cpu.emu_stop(),
                                begin=end, end=end)
        try:
            uc.emu_start(start, end + 1, count=LIMIT)
            if uc.reg_read(UC_X86_REG_EIP) != end or uc.reg_read(UC_X86_REG_ESP) != expected_sp:
                raise RuntimeError("Native return/fragment stack contract failed")
        finally:
            uc.hook_del(hook)
            uc.hook_del(stop_hook)
        return {"entry": hex(start), "stop": hex(end), "scope": scope, "instructions": count,
                "calls": dict(trace), "returned": uc.reg_read(UC_X86_REG_EAX)}

    def call(uc, address, arguments=(), ecx=0):
        sp = STACK + 0xF000
        uc.mem_write(sp, struct.pack("<" + "I" * (len(arguments) + 1), STOP, *arguments))
        uc.reg_write(UC_X86_REG_ESP, sp)
        uc.reg_write(UC_X86_REG_ECX, ecx)
        # qsort is cdecl, unlike the other stdcall-oriented research batches.
        return execute(uc, address, STOP, sp + 4, "complete original function")

    def run_case(name, walk, inputs):
        width, height = len(walk[0]) // 4, len(walk) // 4
        uc = unicorn.Uc(unicorn.UC_ARCH_X86, unicorn.UC_MODE_32)
        for address, size in ((base, image_size), (CONTEXT, 0x100000), (STACK, 0x10000),
                              (STOP, 0x1000), (WALK, 0x100000)):
            uc.mem_map(address, size)
        uc.mem_write(base, image)
        for address, value, size in ((0x5264B4, width, 2), (0x5264B6, height, 2),
                                    (0x69B55C, WALK, 4), (0x6D319C, 0, 4), (0x50F310, 0, 4)):
            put(uc, address, value, size)
        for y, row in enumerate(walk):
            uc.mem_write(WALK + y * 1024, bytes(row))
        for direction in range(4):
            put(uc, CONTEXT + direction * 4, ARRAYS + direction * 0x20000)
            put(uc, CONTEXT + 0x18 + direction * 2, args.capacity, 2)
        runs = [call(uc, 0x488DF0, ecx=CONTEXT)]
        bp = STACK + 0xE000
        uc.mem_write(bp - 0x500, bytes(0x510))
        put(uc, bp + 4, STOP)
        for offset, value in ((-8, CONTEXT), (-0x20, WALK), (-0x2C, 0), (-0x30, 0)):
            put(uc, bp + offset, value)
        uc.reg_write(UC_X86_REG_EBP, bp)
        uc.reg_write(UC_X86_REG_ESP, bp - 0x44)
        uc.reg_write(UC_X86_REG_EBX, CONTEXT)
        uc.reg_write(UC_X86_REG_ESI, WALK)
        runs.append(execute(uc, 0x48A88E, 0x48ACC8, bp - 0x44,
                            "post-allocation/pre-free contour traversal fragment"))
        expected, expected_walk = contour_reference(walk)
        actual = []
        for direction in range(4):
            n = get(uc, CONTEXT + 0x10 + direction * 2, 2)
            address = get(uc, CONTEXT + direction * 4)
            assert n < args.capacity
            rows = [list(struct.unpack("<3h2B", uc.mem_read(address + i * 8, 8))) for i in range(n)]
            assert rows == expected[direction], (name, direction, "unsorted native contour mismatch")
            runs.append(call(uc, 0x4D4490, arguments=(address, n, 8, 0x48AD40)))
            sorted_rows = [list(struct.unpack("<3h2B", uc.mem_read(address + i * 8, 8))) for i in range(n)]
            assert sorted(map(tuple, rows)) == sorted(map(tuple, sorted_rows))
            keys = [tuple(row[:2]) for row in sorted_rows]
            assert keys == sorted(keys)
            actual.append(sorted_rows)
        actual_walk = [list(uc.mem_read(WALK + y * 1024, width * 4)) for y in range(height * 4)]
        assert actual_walk == expected_walk
        assert all(value & 15 == 0 for row in actual_walk for value in row)
        encoded = b"".join(struct.pack("<3h2B", *row) for group in actual for row in group)
        cases.append({"name": name, "inputs": inputs, "runs": runs, "passed": True,
            "observed": {"counts": list(map(len, actual)), "native_sorted_contours_sha256": assets.sha(encoded),
                         "final_walk_sha256": assets.sha(bytes(v for row in actual_walk for v in row)),
                         "all_direction_bits_consumed": True, "fixture_capacity_per_direction": args.capacity},
            "oracle": "entire unsorted records and walk mutation match separately expressed geometry; native qsort multiset and signed key order"})
        print(json.dumps({"contour_case": name, "counts": list(map(len, actual))}), flush=True)

    rng = random.Random(11788)
    for ordinal in range(30):
        width, height = (8, 8) if ordinal < 10 else (12, 10) if ordinal < 20 else (20, 16)
        if ordinal % 5 == 0:
            ids = [0] * (width * height)
        elif ordinal % 5 == 1:
            ids = [1] * (width * height)
        else:
            ids = [rng.randrange(3) for _ in range(width * height)]
        partial = rng.getrandbits(16)
        records = [[1] * 16, [0] * 16, [partial >> i & 1 for i in range(16)]]
        walk = generation.walk_reference(width, height, ids, records)
        run_case("synthetic_contours_{}".format(ordinal), walk,
                 {"width": width, "height": height, "megatile_ids": ids, "vf4_records": records})

    report_hashes, authentic = {}, 0
    if args.terrain_report or args.campaign_regions_report:
        if any(value is None for value in (args.terrain_report, args.campaign_regions_report, args.terrain_data, args.campaign_data)):
            parser.error("Authentic maps require both reports and terrain/campaign scratch inputs")
        provenance = json.loads(args.terrain_report.read_text())
        graphs = json.loads(args.campaign_regions_report.read_text())
        if graphs["probe_script_sha256"] != dependencies["probe_native_campaign_regions_117.py"]:
            parser.error("Stale original campaign graph source")
        if graphs["terrain_report_sha256"] != assets.file_sha(args.terrain_report):
            parser.error("Stale terrain input report")
        if not graphs["all_audited_maps_included"] or not graphs["all_expected_results_matched"]:
            parser.error("Incomplete original campaign graph evidence")
        known = {case["name"].removeprefix("campaign_graph_"): case for case in graphs["cases"]
                 if case["name"].startswith("campaign_graph_")}
        records_by_set, cv5_by_set = {}, {}
        for tileset in assets.TILESETS:
            for extension, store, fmt in (("vf4", records_by_set, "<16H"), ("cv5", cv5_by_set, "<26H")):
                payload = (args.terrain_data / "tileset" / (tileset + "." + extension)).read_bytes()
                if assets.sha(payload) != provenance["effective"]["tileset\\{}.{}".format(tileset, extension)]["sha256"]:
                    parser.error("Unknown original tileset payload")
                store[tileset] = list(struct.iter_unpack(fmt, payload))
        identities = provenance["campaigns"][:args.map_limit] if args.map_limit else provenance["campaigns"]
        for identity in identities:
            chk = (args.campaign_data / identity["path"].replace("\\", "/")).read_bytes()
            if assets.sha(chk) != identity["chk_sha256"]:
                parser.error("Unknown original CHK")
            tileset = identity["tileset"]
            raw = [x[0] for x in struct.iter_unpack("<H", assets.sections(chk)[b"MTXM"][0])]
            ids = [cv5_by_set[tileset][value >> 4 & 0x7FF][10 + (value & 15)] | (value & 0x8000) for value in raw]
            walk = generation.walk_reference(identity["width"], identity["height"], ids, records_by_set[tileset])
            walk_hash = assets.sha(bytes(v for row in walk for v in row))
            if walk_hash != known[identity["path"]]["observed"]["walk_sha256"]:
                parser.error("Derived walk input differs from complete recorded original producer output")
            run_case("authentic_contours_" + identity["path"], walk,
                     {"chk_sha256": identity["chk_sha256"], "native_producer_walk_sha256": walk_hash,
                      "width": identity["width"], "height": identity["height"], "tileset": tileset})
            authentic += 1
        report_hashes = {"terrain_report_sha256": assets.file_sha(args.terrain_report),
                         "campaign_regions_report_sha256": assets.file_sha(args.campaign_regions_report)}
    complete = sum(run["scope"] == "complete original function" for case in cases for run in case["runs"])
    result = {"target_sha256": TARGET_HASH, "version": "1.17.0.1", "probe_script_sha256": assets.file_sha(Path(__file__)),
        "dependency_hashes": dependencies, **report_hashes, "case_count": len(cases), "total_native_checks": len(cases),
        "all_expected_results_matched": True, "failed_cases": 0, "complete_function_returns": complete,
        "fragment_completions": len(cases), "authentic_contour_maps": authentic,
        "fixture_capacity_per_direction": args.capacity,
        "runtime": {"python": platform.python_version(), "architecture": platform.machine(), "unicorn": unicorn.__version__},
        "maximum_instructions_per_execution": LIMIT, "no_helper_stubs": True, "no_import_execution": True,
        "full_game_or_campaign_executed": False, "cases": cases,
        "limits": "Contour allocation/growth/free/shrink and local path/collision consumers remain separate; core is explicitly a fragment."}
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({"checks": len(cases), "complete": complete, "fragments": len(cases), "authentic_maps": authentic}))


if __name__ == "__main__":
    main()
