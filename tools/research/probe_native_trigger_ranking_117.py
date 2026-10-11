#!/usr/bin/env python3
"""Execute isolated original scalar ranking/Opponents functions without stubs.

Private emulated state and a per-instruction allowlist exclude imports and game
entry points. All campaign CHKs are audited; native fixtures are not live play.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import platform
import random
import struct

import capstone
import pefile

HASH = "94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0"
RECORD, STACK, STOP, MASK = 0x800000, 0x900000, 0xA00000, 0xFFFFFFFF
RANK = {8: ("kills", False, 0x4B9160), 9: ("score", False, 0x4B91C0),
        10: ("resources", False, 0x4B9220), 18: ("kills", True, 0x4B94A0),
        19: ("score", True, 0x4B9510), 20: ("resources", True, 0x4B9580)}
ALLOW = [(0x4B3160, 0x4B322F), (0x443560, 0x443573),
         (0x4BD590, 0x4BD5C9), (0x4BDA70, 0x4BDA76),
         (0x4B9660, 0x4B96BE), (0x4B9730, 0x4B9791),
         (0x4B9B10, 0x4B9B71), (0x4B9C60, 0x4B9CBB),
         (0x4B9E60, 0x4B9ED3), (0x4B9F70, 0x4B9FD1),
         (0x4BA410, 0x4BA4C9), (0x4BA510, 0x4BA624),
         (0x4B9D30, 0x4B9E0C), (0x4BA060, 0x4BA228),
         (0x4B9320, 0x4B935E), (0x4B9160, 0x4B9280),
         (0x4B94A0, 0x4B9502), (0x4B9510, 0x4B9572), (0x4B9580, 0x4B95E2)]
FIELDS = {"minerals": 0x5263D0, "gas": 0x526400,
          "score_units": 0x5291B4, "score_buildings": 0x529304,
          "score_kills": 0x5291E4, "score_razings": 0x529334,
          "score_custom": 0x5295D4, "kill_men": 0x529184,
          "kill_buildings": 0x5292D4, "kill_factories": 0x5293F4,
          "kills": 0x52EB84}
TYPES = {"resources": (0, 1, 2, 3), "score": tuple(range(9)),
         "kills": (0, 227, 229, 230, 231, 232)}
SELECTORS = list(range(12)) + list(range(13, 27))
PROFILES = {"zero": [0] * 12, "tie": [5] * 12, "max_tie": [MASK] * 12,
            "ascending": list(range(12)), "descending": list(range(11, -1, -1)),
            "unsigned": [0x80000000, 0x7FFFFFFF, MASK, 0, 1, 0x80000001,
                         0x7FFFFFFE, 0xFFFFFFFE, 19, 23, 29, 31]}


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    for name in ("exe", "data", "manifest"):
        ap.add_argument("--" + name, type=Path, required=True)
    ap.add_argument("--out", type=Path)
    ap.add_argument("--execute-original-code", action="store_true")
    a = ap.parse_args()
    raw = a.exe.read_bytes()
    if hashlib.sha256(raw).hexdigest() != HASH:
        ap.error("Unreviewed executable hash")
    if not a.execute_original_code:
        print(json.dumps({"target_sha256": HASH, "rank_conditions": RANK,
                          "opponents_helper": "0x4ba060", "opponents_condition": "0x4b9320",
                          "stubs": [], "limits": "Private scalar/alliance fixtures; no full mission"}, indent=2))
        return
    if not a.out:
        ap.error("--out required for execution evidence")
    import unicorn
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
    from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_ESP
    pe = pefile.PE(data=raw)
    u = Uc(UC_ARCH_X86, UC_MODE_32)
    u.mem_map(0x400000, (pe.OPTIONAL_HEADER.SizeOfImage + 4095) & ~4095)
    u.mem_write(0x400000, pe.get_memory_mapped_image())
    for address, n in ((RECORD, 4096), (STACK, 16384), (STOP, 4096)):
        u.mem_map(address, n)

    def w32(address, value):
        u.mem_write(address, struct.pack("<I", value & MASK))

    def fixture(mode, current, values, alliances):
        controllers, finished = [2] * 8, [0] * 8
        if mode == "sparse":
            controllers, finished = [0, 1, 2, 3, 4, 5, 7, 2], [0, 1, 2, 0, 0, 0, 0, 0]
        elif mode == "empty":
            controllers = [0] * 8
        state = {}
        for name, address in FIELDS.items():
            vector = values * 228 if name == "kills" else values
            state[name] = struct.pack("<" + "I" * len(vector), *vector)
            u.mem_write(address, state[name])
        for k in range(12):
            u.mem_write(0x6726D0 + 36 * k, bytes([controllers[k] if k < 8 else 0]))
            u.mem_write(0x6726D2 + 36 * k, bytes([1 + k % 4]))
        u.mem_write(0x5349E0, bytes(finished))
        w32(0x6CA754, current)
        u.mem_write(0x672664, bytes([int(mode == "alternate")]))
        u.mem_write(0x67294D, bytes([int(mode == "alternate")]))
        u.mem_write(0x5264AC, bytes([5, 1, 5, 0]))
        u.mem_write(0x534914, bytes(v for row in alliances for v in row))
        u.mem_write(RECORD, b"\0" * 64)
        active = [k for k in range(8) if controllers[k] in (1, 2, 3, 4, 7)
                  and not (controllers[k] in (1, 2) and finished[k] in (1, 2))]
        if mode == "alternate":
            active = [k for k in (5, 1, 5, 0) if k in active]
        return state, active, controllers

    def selection(selector, current, active, rel):
        if selector < 12:
            return [selector]
        if selector == 13:
            return [current]
        if selector == 17:
            return active + list(range(8, 12))
        if selector in (14, 15, 16):
            value = {14: 0, 15: 2, 16: 1}[selector]
            return [k for k in active if k != current and rel[current][k] == value]
        if 18 <= selector <= 21:
            return [k for k in active if k % 4 + 1 == selector - 17]
        if selector == 26:
            return [k for k in active if k != current and not (rel[current][k] == rel[k][current] == 2)]
        return []

    def opponents(owner, active, controllers, rel):
        if owner >= 8 or controllers[owner] in (3, 7):
            return 0, [], []
        coalition = [s for s in active if controllers[s] not in (3, 7)
                     and (s == owner or rel[s][owner] == rel[owner][s] == 2)]
        enemy_bitmap = {e for s in coalition for e in active
                        if e != s and controllers[e] not in (3, 7)
                        and not (rel[e][s] == rel[s][e] == 2)}
        return len(enemy_bitmap), coalition, sorted(enemy_bitmap)

    def scalar(family, typ, values):
        if family == "resources":
            return [v if typ in (0, 1) else (2 * v) & MASK if typ == 2 else 0 for v in values]
        if family == "score":
            multiplier = {0: 5, 1: 1, 2: 1, 3: 2, 4: 1, 5: 1, 6: 2, 7: 1}.get(typ, 0)
            return [(multiplier * v) & MASK for v in values]
        return [(2 * v) & MASK if typ == 229 else v for v in values]

    all_visited = set()

    def call(entry, ecx, argument=None):
        done, count = False, 0

        def step(machine, pc, n, unused):
            nonlocal done, count
            if pc == STOP:
                done = True
                machine.emu_stop()
                return
            if not any(lo <= pc and pc + n <= hi for lo, hi in ALLOW):
                raise RuntimeError(f"Unapproved instruction {pc:#x}")
            count += 1
            all_visited.add(pc)

        hook = u.hook_add(UC_HOOK_CODE, step)
        w32(STACK + 0x2000, STOP)
        if argument is not None:
            w32(STACK + 0x2004, argument)
        u.reg_write(UC_X86_REG_ESP, STACK + 0x2000)
        u.reg_write(UC_X86_REG_ECX, ecx)
        u.reg_write(UC_X86_REG_EDX, 0)
        try:
            u.emu_start(entry, STOP + 1, count=40000)
        finally:
            u.hook_del(hook)
        assert done
        return u.reg_read(UC_X86_REG_EAX), count

    def verify(state, rel):
        for name, wanted in state.items():
            assert bytes(u.mem_read(FIELDS[name], len(wanted))) == wanted, name
        assert bytes(u.mem_read(0x534914, 144)) == bytes(v for row in rel for v in row)
        return hashlib.sha256(b"".join(state.values())).hexdigest()

    rank_cases, opponent_cases = [], []
    neutral_rel = [[2 if x == y else 0 for y in range(12)] for x in range(12)]
    for kind, (family, least, entry) in RANK.items():
        for mode in ("ordinary", "sparse", "alternate", "empty"):
            for profile, values in PROFILES.items():
                for current in (0, 1, 2, 5, 7, 8, 11):
                    for typ in TYPES[family]:
                        state, active, controllers = fixture(mode, current, values, neutral_rel)
                        scores = scalar(family, typ, values)
                        initial = MASK if least else 0
                        owner_value = scores[current] if current in active else initial
                        extreme = min([initial] + [scores[k] for k in active]) if least else max([initial] + [scores[k] for k in active])
                        expected = int(owner_value <= extreme) if least else int(owner_value >= extreme)
                        # These fields are ignored by native ranking predicates.
                        w32(RECORD + 4, MASK)
                        w32(RECORD + 8, 0x12345678)
                        u.mem_write(RECORD + 12, struct.pack("<H", typ if family == "kills" else 0))
                        u.mem_write(RECORD + 14, bytes([99, kind, typ if family != "kills" else 0]))
                        observed, count = call(entry, RECORD)
                        assert observed == expected, (kind, mode, profile, current, typ, scores, active, observed, expected)
                        digest = verify(state, neutral_rel)
                        rank_cases.append({"condition_id": kind, "family": family, "least": least,
                                           "fixture": [mode, profile, current], "type": typ,
                                           "active_sequence": active, "scalar_values": scores,
                                           "current_value_or_default": owner_value, "extreme_or_default": extreme,
                                           "observed": observed, "instructions": count, "state_sha256": digest})

    rng = random.Random(117)
    matrices = {"all_foes": neutral_rel,
                "all_allied": [[2] * 12 for unused in range(12)],
                "all_neutral": [[2 if x == y else 1 for y in range(12)] for x in range(12)]}
    coalition = [row[:] for row in neutral_rel]
    coalition[0][1] = coalition[1][0] = 2
    coalition[0][2] = coalition[2][0] = 2
    # Both allies are allied to owner0 but hostile to each other: union includes1/2.
    coalition[0][3], coalition[3][0] = 2, 0
    matrices["nontransitive_team"] = coalition
    matrices["random_seed_117"] = [[rng.randrange(3) for y in range(12)] for x in range(12)]
    for mode in ("ordinary", "sparse", "alternate", "empty"):
        for name, rel in matrices.items():
            for current in (0, 2, 7, 8):
                for selector in SELECTORS:
                    state, active, controllers = fixture(mode, current, PROFILES["ascending"], rel)
                    selected = selection(selector, current, active, rel)
                    breakdown = []
                    for owner in selected:
                        value, team, enemy = opponents(owner, active, controllers, rel)
                        breakdown.append({"owner": owner, "count": value, "team_sequence": team, "opponents": enemy})
                    expected = sum(item["count"] for item in breakdown) & MASK
                    observed, count = call(0x4BA060, selector, 0)
                    assert observed == expected, (mode, name, current, selector, selected, breakdown, observed, expected)
                    digest = verify(state, rel)
                    opponent_cases.append({"kind": "opponents_getter", "fixture": [mode, name, current],
                                           "selector": selector, "selected_players": selected, "breakdown": breakdown,
                                           "observed": observed, "instructions": count, "state_sha256": digest})
                    for comparison in (0, 1, 10, 2):
                        # Cycle below/equal/above threshold across selectors and modes.
                        amount = max(0, expected + (selector % 3) - 1)
                        w32(RECORD + 4, selector)
                        w32(RECORD + 8, amount)
                        u.mem_write(RECORD + 14, bytes([comparison, 14]))
                        wanted = int((expected & 255) >= amount) if comparison == 0 else int((expected & 255) <= amount) if comparison == 1 else int((expected & 255) == amount) if comparison == 10 else 0
                        observed, count = call(0x4B9320, RECORD)
                        assert observed == wanted, (mode, name, current, selector, comparison, amount, observed, wanted)
                        verify(state, rel)
                        opponent_cases.append({"kind": "opponents_condition", "fixture": [mode, name, current],
                                               "selector": selector, "comparison": comparison, "amount": amount,
                                               "query_value_u32": expected, "compared_value_u8": expected & 255,
                                               "observed": observed, "instructions": count, "state_sha256": digest})
    mraw = a.manifest.read_bytes()
    manifest = json.loads(mraw)
    maps, histogram = [], Counter()
    for row in manifest["campaign_found"]:
        data = (a.data / row["path"].replace("\\", "/")).read_bytes()
        assert hashlib.sha256(data).hexdigest() == row["sha256"]
        pos, counts, trigger_count = 0, Counter(), 0
        while pos < len(data):
            tag, n = struct.unpack_from("<4sI", data, pos)
            part = data[pos + 8:pos + 8 + n]
            pos += 8 + n
            assert len(part) == n
            if tag != b"TRIG":
                continue
            assert n % 2400 == 0
            trigger_count += n // 2400
            for offset in range(0, n, 2400):
                for index in range(16):
                    kind = part[offset + index * 20 + 15]
                    if kind in RANK or kind == 14:
                        counts[kind] += 1
        histogram.update(counts)
        maps.append({"path": row["path"], "sha256": row["sha256"], "trigger_count": trigger_count,
                     "selected_condition_histogram": dict(counts)})
    # Exact local campaign inventory has no use of these seven condition types.
    assert not histogram, histogram
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    witnesses = [{"va": hex(pc), "rva": hex(pc - 0x400000), "bytes": ins.bytes.hex(),
                  "mnemonic": ins.mnemonic, "operands": ins.op_str}
                 for pc in sorted(all_visited) for ins in list(md.disasm(pe.get_data(pc - 0x400000, 15), pc))[:1]]
    result = {"target_sha256": HASH, "probe_script_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
              "runtime_architecture": platform.machine(), "python_version": platform.python_version(),
              "unicorn_version": unicorn.__version__, "pefile_version": pefile.__version__, "capstone_version": capstone.__version__,
              "native_code_executed": True, "complete_game_executed": False, "stubs": [], "imports_executed": [],
              "execution_scope": "Complete original Opponents/ranking functions and actual callees",
              "ranking_cases": rank_cases, "opponents_cases": opponent_cases,
              "passed_cases": len(rank_cases) + len(opponent_cases), "failed_cases": 0,
              "manifest_sha256": hashlib.sha256(mraw).hexdigest(),
              "campaign_audit": {"physical_maps": len(maps), "maps": maps, "selected_condition_histogram": dict(histogram)},
              "executed_instruction_witnesses": witnesses,
              "limits": ["All scalar/controller/alliance inputs are fixtures, not played mission state",
                         "No selected conditions occur in any68auditedCHKs; no campaign parameter execution is claimed",
                         "Opponents normal selectors0..11/13..26; None12 and out-of-range EUD selectors excluded",
                         "Command/Bring and unit/location counting are not executed"]}
    a.out.parent.mkdir(parents=True, exist_ok=True)
    a.out.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({"passed_cases": result["passed_cases"], "ranking": len(rank_cases),
                      "opponents": len(opponent_cases), "campaign_selected_records": sum(histogram.values())}))


if __name__ == "__main__":
    main()
