#!/usr/bin/env python3
"""Probe complete original scalar trigger functions with private game-state fixtures.

Only the hash-pinned PE instruction allowlist can execute. No callees are stubbed,
no imports are allowed, and no complete game or host-side original code is run.
Campaign checks cover decoded parameters, not original mission runtime state.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import platform
import struct

import capstone
import pefile

HASH = "94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0"
RECORD, STACK, STOP = 0x800000, 0x900000, 0xA00000
MASK = 0xFFFFFFFF
GET = {"resources": 0x4BA410, "score": 0x4BA510,
       "deaths": 0x4B99E0, "kills": 0x4B9D30}
COND = {4: ("resources", 0x4B8F80), 5: ("kills", 0x4B8FD0),
        15: ("deaths", 0x4B9360), 21: ("score", 0x4B9020)}
ACTION = {26: ("resources", 0x4B6940), 27: ("score", 0x4B6990),
          45: ("deaths", 0x4B6550)}
ALLOW = [(0x4B3160, 0x4B322F), (0x443560, 0x443573),
         (0x4BD590, 0x4BD5C9), (0x4BDA70, 0x4BDA76),
         (0x4B9660, 0x4B96BE), (0x4B9730, 0x4B9791),
         (0x4B9B10, 0x4B9B74), (0x4B9C60, 0x4B9CBB),
         (0x4B9E60, 0x4B9ED3), (0x4B9F70, 0x4B9FD4),
         (0x4BA410, 0x4BA4C9), (0x4BA510, 0x4BA624),
         (0x4B99E0, 0x4B9ABC), (0x4B9D30, 0x4B9E0C),
         (0x4B6A90, 0x4B6B90), (0x4B6BE0, 0x4B6CB8),
         (0x4B6CF0, 0x4B6E38), (0x4B7E80, 0x4B7F80),
         (0x4B82D0, 0x4B83A8), (0x4B8440, 0x4B8588),
         (0x4B8640, 0x4B87C8), (0x4B8810, 0x4B8914),
         (0x4B8950, 0x4B8BE8), (0x4B8F80, 0x4B8FC2),
         (0x4B8FD0, 0x4B9012), (0x4B9020, 0x4B9062),
         (0x4B9360, 0x4B93A2), (0x4B6550, 0x4B659E),
         (0x4B6940, 0x4B698E), (0x4B6990, 0x4B69DE)]
FIELDS = {"minerals": 0x5263D0, "gas": 0x526400,
          "cumulative_minerals": 0x526460, "cumulative_gas": 0x526430,
          "score_units": 0x5291B4, "score_buildings": 0x529304,
          "score_kills": 0x5291E4, "score_razings": 0x529334,
          "score_custom": 0x5295D4, "death_men": 0x529154,
          "death_buildings": 0x5292A4, "death_factories": 0x5293C4,
          "kill_men": 0x529184, "kill_buildings": 0x5292D4,
          "kill_factories": 0x5293F4, "deaths": 0x531644,
          "kills": 0x52EB84}
SELECTORS = list(range(12)) + list(range(13, 27))
TYPES = {"resources": (0, 1, 2, 3), "score": tuple(range(9)),
         "deaths": (0, 227, 229, 230, 231, 232),
         "kills": (0, 227, 229, 230, 231, 232)}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ("exe", "data", "manifest"):
        p.add_argument("--" + name, type=Path, required=True)
    p.add_argument("--out", type=Path)
    p.add_argument("--execute-original-code", action="store_true")
    a = p.parse_args()
    raw = a.exe.read_bytes()
    if hashlib.sha256(raw).hexdigest() != HASH:
        p.error("Unreviewed executable hash")
    if not a.execute_original_code:
        print(json.dumps({"target_sha256": HASH, "complete_functions": GET,
                          "conditions": COND, "actions": ACTION, "stubs": [],
                          "limits": "Synthetic scalar state; campaign parameters, not live missions"}, indent=2))
        return
    if not a.out:
        p.error("--out required for execution evidence")
    import unicorn
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_ESP
    pe = pefile.PE(data=raw)
    image = pe.get_memory_mapped_image()
    u = Uc(UC_ARCH_X86, UC_MODE_32)
    u.mem_map(0x400000, (pe.OPTIONAL_HEADER.SizeOfImage + 4095) & ~4095)
    u.mem_write(0x400000, image)
    for address, size in ((RECORD, 4096), (STACK, 16384), (STOP, 4096)):
        u.mem_map(address, size)

    def w32(address, value):
        u.mem_write(address, struct.pack("<I", value & MASK))

    def fixture(mode="ordinary", overflow=False, current=2):
        state = {}
        for i, name in enumerate(FIELDS):
            n = 228 * 12 if name in ("deaths", "kills") else 12
            state[name] = [(100 * (i + 1) + 17 * k) & MASK for k in range(n)]
            if overflow:
                state[name] = [(MASK - 5 + k % 6) for k in range(n)]
            u.mem_write(FIELDS[name], struct.pack("<" + "I" * n, *state[name]))
        controllers = [2] * 8
        finished = [0] * 8
        if mode == "sparse":
            controllers = [0, 1, 2, 3, 4, 5, 7, 2]
            finished = [0, 1, 2, 0, 0, 0, 0, 0]
        for player in range(12):
            u.mem_write(0x6726D0 + player * 36, bytes([controllers[player] if player < 8 else 0]))
            u.mem_write(0x6726D2 + player * 36, bytes([1 + player % 4]))
        u.mem_write(0x5349E0, bytes(finished))
        w32(0x6CA754, current)
        alternate = mode == "alternate"
        u.mem_write(0x672664, bytes([int(alternate)]))
        u.mem_write(0x67294D, bytes([int(alternate)]))
        u.mem_write(0x5264AC, bytes([5, 1, 5, 0]))
        alliances = [[(x + y) % 3 for y in range(12)] for x in range(12)]
        # Explicit mutual, one-way, neutral, enemy and self relations.
        for player in range(12):
            alliances[current][player] = player % 3
        alliances[current][1] = alliances[1][current] = 2
        alliances[current][5], alliances[5][current] = 2, 0
        alliances[current][current] = 2
        u.mem_write(0x534914, bytes(v for row in alliances for v in row))
        u.mem_write(RECORD, b"\0" * 64)
        active = [k for k in range(8) if controllers[k] in (1, 2, 3, 4, 7)
                  and not (controllers[k] in (1, 2) and finished[k] in (1, 2))]
        if alternate:
            active = [k for k in (5, 1, 5, 0) if k in active]
        return state, {"mode": mode, "overflow": overflow, "current": current,
                       "active": active, "alliances": alliances}

    def players(selector, f):
        active, cur, rel = f["active"], f["current"], f["alliances"]
        if selector < 12:
            return [selector]
        if selector == 13:
            return [cur]
        if selector == 17:
            return active + list(range(8, 12))
        if selector in (14, 15, 16):
            relation = {14: 0, 15: 2, 16: 1}[selector]
            return [k for k in active if k != cur and rel[cur][k] == relation]
        if selector == 26:
            return [k for k in active if k != cur and not (rel[cur][k] == rel[k][cur] == 2)]
        if 18 <= selector <= 21:
            return [k for k in active if 1 + k % 4 == selector - 17]
        return []

    def query(state, family, selector, typ, f):
        total = 0
        for k in players(selector, f):
            if family == "resources":
                value = {0: state["minerals"][k], 1: state["gas"][k],
                         2: state["minerals"][k] + state["gas"][k]}.get(typ, 0)
            elif family == "score":
                keys = {0: ("units", "buildings", "kills", "razings", "custom"),
                        1: ("units",), 2: ("buildings",), 3: ("units", "buildings"),
                        4: ("kills",), 5: ("razings",), 6: ("kills", "razings"), 7: ("custom",)}.get(typ, ())
                value = sum(state["score_" + name][k] for name in keys)
            elif typ < 228:
                value = state[family][12 * typ + k]
            else:
                prefix = "death" if family == "deaths" else "kill"
                keys = {229: ("men", "buildings"), 230: ("men",),
                        231: ("buildings",), 232: ("factories",)}[typ]
                value = sum(state[prefix + "_" + name][k] for name in keys)
            total = (total + value) & MASK
        return total

    def mutate(state, family, selector, typ, modifier, amount, f):
        for k in players(selector, f):
            if k >= 8 or modifier not in (7, 8, 9):
                continue
            writes, n = [], amount
            if family == "resources":
                names = {0: ("minerals",), 1: ("gas",), 2: ("minerals", "gas")}.get(typ, ())
                for name in names:
                    writes.append((name, k))
                    if modifier != 9:
                        writes.append(("cumulative_" + name, k))
            elif family == "score":
                names = {0: ("units", "buildings", "kills", "razings"),
                         1: ("units",), 2: ("buildings",), 3: ("units", "buildings"),
                         4: ("kills",), 5: ("razings",), 6: ("kills", "razings"), 7: ("custom",)}.get(typ, ())
                if typ in (3, 6):
                    n = ((amount + 1) & MASK) >> 1
                elif typ == 0:
                    n = ((amount + 3) & MASK) >> 2
                writes = [("score_" + name, k) for name in names]
            elif typ < 228:
                writes = [("deaths", 12 * typ + k)]
            elif typ in (230, 231, 232):
                writes = [("death_" + {230: "men", 231: "buildings", 232: "factories"}[typ], k)]
            for name, index in writes:
                v = state[name][index]
                state[name][index] = n if modifier == 7 else (v + n) & MASK if modifier == 8 else max(0, v - n)

    def call(entry, ecx, edx=0, argument=None):
        complete, instructions, visited = False, 0, set()

        def step(machine, pc, n, unused):
            nonlocal complete, instructions
            if pc == STOP:
                complete = True
                machine.emu_stop()
                return
            if not any(lo <= pc and pc + n <= hi for lo, hi in ALLOW):
                raise RuntimeError(f"Unapproved instruction {pc:#x}")
            instructions += 1
            visited.add(pc)

        hook = u.hook_add(UC_HOOK_CODE, step)
        sp = STACK + 0x2000
        w32(sp, STOP)
        if argument is not None:
            w32(sp + 4, argument)
        u.reg_write(UC_X86_REG_ESP, sp)
        u.reg_write(UC_X86_REG_ECX, ecx)
        u.reg_write(UC_X86_REG_EDX, edx)
        try:
            u.emu_start(entry, STOP + 1, count=20000)
        finally:
            u.hook_del(hook)
        if not complete:
            raise RuntimeError("Function did not return within instruction budget")
        all_visited.update(visited)
        return u.reg_read(UC_X86_REG_EAX), instructions

    def verify_state(expected):
        changes = []
        for name, values in expected.items():
            observed = bytes(u.mem_read(FIELDS[name], 4 * len(values)))
            wanted = struct.pack("<" + "I" * len(values), *values)
            if observed != wanted:
                raise AssertionError((name, list(struct.unpack("<" + "I" * len(values), observed)), values))
            changes.append(observed)
        return hashlib.sha256(b"".join(changes)).hexdigest()

    all_visited = set()
    synthetic, campaign = [], []

    def getter_case(family, selector, typ, mode="ordinary", overflow=False, current=2):
        state, f = fixture(mode, overflow, current)
        expected = query(state, family, selector, typ, f)
        observed, count = call(GET[family], selector, typ, 0)
        assert observed == expected, (family, selector, typ, f, observed, expected)
        digest = verify_state(state)
        return {"kind": "getter", "family": family, "selector": selector, "type": typ,
                "fixture": [mode, overflow, current], "selected_players": players(selector, f),
                "observed": observed, "state_sha256": digest, "instructions": count}

    def condition_case(kind, selector, typ, comparison, amount, mode="ordinary", overflow=False, current=2):
        state, f = fixture(mode, overflow, current)
        family, entry = COND[kind]
        value = query(state, family, selector, typ, f)
        expected = int(value >= amount) if comparison == 0 else int(value <= amount) if comparison == 1 else int(value == amount) if comparison == 10 else 0
        w32(RECORD + 4, selector)
        w32(RECORD + 8, amount)
        u.mem_write(RECORD + 12, struct.pack("<H", typ if family in ("kills", "deaths") else 0))
        u.mem_write(RECORD + 14, bytes([comparison, kind, typ if family in ("resources", "score") else 0]))
        observed, count = call(entry, RECORD)
        assert observed == expected, (kind, selector, typ, comparison, amount, observed, expected)
        digest = verify_state(state)
        return {"kind": "condition", "condition_id": kind, "selector": selector, "type": typ,
                "comparison": comparison, "amount": amount, "fixture": [mode, overflow, current],
                "queried_value": value, "observed": observed, "state_sha256": digest, "instructions": count}

    def action_case(kind, selector, typ, modifier, amount, mode="ordinary", overflow=False, current=2):
        state, f = fixture(mode, overflow, current)
        family, entry = ACTION[kind]
        before = {name: values[:] for name, values in state.items()}
        mutate(state, family, selector, typ, modifier, amount, f)
        w32(RECORD + 16, selector)
        w32(RECORD + 20, amount)
        u.mem_write(RECORD + 24, struct.pack("<H", typ))
        u.mem_write(RECORD + 26, bytes([kind, modifier]))
        observed, count = call(entry, RECORD)
        assert observed == 1
        digest = verify_state(state)
        changes = {name: [[i, old, new] for i, (old, new) in enumerate(zip(before[name], values)) if old != new]
                   for name, values in state.items() if values != before[name]}
        return {"kind": "action", "action_id": kind, "selector": selector, "type": typ,
                "modifier": modifier, "amount": amount, "fixture": [mode, overflow, current],
                "selected_players": players(selector, f), "observed": observed,
                "changes": changes, "state_sha256": digest, "instructions": count}

    for selector in SELECTORS:
        for family, types in TYPES.items():
            for typ in types:
                synthetic.append(getter_case(family, selector, typ))
        for kind, (family, unused) in COND.items():
            typ = TYPES[family][selector % len(TYPES[family])]
            state, f = fixture()
            value = query(state, family, selector, typ, f)
            for comparison in (0, 1, 10, 2):
                for amount in (max(0, value - 1), value, min(MASK, value + 1)):
                    synthetic.append(condition_case(kind, selector, typ, comparison, amount))
        for kind, (family, unused) in ACTION.items():
            for typ in TYPES[family]:
                for modifier in (7, 8, 9):
                    synthetic.append(action_case(kind, selector, typ, modifier, 5))
    # Amount overflow, ceil-division overflow, subtraction clamping and invalid modifiers.
    for kind, (family, unused) in ACTION.items():
        for typ in TYPES[family]:
            for modifier in (7, 8, 9, 0):
                for amount in (0, 1, MASK):
                    synthetic.append(action_case(kind, 13, typ, modifier, amount, overflow=True))
    # Deaths group mutation 228/229 is intentionally a no-op.
    for typ in (228, 229):
        for modifier in (7, 8, 9):
            synthetic.append(action_case(45, 17, typ, modifier, 123))
    # Both iterator branches, sparse controller/finished filter and alternative duplicates.
    for mode in ("sparse", "alternate"):
        for selector in (13, 14, 15, 16, 17, 18, 19, 20, 21, 26):
            for family in GET:
                synthetic.append(getter_case(family, selector, TYPES[family][0], mode, current=7))
            for kind, (family, unused) in ACTION.items():
                for modifier in (7, 8, 9):
                    synthetic.append(action_case(kind, selector, TYPES[family][0], modifier, 19, mode, current=7))
    # Read-side 32-bit summation and unsigned thresholds under overflowing fixtures.
    for kind, (family, unused) in COND.items():
        for selector in (0, 13, 17, 18, 26):
            for comparison in (0, 1, 10):
                synthetic.append(condition_case(kind, selector, TYPES[family][0], comparison, 0x80000000, overflow=True))

    manifest_raw = a.manifest.read_bytes()
    manifest = json.loads(manifest_raw)
    unique, maps = {}, []
    raw_conditions, raw_actions, ignored = Counter(), Counter(), Counter()
    for row in manifest["campaign_found"]:
        data = (a.data / row["path"].replace("\\", "/")).read_bytes()
        assert hashlib.sha256(data).hexdigest() == row["sha256"]
        pos, counts, keys, trig_count = 0, Counter(), set(), 0
        while pos < len(data):
            tag, n = struct.unpack_from("<4sI", data, pos)
            part = data[pos + 8:pos + 8 + n]
            pos += 8 + n
            assert len(part) == n
            if tag != b"TRIG":
                continue
            assert n % 2400 == 0
            for offset in range(0, n, 2400):
                trig_count += 1
                for index in range(16):
                    v = part[offset + 20 * index:offset + 20 * index + 20]
                    kind = v[15]
                    if kind not in COND:
                        continue
                    family = COND[kind][0]
                    selector, amount = struct.unpack_from("<II", v, 4)
                    typ = struct.unpack_from("<H", v, 12)[0] if family in ("deaths", "kills") else v[16]
                    key = ("condition", kind, selector, typ, v[14], amount)
                    raw_conditions[kind] += 1
                    counts[str(kind)] += 1
                    if v[17] & 2:
                        ignored["conditions"] += 1
                    unique.setdefault(key, []).append([row["path"], trig_count - 1, index, bool(v[17] & 2)])
                    keys.add(key)
                for index in range(64):
                    v = part[offset + 320 + 32 * index:offset + 320 + 32 * index + 32]
                    kind = v[26]
                    if kind not in ACTION:
                        continue
                    selector, amount = struct.unpack_from("<II", v, 16)
                    typ = struct.unpack_from("<H", v, 24)[0]
                    key = ("action", kind, selector, typ, v[27], amount)
                    raw_actions[kind] += 1
                    counts[str(kind)] += 1
                    if v[28] & 2:
                        ignored["actions"] += 1
                    unique.setdefault(key, []).append([row["path"], trig_count - 1, index, bool(v[28] & 2)])
                    keys.add(key)
        maps.append({"path": row["path"], "sha256": row["sha256"], "trigger_count": trig_count,
                     "scalar_record_histogram": dict(counts), "distinct_parameter_tuples": len(keys)})
    unresolved = []
    for key, refs in sorted(unique.items()):
        kind, code, selector, typ, comparison_or_modifier, amount = key
        valid = selector in SELECTORS and (typ < 228 or typ in (229, 230, 231, 232))
        if not valid:
            unresolved.append({"parameters": list(key), "references": refs})
            continue
        case = condition_case(code, selector, typ, comparison_or_modifier, amount) if kind == "condition" else action_case(code, selector, typ, comparison_or_modifier, amount)
        case["references"] = refs
        campaign.append(case)
    assert not unresolved, unresolved
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    witnesses = [{"va": hex(pc), "rva": hex(pc - 0x400000), "bytes": ins.bytes.hex(),
                  "mnemonic": ins.mnemonic, "operands": ins.op_str}
                 for pc in sorted(all_visited) for ins in list(md.disasm(pe.get_data(pc - 0x400000, 15), pc))[:1]]
    result = {"target_sha256": HASH, "probe_script_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
              "runtime_architecture": platform.machine(), "python_version": platform.python_version(),
              "unicorn_version": unicorn.__version__, "pefile_version": pefile.__version__,
              "capstone_version": capstone.__version__, "native_code_executed": True,
              "complete_game_executed": False, "stubs": [], "imports_executed": [],
              "execution_scope": "Complete original functions and their actual scalar/selector callees",
              "manifest_sha256": hashlib.sha256(manifest_raw).hexdigest(),
              "synthetic_cases": synthetic, "campaign_parameter_cases": campaign,
              "campaign_audit": {"physical_maps": len(maps), "maps": maps,
                                 "raw_condition_histogram": dict(raw_conditions), "raw_action_histogram": dict(raw_actions),
                                 "ignored_record_histogram": dict(ignored), "distinct_parameter_tuples": len(unique),
                                 "unresolved_parameters": unresolved},
              "passed_cases": len(synthetic) + len(campaign), "failed_cases": 0,
              "executed_instruction_witnesses": witnesses,
              "limits": ["Scalar state is a synthetic fixture; campaign parameters do not supply live mission state",
                         "Queries cover valid direct players 0..11 and selectors 13..26; None 12/out-of-range EUD behavior excluded",
                         "Deaths setters update scalar counters; no actual death/kill/score producer or unit creation executed",
                         "Raw campaign inputs include all TRIG records, including disabled or unreachable slots; VM skip behavior is separate evidence"]}
    a.out.parent.mkdir(parents=True, exist_ok=True)
    a.out.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({"passed_cases": result["passed_cases"], "synthetic": len(synthetic), "campaign": len(campaign),
                      "raw_conditions": dict(raw_conditions), "raw_actions": dict(raw_actions), "unresolved": len(unresolved)}))


if __name__ == "__main__":
    main()
