#!/usr/bin/env python3
"""Native UMS override probes: complete PUNI and explicit post-init fragments.

No PE imports or substituted native callees. Default emits the execution plan.
Override wrappers import Storm memset/error, so their post-init execution starts
with a synthetic stack frame. This is recorded separately from complete calls.
"""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import struct
import subprocess
import sys

import capstone
import pefile

HASH = "94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0"
PYMS_COMMIT = "bfc5d3aad0b5614a5aff72c223f8efa00afddfa4"
BASE, SCRATCH, PAYLOAD, STACK, STOP = 0x400000, 0x800000, 0x802000, 0x900000, 0xA00000
# tag: complete handler, post-import fragment entry, local size, saved ESI, entity count
HANDLERS = {
    "UNIS": (0x4680D0, 0x468120, 4048, True, 228),
    "UNIx": (0x467F40, 0x467F94, 4168, True, 228),
    "UPGS": (0x468340, 0x468390, 600, False, 46),
    "UPGx": (0x468250, 0x4682A0, 796, False, 61),
    "TECS": (0x468500, 0x468550, 216, False, 24),
    "TECx": (0x468430, 0x468480, 396, False, 44),
    "UPGR": (0x468750, 0x4687A0, 1748, True, 46),
    "PUPx": (0x468660, 0x4686B0, 2320, True, 61),
    "PTEC": (0x468930, 0x468980, 912, True, 24),
    "PTEx": (0x468840, 0x468890, 1672, True, 44),
    "PUNI": (0x4685D0, None, 5700, False, 228),
}
LENGTHS = {"UNIS": 4048, "UNIx": 4168, "UPGS": 598, "UPGx": 794,
           "TECS": 216, "TECx": 396, "UPGR": 1748, "PUPx": 2318,
           "PTEC": 912, "PTEx": 1672, "PUNI": 5700}
ALLOW = [(0x467F40, 0x468A20), (0x447DB0, 0x447DD5), (0x4D2200, 0x4D222B),
         (0x4C12B0, 0x4C12D7), (0x4D16A0, 0x4D16D1), (0x4D19E8, 0x4D19EF),
         (0x49A2C0, 0x49A2F6), (0x49A300, 0x49A33F),
         (0x49A370, 0x49A3B3), (0x49A3C0, 0x49A3FA)]
UPGRADE_ARRAYS = [0x5248D0, 0x524950, 0x5249D0, 0x524A50, 0x524AD0, 0x524B50]
TECH_ARRAYS = [0x51F760, 0x51F7B8, 0x51F810, 0x51F868]
# Array bases are derived from negative-index loop destinations: the first loop
# iteration addresses unit227 using -1/-2/-4 relative to end-of-array constants.
UNIT_ARRAYS = [("health", 0x520708, 4), ("shields", 0x520540, 2),
               ("armor", 0x521D90, 1), ("build_time", 0x5238B0, 2),
               ("mineral_cost", 0x523520, 2), ("gas_cost", 0x5236E8, 2),
               ("name", 0x524458, 2)]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--exe", required=True, type=Path)
    ap.add_argument("--data", type=Path)
    ap.add_argument("--manifest", type=Path)
    ap.add_argument("--pyms", type=Path, help="Pinned PyMS checkout, only used for units.dat association fields")
    ap.add_argument("--out", type=Path)
    ap.add_argument("--execute-original-code", action="store_true")
    args = ap.parse_args()
    exe = args.exe.read_bytes()
    assert hashlib.sha256(exe).hexdigest() == HASH, "Unknown EXE hash"
    if not args.execute_original_code:
        print(json.dumps({"target_sha256": HASH, "stubs": [],
                          "full_function": "PUNI and four native per-player setters",
                          "fragments": "10 override handlers, after imported memset; original reader/setters",
                          "complete_game": False}, indent=2))
        return
    if not args.out:
        ap.error("--out is required with execution")
    if args.data and not (args.manifest and args.pyms):
        ap.error("--data requires --manifest and --pyms")
    import unicorn
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EBP, UC_X86_REG_ESP, UC_X86_REG_ECX, UC_X86_REG_EDX
    pe = pefile.PE(data=exe)
    image = pe.get_memory_mapped_image()
    size = (pe.OPTIONAL_HEADER.SizeOfImage + 4095) & ~4095
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    cases, coverage = [], []
    associations = [(228, 130, 130)] * 228
    dat_hash = None
    if args.pyms:
        assert subprocess.check_output(["git", "-C", str(args.pyms), "rev-parse", "HEAD"], text=True).strip() == PYMS_COMMIT
        sys.path.insert(0, str(args.pyms))
        from PyMS.FileFormats.DAT.UnitsDAT import UnitsDAT
        dat = (args.data / "arr/units.dat").read_bytes()
        dat_hash = hashlib.sha256(dat).hexdigest()
        units = UnitsDAT()
        units.load_data(dat)
        associations = [(u.subunit1, u.ground_weapon, u.air_weapon) for u in units.entries]
        assert len(associations) == 228

    def w32(uc, a, v):
        uc.mem_write(a, struct.pack("<I", v & 0xFFFFFFFF))

    def new():
        uc = Uc(UC_ARCH_X86, UC_MODE_32)
        uc.mem_map(BASE, size)
        uc.mem_write(BASE, image)
        uc.mem_map(SCRATCH, 0x20000)
        uc.mem_map(STACK, 0x20000)
        uc.mem_map(STOP, 4096)
        w32(uc, 0x6D319C, 2)
        uc.mem_write(0x536720, b"\1")
        uc.mem_write(0x6728D0 + 0x75, b"\0")
        uc.mem_write(0x6728D0 + 0x79, b"\0")
        uc.mem_write(0x6728D0 + 0x7F, b"\0")
        uc.mem_write(0x51FB90, struct.pack("<228H", *(x[0] for x in associations)))
        uc.mem_write(0x5211D8, bytes(x[1] for x in associations))
        uc.mem_write(0x5213A8, bytes(x[2] for x in associations))
        return uc

    def run(uc, start):
        count, ended = 0, False

        def instruction(machine, address, n, _):
            nonlocal count, ended
            if address == STOP:
                ended = True
                machine.emu_stop()
                return
            if not any(lo <= address and address + n <= hi for lo, hi in ALLOW):
                raise RuntimeError(f"Unapproved instruction {address:#x}")
            count += 1
        hook = uc.hook_add(UC_HOOK_CODE, instruction)
        try:
            uc.emu_start(start, STOP + 1, count=200000)
        finally:
            uc.hook_del(hook)
        assert ended, "Instruction limit"
        return uc.reg_read(UC_X86_REG_EAX), count

    def call(uc, fn, ecx=0, edx=0, argv=()):
        sp = STACK + 0x18000
        uc.mem_write(sp, struct.pack("<" + "I" * (1 + len(argv)), STOP, *argv))
        uc.reg_write(UC_X86_REG_ESP, sp)
        uc.reg_write(UC_X86_REG_ECX, ecx)
        uc.reg_write(UC_X86_REG_EDX, edx)
        return run(uc, fn)

    def execute_section(uc, tag, raw):
        uc.mem_write(PAYLOAD, raw)
        uc.mem_write(SCRATCH, struct.pack("<4I", PAYLOAD + len(raw), PAYLOAD, 0, len(raw)))
        fn, fragment, local_size, saved_esi, _ = HANDLERS[tag]
        if fragment is None:
            returned, count = call(uc, fn, argv=(SCRATCH, len(raw), SCRATCH + 32))
        else:
            frame = STACK + 0x18000
            uc.mem_write(frame - local_size - 8, b"\0" * (local_size + 8))
            uc.mem_write(frame, struct.pack("<5I", 0, STOP, SCRATCH, len(raw), SCRATCH + 32))
            uc.reg_write(UC_X86_REG_EBP, frame)
            uc.reg_write(UC_X86_REG_ESP, frame - local_size - (4 if saved_esi else 0))
            returned, count = run(uc, fragment)
        return returned, count

    def player_address(family, player, entity, which):
        if family == "upgrade":
            if entity < 46:
                return (0x534368 if which == 0 else 0x534590) + player * 46 + entity
            return (0x53652A if which == 0 else 0x5365DE) + player * 15 + entity
        if entity < 24:
            return (0x534104 if which == 0 else 0x534224) + player * 24 + entity
        return (0x536318 if which == 0 else 0x536408) + player * 20 + entity

    def expected_section(uc, tag, raw):
        """Expected native output from decoded CHK fields, keeping untouched guards."""
        changes = {}
        def put(a, raw_value):
            for i, v in enumerate(raw_value):
                changes[a + i] = v
        if tag == "PUNI":
            for p in range(12):
                for u in range(228):
                    i = p * 228 + u
                    v = raw[12 * 228 + u] if raw[13 * 228 + i] else raw[i]
                    put(0x52655C + i, bytes([v]))
        elif tag in ("UPGR", "PUPx", "PTEC", "PTEx"):
            n = HANDLERS[tag][4]
            family = "upgrade" if tag in ("UPGR", "PUPx") else "tech"
            for p in range(12):
                for u in range(n):
                    i = p * n + u
                    default = raw[26 * n + i]
                    for field in (0, 1):
                        v = raw[(24 + field) * n + u] if default else raw[field * 12 * n + i]
                        put(player_address(family, p, u, field), bytes([v]))
        elif tag in ("UPGS", "UPGx", "TECS", "TECx"):
            n = HANDLERS[tag][4]
            pad = int(tag == "UPGx")
            dests = UPGRADE_ARRAYS if tag.startswith("UPG") else TECH_ARRAYS
            for u in range(n):
                if not raw[u]:
                    for field, dest in enumerate(dests):
                        offset = n + pad + (field * n + u) * 2
                        put(dest + u * 2, raw[offset:offset + 2])
        else:
            offset = 228
            fields = []
            for _, dest, width in UNIT_ARRAYS:
                fields.append((offset, dest, width))
                offset += 228 * width
            weapons = 100 if tag == "UNIS" else 130
            for u in range(227, -1, -1):
                if raw[u]:
                    continue
                for offset_field, dest, width in fields:
                    put(dest + u * width, raw[offset_field + u * width:offset_field + (u + 1) * width])
                sub = associations[u][0]
                actual = u if sub == 228 else sub
                for weapon in associations[actual][1:]:
                    if weapon == 130:
                        continue
                    # Base UNIS cannot express expansion weapons. Record such
                    # unsupported associations rather than inventing output.
                    assert weapon < weapons, (tag, u, weapon)
                    for field, dest in enumerate((0x525C80, 0x525D88)):
                        at = offset + (field * weapons + weapon) * 2
                        put(dest + weapon * 2, raw[at:at + 2])
        return changes

    def check_section(tag, raw, kind, meta):
        uc = new()
        expected = expected_section(uc, tag, raw)
        # Poison only the affected arrays; the full expected region comparison
        # checks that defaulted entries do not accidentally overwrite neighbors.
        if tag == "PUNI":
            regions = [(0x52655C, 2736)]
        elif tag in ("UPGR", "PUPx"):
            regions = [(0x534368, 552), (0x534590, 552), (0x536558, 180), (0x53660C, 180)]
        elif tag in ("PTEC", "PTEx"):
            regions = [(0x534104, 288), (0x534224, 288), (0x536330, 240), (0x536420, 240)]
        elif tag in ("UPGS", "UPGx"):
            regions = [(a, 122) for a in UPGRADE_ARRAYS]
        elif tag in ("TECS", "TECx"):
            regions = [(a, 88) for a in TECH_ARRAYS]
        else:
            regions = [(a, 228 * w) for _, a, w in UNIT_ARRAYS] + [(0x525C80, 260), (0x525D88, 260)]
        for a, n in regions:
            uc.mem_write(a, b"\xaa" * n)
        returned, count = execute_section(uc, tag, raw)
        assert returned == 1, (tag, returned)
        outputs = []
        for a, n in regions:
            wanted = bytes(expected.get(a + i, 0xAA) for i in range(n))
            observed = bytes(uc.mem_read(a, n))
            assert observed == wanted, (tag, hex(a), next((i for i,(x,y) in enumerate(zip(observed,wanted)) if x != y), None))
            outputs.append({"va": hex(a), "bytes": n, "sha256": hashlib.sha256(observed).hexdigest()})
        layout_differences = []
        if tag in ("UPGR", "PUPx", "PTEC", "PTEx"):
            n = HANDLERS[tag][4]
            for p in range(12):
                for u in range(n):
                    if raw[26*n+p*n+u]:
                        continue
                    for field in (0,1):
                        native = raw[field*12*n+p*n+u]
                        interleaved = raw[(2*p+field)*n+u]
                        if native != interleaved:
                            layout_differences.append({"player":p,"entity":u,"field":field,
                                                       "native_planar":native,"interleaved":interleaved})
        return {"kind": kind, "tag": tag, "handler_va": hex(HANDLERS[tag][0]),
                "entry_va": hex(HANDLERS[tag][1] or HANDLERS[tag][0]),
                "execution_scope": "post_import_initialization_fragment" if HANDLERS[tag][1] else "complete_function",
                "input_sha256": hashlib.sha256(raw).hexdigest(), "output_regions": outputs,
                "expected_modified_bytes": len(expected), "return": returned, "instructions": count,
                "different_cells_if_interleaved":len(layout_differences),
                "interleaved_difference_examples":layout_differences[:4], **meta}

    # PUNI complete-handler gates and native boolean-default selection, preserving
    # arbitrary u8 availability values instead of normalizing them to true/false.
    for flag in (0, 1, 2, 255):
        raw = bytearray(5700)
        raw[:2736] = bytes(i % 251 for i in range(2736))
        raw[2736:2964] = bytes((i + 73) % 251 for i in range(228))
        raw[2964:] = bytes([flag]) * 2736
        cases.append(check_section("PUNI", bytes(raw), "PUNI_defaults", {"default_flag": flag}))
    for supplied in (5699, 5700, 5701):
        uc = new()
        raw = b"\1" * supplied
        returned, count = execute_section(uc, "PUNI", raw)
        assert returned == int(supplied == 5700)
        cases.append({"kind": "PUNI_exact_length", "bytes": supplied, "return": returned,
                      "execution_scope": "complete_function", "instructions": count})
    uc = new()
    uc.mem_write(0x6728D0 + 0x75, b"\1")
    returned, count = execute_section(uc, "PUNI", b"\1" * 5700)
    assert returned == 0
    cases.append({"kind": "PUNI_non_UMS_gate", "return": returned,
                  "execution_scope": "complete_function", "instructions": count})
    for tag in HANDLERS:
        if tag == "PUNI":
            continue
        for flag in (0, 1, 2):
            raw = bytearray((i * 37 + 11) % 256 for i in range(LENGTHS[tag]))
            n = HANDLERS[tag][4]
            if tag in ("UPGR", "PUPx", "PTEC", "PTEx"):
                raw[26 * n:] = bytes([flag]) * (12 * n)
            else:
                raw[:n] = bytes([flag]) * n
                if tag in ("UNIS", "UNIx"):
                    # Fixtures select only unit 0, avoiding invalid base-section
                    # expansion weapon references. Actual maps use real flags.
                    raw[:228] = b"\1" * 228
                    raw[0] = flag
            cases.append(check_section(tag, bytes(raw), "override_defaults_and_planar_layout", {"default_flag": flag}))
    for tag in ("UNIS","UNIx"):
        for unit in (3,4,227):
            raw=bytearray((i*37+11)%256 for i in range(LENGTHS[tag]))
            raw[:228]=b"\1"*228
            raw[unit]=0
            cases.append(check_section(tag,bytes(raw),"unit_subunit_and_weapon_selection",{"custom_unit":unit,
                          "subunit":associations[unit][0],"associated_weapons":list(associations[unit][1:])}))
    # Complete setters establish field identity and expansion gates separately.
    for family, fn, field, ids in (("upgrade",0x49A370,1,(0,45,46,60)),
                                   ("upgrade",0x49A3C0,0,(0,45,46,60)),
                                   ("tech",0x49A2C0,0,(0,23,24,43)),
                                   ("tech",0x49A300,1,(0,23,24,43))):
        for bw in (0,1):
            for entity in ids:
                uc = new()
                uc.mem_write(0x536720, bytes([bw]))
                address = player_address(family, 11, entity, field)
                uc.mem_write(address, b"\xaa")
                _, count = call(uc, fn, 11, entity, (255,))
                gated = (family == "upgrade" and field == 1 and entity >= 46) or (family == "tech" and field == 1 and entity >= 24)
                wanted = 0xAA if gated and not bw else 255
                observed = uc.mem_read(address,1)[0]
                assert observed == wanted
                cases.append({"kind":"native_setter_expansion_gate", "family":family,
                              "field":field, "handler_va":hex(fn), "player":11,
                              "entity":entity,"expansion":bw,"value":255,
                              "stored":observed,"execution_scope":"complete_function","instructions":count})
    manifest_hash = None
    if args.data:
        manifest_raw = args.manifest.read_bytes()
        manifest_hash = hashlib.sha256(manifest_raw).hexdigest()
        manifest = json.loads(manifest_raw)
        effective = manifest["effective"]
        dat_record = next(r for r in effective if r["path"].lower() == "arr\\units.dat") if isinstance(effective,list) else effective["arr\\units.dat"]
        assert dat_record["sha256"] == dat_hash
        for row in manifest["campaign_found"]:
            raw = (args.data / row["path"].replace("\\","/")).read_bytes()
            assert hashlib.sha256(raw).hexdigest() == row["sha256"]
            pos, sections = 0, []
            while pos < len(raw):
                tag_b,n = struct.unpack_from("<4sI",raw,pos);pos += 8
                tag = tag_b.decode("ascii")
                part=raw[pos:pos+n];pos += n
                assert len(part)==n
                if tag in HANDLERS:
                    sections.append(check_section(tag,part,"campaign_override_section",{}))
            coverage.append({"map_path":row["path"],"map_sha256":row["sha256"],
                             "chk_version":row["chk_version"],"sections":sections,
                             "limits":"Independent complete PUNI or explicit post-init fragments; no full map load"})
    def witness(lo,hi):
        off=pe.get_offset_from_rva(lo-BASE)
        return [{"va":hex(i.address),"rva":hex(i.address-BASE),"file_offset":hex(pe.get_offset_from_rva(i.address-BASE)),
                 "bytes":i.bytes.hex(),"mnemonic":i.mnemonic,"operands":i.op_str}
                for i in md.disasm(exe[off:off+hi-lo],lo)]
    native_sections=sum(len(r["sections"]) for r in coverage)
    flat_checks=cases+[s for r in coverage for s in r["sections"]]
    result={"target_sha256":HASH,"probe_script_sha256":hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
            "runtime_architecture":platform.machine(),"python_version":platform.python_version(),
            "unicorn_version":unicorn.__version__,"original_code_executed":True,"complete_game_executed":False,
            "stubs":[],"synthetic_checks":len(cases),"campaign_section_checks":native_sections,
            "total_native_checks":len(cases)+native_sections,"all_expected_results_matched":True,
            "complete_function_checks":sum(r["execution_scope"]=="complete_function" for r in flat_checks),
            "post_import_fragment_checks":sum(r["execution_scope"]=="post_import_initialization_fragment" for r in flat_checks),
            "units_dat_sha256":dat_hash,"units_association_schema_source_commit":PYMS_COMMIT,
            "manifest_sha256":manifest_hash,"cases":cases,"campaign_input_coverage":coverage,
            "handler_contracts":{tag:{"function_va":hex(v[0]),"fragment_entry_va":hex(v[1]) if v[1] else None,
                                     "local_frame_bytes":v[2],"saved_esi":v[3],"entities":v[4],"section_bytes":LENGTHS[tag]}
                                 for tag,v in HANDLERS.items()},
            "witnesses":{"override_wrappers_and_bodies":witness(0x467F40,0x468A20),
                         "availability_setters":witness(0x49A2C0,0x49A3FA),
                         "stack_helper":witness(0x4D2200,0x4D222B)},
            "limits":["Only PUNI and availability setters execute complete functions.",
                      "Ten wrappers require Storm initialization/error imports; post-import fragments use synthetic zeroed stack frames.",
                      "The original bounded reader, native EXE memmove and availability setters execute without stubs.",
                      "Output arrays begin with synthetic guard values; units.dat association fields seed only the three required native arrays.",
                      "No native DAT loader, complete CHK load, unit creation or gameplay executed."]}
    args.out.parent.mkdir(parents=True,exist_ok=True)
    args.out.write_text(json.dumps(result,indent=2)+"\n")
    print(json.dumps({k:result[k] for k in ("synthetic_checks","campaign_section_checks","total_native_checks","all_expected_results_matched")}))


if __name__ == "__main__":
    main()
