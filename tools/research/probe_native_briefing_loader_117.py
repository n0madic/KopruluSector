#!/usr/bin/env python3
"""Hash-gated native briefing and CHK-loader probes in private Unicorn memory.

Default mode emits a plan. Executing requires --execute-original-code. No game
process, DLL imports, Windows APIs or host callbacks are executed by the PE.
"""

import argparse
import hashlib
import json
from pathlib import Path
import platform
import struct

import capstone
import pefile


TARGET_HASH = "94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0"
SCRATCH = 0x800000
PAYLOAD = 0x802000
OUTPUT = 0x804000
LIST = 0x806000
STACK = 0x900000
STOP = 0xA00000
ALLOW = [
    (0x44FCD0, 0x44FD06), (0x44FEE0, 0x44FF5D),
    (0x4B8E70, 0x4B8E76), (0x44FEC0, 0x44FEDE),
    (0x468A20, 0x468B55), (0x468D50, 0x468D7B),
    (0x468D80, 0x468DDB), (0x469130, 0x46916B),
    (0x4692F0, 0x469314), (0x469320, 0x469355),
    (0x4BD550, 0x4BD556), (0x4B6A50, 0x4B6A56),
    (0x4C12B0, 0x4C12D7), (0x4C1020, 0x4C1069),
    (0x4C12E0, 0x4C134C),
    (0x4D16A0, 0x4D16D1), (0x4D19E8, 0x4D19EF),
    (0x468E80, 0x468EA8), (0x468DE0, 0x468E00), (0x468E35, 0x468E3D),
    (0x4B6A70, 0x4B6A85), (0x4D0C60, 0x4D0C88), (0x4D0CE5, 0x4D0CF0),
    (0x44FF60, 0x44FF68), (0x4611C0, 0x4611D6), (0x4C58F0, 0x4C595B),
    (0x4C5A42, 0x4C5A65),
]
PLAN = {
    "target_sha256": TARGET_HASH,
    "engine": "Unicorn x86-32, private buffers, per-instruction allowlist",
    "functions": ["MBRF interpreter and blocked/completed Wait paths",
                  "VER/TYPE/SIDE/OWNR/ERA/DIM/SPRP/FORC/MRGN/UPRP loaders",
                  "required-section presence and duplicate-section dispatch",
                  "TRIG/MBRF malformed length rejection",
                  "UPRP reset and briefing wait-clear callback with empty event queues",
                  "UI timer expiry decision fragment, before any host tick API or callback"],
    "original_callees": ["bounded section reader", "original EXE memmove rep-movsb path",
                         "location/property pointer getters", "briefing Wait/None",
                         "original EXE memset rep-stosb path", "briefing wait clear/event cancel"],
    "stubs": [],
    "limits": "Synthetic section/node inputs; allocation, UI callbacks and full CHK/game initialization excluded",
}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--exe", type=Path, required=True)
    p.add_argument("--out", type=Path)
    p.add_argument("--data", type=Path, help="Optional scratch CHK extraction for campaign input coverage")
    p.add_argument("--manifest", type=Path, help="Archive manifest required with --data")
    p.add_argument("--execute-original-code", action="store_true")
    args = p.parse_args()
    data = args.exe.read_bytes()
    if hashlib.sha256(data).hexdigest() != TARGET_HASH:
        p.error("Unknown executable hash")
    if not args.execute_original_code:
        print(json.dumps(PLAN, indent=2))
        return
    if args.out is None:
        p.error("--out required for execution evidence")
    if args.data is not None and args.manifest is None:
        p.error("--manifest is required with --data")

    import unicorn
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
    from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_EBX, UC_X86_REG_ECX,
                                  UC_X86_REG_EDX, UC_X86_REG_ESI, UC_X86_REG_ESP)

    pe = pefile.PE(data=data)
    base = pe.OPTIONAL_HEADER.ImageBase
    image = pe.get_memory_mapped_image()
    size = (pe.OPTIONAL_HEADER.SizeOfImage + 4095) & ~4095
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    cases = []

    def write32(uc, addr, value):
        uc.mem_write(addr, struct.pack("<I", value & 0xFFFFFFFF))

    def read32(uc, addr):
        return struct.unpack("<I", uc.mem_read(addr, 4))[0]

    def new_machine():
        uc = Uc(UC_ARCH_X86, UC_MODE_32)
        uc.mem_map(base, size)
        uc.mem_write(base, image)
        uc.mem_map(SCRATCH, 0x10000)
        uc.mem_map(STACK, 0x4000)
        uc.mem_map(STOP, 4096)
        # Select the real native memmove implementation's ordinary byte-copy path.
        write32(uc, 0x6D319C, 2)
        return uc

    def call(uc, start, ecx=0, edx=0, arguments=()):
        visited = []
        stopped = False

        def instruction(machine, address, ins_size, _):
            nonlocal stopped
            if address == STOP:
                stopped = True
                machine.emu_stop()
                return
            if not any(lo <= address and address + ins_size <= hi for lo, hi in ALLOW):
                raise RuntimeError(f"Instruction outside approved blocks: {address:#x}")
            visited.append(address)

        hook = uc.hook_add(UC_HOOK_CODE, instruction)
        sp = STACK + 0x2000
        uc.mem_write(sp, struct.pack("<" + "I" * (len(arguments) + 1), STOP, *arguments))
        uc.reg_write(UC_X86_REG_ESP, sp)
        uc.reg_write(UC_X86_REG_ECX, ecx)
        uc.reg_write(UC_X86_REG_EDX, edx)
        try:
            uc.emu_start(start, STOP + 1, count=100000)
        finally:
            uc.hook_del(hook)
        if not stopped:
            raise RuntimeError("Instruction limit without approved return")
        return uc.reg_read(UC_X86_REG_EAX), len(visited)

    def section(uc, raw, advertised=None, readable=None):
        advertised = len(raw) if advertised is None else advertised
        readable = len(raw) if readable is None else readable
        uc.mem_write(PAYLOAD, raw)
        uc.mem_write(SCRATCH, struct.pack("<4I", PAYLOAD + readable, PAYLOAD, 0, advertised))
        return SCRATCH

    def timer_fragment(uc, now):
        """Execute only the original expiry decision, bypassing no callee with a stub.

        ESI/EBX are synthetic entry registers, as if the caller had already read
        its node and clock. Both terminal PCs are before subsequent instructions.
        """
        visited = []
        terminal = None

        def instruction(machine, address, ins_size, _):
            nonlocal terminal
            if address in (0x4C5A65, 0x4C5AF6):
                terminal = address
                machine.emu_stop()
                return
            if not any(lo <= address and address + ins_size <= hi for lo, hi in ALLOW):
                raise RuntimeError(f"Instruction outside approved blocks: {address:#x}")
            visited.append(address)

        hook = uc.hook_add(UC_HOOK_CODE, instruction)
        uc.reg_write(UC_X86_REG_ESI, SCRATCH)
        uc.reg_write(UC_X86_REG_EBX, now)
        try:
            uc.emu_start(0x4C5A42, STOP, count=100)
        finally:
            uc.hook_del(hook)
        if terminal is None:
            raise RuntimeError("Timer fragment did not reach a terminal PC")
        return terminal, len(visited)

    # The complete briefing interpreter, using only native None/Wait/skip handlers.
    for condition, condition_flags, execution_flags, active, wait_flag, ignored, expected in [
        (0, 0, 0, 0, 0, False, 1), (22, 0, 0, 0, 0, False, 1),
        (13, 0, 0, 1, 0, False, 0), (13, 0, 0, 1, 1, False, 0),
        (13, 0, 0, 0, 1, False, 1), (13, 2, 0, 0, 1, False, 1),
        (13, 0, 8, 0, 1, False, 1), (13, 0, 0, 1, 0, True, 1),
    ]:
        uc = new_machine()
        uc.mem_write(SCRATCH + 15, bytes([condition]))
        uc.mem_write(SCRATCH + 17, bytes([condition_flags]))
        write32(uc, SCRATCH + 0x940, execution_flags)
        uc.mem_write(0x667BAC, bytes([active]))
        uc.mem_write(SCRATCH + 320 + 26, b"\1")
        uc.mem_write(SCRATCH + 320 + 28, bytes([wait_flag | (2 if ignored else 0)]))
        returned, count = call(uc, 0x44FEE0, SCRATCH)
        observed = {"return": returned, "execution_flags": read32(uc, SCRATCH + 0x940),
                    "action_index": uc.mem_read(SCRATCH + 0x95F, 1)[0],
                    "wait_entry_flags": uc.mem_read(SCRATCH + 320 + 28, 1)[0]}
        assert returned == expected, observed
        if condition == 13:
            assert observed["action_index"] == 0
            assert observed["execution_flags"] == (execution_flags if returned else execution_flags | 1)
        cases.append({"kind": "briefing_vm", "input": [condition, condition_flags, execution_flags,
                      active, wait_flag, ignored], "observed": observed, "instructions": count})
    for action_index in (0, 63, 64):
        uc = new_machine()
        uc.mem_write(SCRATCH + 15, bytes([13]))
        uc.mem_write(SCRATCH + 0x95F, bytes([action_index]))
        if action_index < 64:
            # Tutorial skip action returns directly in the selected multiplayer case.
            uc.mem_write(SCRATCH + 320 + 32 * action_index + 26, bytes([9]))
            uc.mem_write(0x672664, b"\1")
        returned, count = call(uc, 0x44FEE0, SCRATCH)
        observed = [returned, read32(uc, SCRATCH + 0x940), uc.mem_read(SCRATCH + 0x95F, 1)[0]]
        assert observed == [1, 0, 0], observed
        cases.append({"kind": "briefing_action_limit", "initial_index": action_index,
                      "observed": observed, "instructions": count})

    for active in (0, 1):
        uc = new_machine()
        uc.mem_write(0x667BAC, bytes([active]))
        write32(uc, 0x6CF798, 0)
        write32(uc, 0x6CF79C, 0)
        _, count = call(uc, 0x4611C0, OUTPUT, 11)
        assert uc.mem_read(0x667BAC, 1) == b"\0"
        assert read32(uc, 0x6CF7A0) == OUTPUT
        assert bytes(uc.mem_read(0x6CF7A4, 2)) == struct.pack("<H", 11)
        cases.append({"kind": "briefing_wait_callback_empty_queues", "initial_wait_active": active,
                      "wait_active": 0, "cancel_owner": hex(OUTPUT), "cancel_event": 11,
                      "instructions": count,
                      "limits": "Empty event queues; no allocation/free or UI calls"})
    timer_inputs = [(1000, 100, 1100 + late, prior)
                    for late in (-1, 0, 1, 31, 32, 499, 500) for prior in (0, 32)]
    timer_inputs += [(0xFFFFFFF0, 32, 0x10, 0), (0xFFFFFFF0, 32, 0x11, 0)]
    for started, duration, now, prior in timer_inputs:
        uc = new_machine()
        write32(uc, SCRATCH + 12, started)
        write32(uc, SCRATCH + 16, duration)
        write32(uc, SCRATCH + 24, prior)
        terminal, count = timer_fragment(uc, now)
        late_unsigned = (now - started - duration) & 0xFFFFFFFF
        late = late_unsigned if late_unsigned < 0x80000000 else late_unsigned - 0x100000000
        expired = late > 0
        correction = prior + ((late - prior) >> 5) if 0 < late < 500 else prior
        assert terminal == (0x4C5A65 if expired else 0x4C5AF6)
        assert read32(uc, SCRATCH + 24) == correction & 0xFFFFFFFF
        cases.append({"kind": "briefing_ui_timer_expiry_fragment", "start": started,
                      "duration": duration, "now": now, "initial_correction": prior,
                      "signed_lateness": late, "expired": expired,
                      "correction": correction, "terminal_va": hex(terminal),
                      "instructions": count,
                      "limits": "Decision fragment only; no GetTickCount, timer creation, callbacks or cleanup"})

    uc = new_machine()
    uc.mem_write(0x6C46FC, b"\xaa" * (1280 + 8))
    _, count = call(uc, 0x4B6A70)
    assert bytes(uc.mem_read(0x6C46FC, 1280 + 8)) == b"\xaa" * 4 + b"\0" * 1280 + b"\xaa" * 4
    cases.append({"kind": "UPRP_native_reset", "buffer_va": "0x6c4700", "bytes": 1280,
                  "all_zero": True, "adjacent_guards_preserved": True, "instructions": count})

    for fn, required, kind in ((0x468A30, 2, "VER"), (0x468A60, 12, "SIDE"),
                               (0x468AB0, 12, "OWNR"), (0x469130, 4, "DIM"),
                               (0x4692F0, 4, "SPRP"), (0x468D50, 1280, "MRGN64"),
                               (0x468D80, 5100, "MRGN255"), (0x468DB0, 1280, "UPRP")):
        for supplied in (required - 1, required, required + 1):
            uc = new_machine()
            raw = bytes(i % 251 for i in range(supplied))
            sec = section(uc, raw)
            returned, count = call(uc, fn, arguments=(sec, supplied, OUTPUT))
            assert returned == int(supplied == required), (kind, supplied, returned)
            if returned:
                if kind == "VER":
                    assert bytes(uc.mem_read(OUTPUT + 24, 2)) == raw
                elif kind in ("SIDE", "OWNR"):
                    address = 0x673D51 if kind == "SIDE" else 0x673D50
                    assert bytes(uc.mem_read(address + 36 * i, 1)[0] for i in range(12)) == raw
                elif kind == "DIM":
                    assert bytes(uc.mem_read(0x5264B4, 4)) == raw
                elif kind == "SPRP":
                    assert bytes(uc.mem_read(OUTPUT, 4)) == raw
                elif kind.startswith("MRGN"):
                    assert bytes(uc.mem_read(0x534F40, required)) == raw
                elif kind == "UPRP":
                    assert bytes(uc.mem_read(0x6C4700, required)) == raw
            cases.append({"kind": "fixed_section_length_and_copy", "section": kind,
                          "size": supplied, "return": returned, "instructions": count})
    for era in (0, 4, 5, 7, 8, 65535):
        for bw in (0, 1):
            uc = new_machine()
            uc.mem_write(0x536720, bytes([bw]))
            sec = section(uc, struct.pack("<H", era))
            returned, count = call(uc, 0x468B00, arguments=(sec, 2, OUTPUT))
            observed = struct.unpack("<H", uc.mem_read(0x5264BC, 2))[0]
            accepted = bw or era <= 4
            assert returned == int(bool(accepted))
            assert observed == (era & 7 if accepted else era)
            cases.append({"kind": "ERA_mode_and_normalization", "era": era, "expansion": bw,
                          "return": returned, "stored": observed, "instructions": count})
    for supplied in (0, 1, 19, 20, 21):
        uc = new_machine()
        raw = bytes(range(supplied))
        sec = section(uc, raw)
        uc.mem_write(OUTPUT, b"\xaa" * 32)
        returned, count = call(uc, 0x469320, arguments=(sec, supplied, OUTPUT))
        expected = b"\xaa" * 4 + raw + b"\0" * (20 - supplied) + b"\xaa" * 8 if supplied <= 20 else b"\xaa" * 32
        assert bytes(uc.mem_read(OUTPUT, 32)) == expected and returned == int(supplied <= 20)
        cases.append({"kind": "FORC_zero_default_tail", "size": supplied,
                      "return": returned, "instructions": count})
    for supplied, readable in ((4, 3), (4, 4), (4, 5)):
        uc = new_machine()
        sec = section(uc, b"ABCDX", advertised=supplied, readable=readable)
        returned, count = call(uc, 0x4C12B0, sec, OUTPUT)
        assert returned == int(supplied <= readable)
        if returned:
            assert bytes(uc.mem_read(OUTPUT, 4)) == b"ABCD"
        cases.append({"kind": "bounded_section_copy", "size": supplied,
                      "readable": readable, "return": returned, "instructions": count})
    for function, kind in ((0x468DE0, "TRIG"), (0x468E80, "MBRF")):
        for length in (1, 2399, 2401, 4799):
            uc = new_machine()
            returned, count = call(uc, function, arguments=(SCRATCH, length, OUTPUT))
            assert returned == 0
            cases.append({"kind": "trigger_section_invalid_length", "section": kind,
                          "length": length, "return": returned, "instructions": count})

    # Actual list traversal verifies optional/required presence and all duplicate
    # matches. Nodes/payloads are fixtures, callback is the real native DIM handler.
    for required, node_tags, expected in ((0, (), 1), (1, (), 0),
                                           (1, (b"XXXX",), 0), (1, (b"DIM ",), 1)):
        uc = new_machine()
        tag = struct.unpack("<I", b"DIM ")[0]
        uc.mem_write(OUTPUT, struct.pack("<III", tag, 0x469130, required))
        write32(uc, LIST + 8, SCRATCH if node_tags else 0)
        if node_tags:
            write32(uc, SCRATCH + 0x10, struct.unpack("<I", node_tags[0])[0])
        returned, count = call(uc, 0x4C1020, LIST, OUTPUT, (1,))
        assert returned == expected
        cases.append({"kind": "required_section_presence", "required": required,
                      "present_tags": [x.decode("ascii") for x in node_tags],
                      "return": returned, "instructions": count})
    for lengths in ((4, 4), (3, 4), (4, 3)):
        uc = new_machine()
        tag = struct.unpack("<I", b"DIM ")[0]
        uc.mem_write(OUTPUT, struct.pack("<III", tag, 0x469130, 1))
        write32(uc, LIST + 8, SCRATCH)
        for i, n in enumerate(lengths):
            node = SCRATCH + 0x100 * i
            payload = PAYLOAD + 0x100 * i
            uc.mem_write(payload, struct.pack("<HH", 64 + 64 * i, 96 + 32 * i))
            uc.mem_write(node, struct.pack("<6I", 0, node + 0x100 if i == 0 else 0,
                                         payload + n, payload, tag, n))
        returned, count = call(uc, 0x4C12E0, LIST, OUTPUT, (1, OUTPUT + 0x100))
        observed = list(struct.unpack("<HH", uc.mem_read(0x5264B4, 4)))
        assert returned == int(lengths == (4, 4))
        expected_dims = [128, 128] if lengths == (4, 4) else [64, 96] if lengths[0] == 4 else None
        if expected_dims:
            assert observed == expected_dims
        cases.append({"kind": "duplicate_section_dispatch", "sizes": lengths,
                      "return": returned, "stored_dimensions": observed, "instructions": count})

    def witness(lo, hi):
        off = pe.get_offset_from_rva(lo - base)
        return [{"va": hex(i.address), "rva": hex(i.address - base),
                 "file_offset": hex(pe.get_offset_from_rva(i.address - base)),
                 "bytes": i.bytes.hex(), "mnemonic": i.mnemonic, "operands": i.op_str}
                for i in md.disasm(data[off:off + hi - lo], lo)]

    descriptors = []
    for va in (0x4EEB80, 0x4EEBA8, 0x4EEBD0):
        off = pe.get_offset_from_rva(va - base)
        fields = struct.unpack_from("<10I", data, off)
        stage_tables = []
        for stage in range(4):
            table_va, count = fields[1 + 2 * stage:3 + 2 * stage]
            rows = []
            for i in range(count):
                o = pe.get_offset_from_rva(table_va + i * 12 - base)
                tag, handler, required = struct.unpack_from("<4sII", data, o)
                rows.append({"tag": tag.decode("ascii"), "handler_va": hex(handler),
                             "required": bool(required)})
            stage_tables.append({"stage": stage, "table_va": hex(table_va), "entries": rows})
        descriptors.append({"va": hex(va), "version": fields[0],
                            "expansion_required": bool(fields[9]), "stage_tables": stage_tables})
    campaign_coverage = []
    manifest_hash = None
    if args.data is not None:
        manifest_bytes = args.manifest.read_bytes()
        manifest_hash = hashlib.sha256(manifest_bytes).hexdigest()
        manifest = json.loads(manifest_bytes)
        catalog_paths = {r["path"].rstrip("\\") + "\\staredit\\scenario.chk"
                         for r in manifest["map_catalog"] if r["path"] and r["index"]}
        for row in manifest["campaign_found"]:
            raw = (args.data / row["path"].replace("\\", "/")).read_bytes()
            if hashlib.sha256(raw).hexdigest() != row["sha256"]:
                raise ValueError("CHK hash mismatch: " + row["path"])
            parts = []
            pos = 0
            while pos < len(raw):
                if pos + 8 > len(raw):
                    raise ValueError("Truncated CHK header")
                tag, n = struct.unpack_from("<4sI", raw, pos)
                pos += 8
                if pos + n > len(raw):
                    raise ValueError("Truncated CHK section")
                parts.append((tag, raw[pos:pos + n]))
                pos += n
            leaf_inputs = []
            for tag, part in parts:
                fn = {b"VER ": 0x468A30, b"SIDE": 0x468A60, b"OWNR": 0x468AB0,
                      b"ERA ": 0x468B00, b"DIM ": 0x469130, b"SPRP": 0x4692F0,
                      b"FORC": 0x469320, b"UPRP": 0x468DB0}.get(tag)
                if tag == b"MRGN":
                    fn = 0x468D80 if len(part) == 5100 else 0x468D50
                if fn is None:
                    continue
                uc = new_machine()
                uc.mem_write(0x536720, b"\1")  # Inspected baseline is Brood War enabled.
                sec = section(uc, part)
                returned, count = call(uc, fn, arguments=(sec, len(part), OUTPUT))
                assert returned == 1, (row["path"], tag, len(part), returned)
                if tag == b"VER ":
                    observed = bytes(uc.mem_read(OUTPUT + 24, 2))
                elif tag in (b"SIDE", b"OWNR"):
                    dest = 0x673D51 if tag == b"SIDE" else 0x673D50
                    observed = bytes(uc.mem_read(dest + 36 * i, 1)[0] for i in range(12))
                elif tag == b"ERA ":
                    expected = struct.pack("<H", struct.unpack("<H", part)[0] & 7)
                    observed = bytes(uc.mem_read(0x5264BC, 2))
                    assert observed == expected
                elif tag == b"DIM ":
                    observed = bytes(uc.mem_read(0x5264B4, 4))
                elif tag == b"SPRP":
                    observed = bytes(uc.mem_read(OUTPUT, 4))
                elif tag == b"FORC":
                    observed = bytes(uc.mem_read(OUTPUT + 4, 20))
                    assert observed == part + b"\0" * (20 - len(part))
                elif tag == b"MRGN":
                    observed = bytes(uc.mem_read(0x534F40, len(part)))
                elif tag == b"UPRP":
                    observed = bytes(uc.mem_read(0x6C4700, len(part)))
                if tag not in (b"ERA ", b"FORC"):
                    assert observed == part
                leaf_inputs.append({"tag": tag.decode("ascii"), "bytes": len(part),
                                    "input_sha256": hashlib.sha256(part).hexdigest(),
                                    "output_sha256": hashlib.sha256(observed).hexdigest(),
                                    "handler_va": hex(fn), "return": returned,
                                    "instructions": count})
            descriptor = next((d for d in descriptors if d["version"] == row["chk_version"]), None)
            presence = []
            if descriptor:
                uc = new_machine()
                write32(uc, LIST + 8, SCRATCH)
                for i, (tag, _) in enumerate(parts):
                    node = SCRATCH + 32 * i
                    write32(uc, node + 4, node + 32 if i + 1 < len(parts) else 0)
                    write32(uc, node + 16, struct.unpack("<I", tag)[0])
                tag_set = {tag.decode("ascii") for tag, _ in parts}
                for group in descriptor["stage_tables"]:
                    returned, count = call(uc, 0x4C1020, LIST, int(group["table_va"], 16), (len(group["entries"]),))
                    missing = [e["tag"] for e in group["entries"] if e["required"] and e["tag"] not in tag_set]
                    assert returned == int(not missing)
                    presence.append({"group": group["stage"], "return": returned,
                                     "missing_required_tags": missing, "instructions": count})
            campaign_coverage.append({"map_path": row["path"], "map_sha256": row["sha256"],
                                      "catalog_member": row["path"] in catalog_paths,
                                      "chk_version": row["chk_version"],
                                      "native_version_descriptor_present": descriptor is not None,
                                      "section_leaf_inputs": leaf_inputs,
                                      "required_presence_inputs": presence,
                                      "limits": "Independent section leaves and list fixtures; full map loading not executed"})
    leaf_checks = sum(len(r["section_leaf_inputs"]) for r in campaign_coverage)
    presence_checks = sum(len(r["required_presence_inputs"]) for r in campaign_coverage)
    result = {
        "plan": PLAN, "target_sha256": TARGET_HASH, "unicorn_version": unicorn.__version__,
        "probe_script_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        "runtime_architecture": platform.machine(), "python_version": platform.python_version(),
        "original_code_executed": True, "complete_game_executed": False, "stubs": [],
        "checks": len(cases), "all_expected_results_matched": True, "cases": cases,
        "version_descriptors": descriptors,
        "manifest_sha256": manifest_hash, "campaign_input_coverage": campaign_coverage,
        "campaign_section_leaf_checks": leaf_checks,
        "campaign_required_presence_checks": presence_checks,
        "total_native_checks": len(cases) + leaf_checks + presence_checks,
        "witnesses": {name: witness(*bounds) for name, bounds in {
            "version_selection": (0x469700, 0x4697BA),
            "table_group_selection": (0x469970, 0x469A10),
            "required_presence": (0x4C1020, 0x4C1069),
            "section_dispatch": (0x4C12E0, 0x4C134C),
            "bounded_reader": (0x4C12B0, 0x4C12D7),
            "core_loaders": (0x468A20, 0x468B55),
            "location_property_loaders": (0x468D50, 0x468DDB),
            "dimensions": (0x469130, 0x46916B),
            "map_properties_forces": (0x4692F0, 0x469355),
            "briefing_runner": (0x44FEE0, 0x44FF5D),
            "briefing_wait": (0x44FCD0, 0x44FD06),
            "briefing_transmission": (0x44FE30, 0x44FEB7),
            "briefing_list_runner": (0x460EA0, 0x460EDF),
            "briefing_loader": (0x468E80, 0x468F21),
            "briefing_owner_loader": (0x4607E0, 0x4609B4),
            "briefing_group_caller": (0x46A71D, 0x46A75D),
            "mode_table_predicate": (0x447DB0, 0x447DD5),
            "briefing_wait_schedule": (0x4611A0, 0x4611B7),
            "briefing_timer_callback": (0x4611C0, 0x4611D6),
            "briefing_wait_clear": (0x44FF60, 0x44FF68),
            "briefing_ui_timer_create": (0x4C5960, 0x4C5A19),
            "briefing_ui_timer_cancel": (0x4C58F0, 0x4C595B),
            "briefing_ui_timer": (0x4C5A20, 0x4C5B2D),
            "UPRP_reset": (0x4B6A70, 0x4B6A85),
            "native_memset": (0x4D0C60, 0x4D0CF0),
        }.items()},
        "limits": ["Synthetic inputs; this is not complete CHK or campaign loading.",
                   "Heap allocation and full initialization defaults were not executed; only the UPRP reset leaf was.",
                   "Briefing Wait arm/UI schedule excluded; its clear callback ran only with empty event queues.",
                   "UI timer expiry decision is a fragment with synthetic entry registers, not a complete timer routine.",
                   "Duplicate dispatch uses an explicitly constructed list, not the native chunk enumerator.",
                   "Memmove CPU feature state was set to select its original rep-movsb path."],
    }
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({"synthetic_checks": len(cases), "campaign_section_leaf_checks": leaf_checks,
                      "campaign_required_presence_checks": presence_checks,
                      "total_native_checks": result["total_native_checks"],
                      "all_expected_results_matched": True, "out": str(args.out)}))


if __name__ == "__main__":
    main()
