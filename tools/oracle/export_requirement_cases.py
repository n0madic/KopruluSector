#!/usr/bin/env python3
"""Export the original's requirement-interpreter probes into JSON golden files (plan decision D16).

The original has two requirement interpreters: the general order/research/tech-use/upgrade evaluator
0x407570 with its wrappers 0x407bb0, 0x407dc0, 0x407f90, 0x408140, and the unit-production gate
CanTrainUnit 0x4b32a0. Their isolated original-code probes are
docs/research/evidence/native-dat-requirements-probes.json (tools/research/inspect_native_dat_requirements_117.py)
and docs/research/evidence/native-requirements-probes.json (tools/research/inspect_native_requirements_117.py).
The evidence keeps only part of each fixture (`fixture_inputs`); the rest lives in the probe scripts' case
builders. This exporter mirrors those builders as data: each case gets the call, its arguments, the synthetic
requirement table it ran on, and its patch over the probe's `new()` fixture as semantic operations
(tests/c/test_requirements.c applies them to a native game). The probe scripts are hashed against the
evidence's `probe_script_sha256`, so a changed probe fails the export until this mirror is updated.

Patch operations (player 0 unless named):
  {"u", field, value}              the producer's unit field (g.u)
  {"u8", field, byte, value}       one byte of the producer's unit field (a w8 into a wider field)
  {"u_other", field}               the producer's field points at the second record (the probe's zeroed `target`)
  {"other", field, value}          the second record's unit field
  {"dat", table, column, i, value} a DAT value
  {"count_completed", type, v}, {"count_all", type, v}
  {"available", tech, v}, {"researched", tech, v}, {"upgrade", id, level}, {"unit_available", type, v}
  {"research_progress", player, byte, v}, {"upgrade_progress", player, byte, v}
  {"cheats", v}, {"expansion", v}
  {"offset", id, v}                the requirement offset of id in the case's domain

Outputs: tests/golden/oracle/requirements_general.json, tests/golden/oracle/requirements_units.json.

Usage: python3 tools/oracle/export_requirement_cases.py [--check]
"""
from __future__ import annotations

import argparse
import logging
import sys
from typing import Any

from oracle_common import (GOLDEN, ROOT, TARGET_SHA256, ExportError, cases_module, load_evidence, sha256_file,
                           update_goldens)

log = logging.getLogger("export_requirement_cases")

END = 0xFFFF
U32 = 1 << 32
# The probe's synthetic tables of the wrappers and the unit gate: the header word 0, the program from word 1.
TABLE_HEADER = 0
PROGRAM_AT = 1

GENERAL_PROBE = "tools/research/inspect_native_dat_requirements_117.py"
UNIT_PROBE = "tools/research/inspect_native_requirements_117.py"

# The probe's domain names, their call names and capacities.
DOMAINS = {"orders": ("orders", 189), "research": ("research", 44), "technology_use": ("tech_use", 44),
           "upgrades": ("upgrades", 61)}

# Probe new() of the general evaluator (inspect_native_dat_requirements_117.py).
GENERAL_FIXTURE = {
    "producer": 7, "status": 3, "order": 0x9C, "tech_byte": 44, "upgrade_byte": 61, "queue": 228,
    "count_completed": 1, "count_all": 1, "tech_available": 1, "tech_researched": 1, "upgrade_level": 0,
    "upgrade_max": 3, "research_progress": 0, "upgrade_progress": 0, "cheats": 0, "expansion": 1, "offsets": END,
    "producer_dat": {"special_ability_flags": 0, "right_click_action": 1, "space_provided": 8, "ground_weapon": 0,
                     "air_weapon": 0},
}
# Probe new() of the unit gate (inspect_native_requirements_117.py).
UNIT_FIXTURE = {
    "producer": 7, "request": 106, "status": 3, "order": 0x9C, "tech_byte": 44, "upgrade_byte": 61, "queue": 228,
    "count_completed": 1, "count_all": 1, "unit_available": 1, "tech_researched": 1, "upgrade_level": 1,
    "cheats": 0, "expansion": 1, "offsets": END, "producer_dat": {"special_ability_flags": 0},
}

CHEAT_TECH, CHEAT_ADDON = 0x1000, 0x2000
HALLUCINATED = 0x40000003
BURROWED = 0x13


def status(v: int) -> list:
    return ["u", "status", v]


def tech_byte(v: int) -> list:
    return ["u8", "union_0xc8", 0, v]


def upgrade_byte(v: int) -> list:
    return ["u8", "union_0xc8", 1, v]


def flags(t: int, v: int) -> list:
    return ["dat", "units", "special_ability_flags", t, v]


def completed(t: int, v: int) -> list:
    return ["count_completed", t, v]


# The general token cases ("general token <name>": words, patch), probe list `tests`.
GENERAL_TOKENS = {
    "current producer matches": ([0xFF02, 7], []),
    "current producer mismatch": ([0xFF02, 64], []),
    "current producer OR next matches": ([0xFF02, 64, 0xFF01, 0xFF02, 7], []),
    "ordinary completed missing": ([111], [completed(111, 0)]),
    "ordinary completed cheat does not bypass": ([111], [completed(111, 0), ["cheats", CHEAT_ADDON]]),
    "has-unit incomplete": ([0xFF03, 111], [completed(111, 0)]),
    "landed success": ([0xFF05], []),
    "landed failure": ([0xFF05], [status(1)]),
    "lifted failure": ([0xFF06], []),
    "lifted success": ([0xFF06], [status(1)]),
    "busy empty": ([0xFF07], []),
    "busy queue": ([0xFF07], [["u", "queue0", 0]]),
    "not addon building": ([0xFF08], []),
    "addon building": ([0xFF08], [["u", "secondary_order", 0x25], ["u_other", "build_unit"]]),
    "not researching": ([0xFF09], []),
    "researching": ([0xFF09], [tech_byte(0)]),
    "not upgrading": ([0xFF0A], []),
    "upgrading": ([0xFF0A], [upgrade_byte(0)]),
    "Nydus no link": ([0xFF0D], []),
    "Nydus linked": ([0xFF0D], [["u_other", "union_0xd0"]]),
    "context technology missing": ([0xFF0F], [["researched", 0, 0]]),
    "context technology cheat": ([0xFF0F], [["researched", 0, 0], ["cheats", CHEAT_TECH]]),
    "burrowed lacks AI": ([0xFF11], [status(BURROWED)]),
    "burrowed has AI exception": ([0xFF11], [status(BURROWED), ["u_other", "ai"]]),
    "not burrowed success": ([0xFF11], []),
    "burrowed required fail": ([0xFF26], []),
    "burrowed required pass": ([0xFF26], [status(BURROWED)]),
    "not landed failure": ([0xFF12], []),
    "not landed success": ([0xFF12], [status(1)]),
    "landed only failure": ([0xFF13], [status(1)]),
    "landed only success": ([0xFF13], []),
    "move blocked grounded": ([0xFF14], []),
    "move supported action": ([0xFF14], [status(1)]),
    "attack ground weapon": ([0xFF15], []),
    "attack no weapons": ([0xFF15], [["dat", "units", "ground_weapon", 7, 130], ["dat", "units", "air_weapon", 7, 130]]),
    "worker property absent": ([0xFF16], []),
    "worker property present": ([0xFF16], [flags(7, 8)]),
    "flying building property": ([0xFF17], [flags(7, 32)]),
    "transport zero capacity": ([0xFF18], [["dat", "units", "space_provided", 7, 0]]),
    "transport has capacity": ([0xFF18], []),
    "powerup property": ([0xFF19], [flags(7, 0x800)]),
    "subunit property": ([0xFF1A], [flags(7, 0x10)]),
    "mine byte zero": ([0xFF1B], []),
    "mine byte nonzero": ([0xFF1B], [["u8", "union_0xc0", 0, 1]]),
    "hero property missing": ([0xFF1C], []),
    "hero property present": ([0xFF1C], [flags(7, 0x40)]),
    "hold position action0": ([0xFF1D], [["dat", "units", "right_click_action", 7, 0], status(1)]),
    "hold position action1": ([0xFF1D], []),
    "hallucination implicit reject": ([], [status(HALLUCINATED)]),
    "hallucination allow token": ([0xFF1E], [status(HALLUCINATED)]),
    "disabled": ([0xFF22], []),
    "blank": ([0xFF23], []),
    "expansion false": ([0xFF24], [["expansion", 0]]),
    "expansion true": ([0xFF24], []),
}

# The wrapper rejections ("<domain> wrapper <test>"), probe loop over TABLES.
WRAPPER_TESTS = {
    "ID bound": [],
    "owner mismatch": [["u", "owner", 1]],
    "incomplete": [status(0)],
    "disabled": [["u", "lockdown_timer", 1]],
    "offset missing": [["offset", 0, 0]],
}

# The wrapper branches over a synthetic empty recipe of id 0 ("wrapper <name>": domain, patch).
WRAPPER_BRANCHES = {
    "research unavailable": ("research", [["researched", 0, 0], ["available", 0, 0]]),
    "research in progress": ("research", [["researched", 0, 0], ["research_progress", 0, 0, 1]]),
    "research already completed": ("research", []),
    "research cheat causes completed branch": ("research", [["researched", 0, 0], ["cheats", CHEAT_TECH]]),
    "technology use unavailable": ("technology_use", [["available", 0, 0]]),
    "technology use hallucinated": ("technology_use", [status(HALLUCINATED)]),
    "upgrade maximum reached": ("upgrades", [["upgrade", 0, 3]]),
    "upgrade in progress": ("upgrades", [["upgrade_progress", 0, 0, 1]]),
}

# The unit gate's checks before the recipe ("unit gate <kind>"), probe list over new(program=[]).
UNIT_GATE = {
    "out of range ID": [],
    "wrong producer owner": [["u", "owner", 1]],
    "producer incomplete": [status(2)],
    "producer status disabled": [status(0x403)],
    "producer timer117": [["u", "lockdown_timer", 1]],
    "producer timer119": [["u", "stasis_timer", 1]],
    "producer timer124": [["u", "maelstrom_timer", 1]],
    "unit unavailable": [["unit_available", 106, 0]],
    "fifth queue word105": [["u", "queue4", 105]],
    "fifth queue word106 allowed": [["u", "queue4", 106]],
    "hallucination status": [status(HALLUCINATED)],
    "missing offset zero": [["offset", 106, 0]],
}

# The unit gate's token cases ("requirement token <name>": program, patch), probe list `tests`.
UNIT_TOKENS = {
    "producer match": ([0xFF02, 7], []),
    "producer wrong existing parent": ([0xFF02, 7], [["u", "type", 64]]),
    "producer wrong missing parent": ([0xFF02, 7], [["u", "type", 64], completed(7, 0)]),
    "OR producer second match": ([0xFF02, 64, 0xFF01, 0xFF02, 7], []),
    "completed prerequisite missing": ([111], [completed(111, 0)]),
    "completed prerequisite cheat": ([111], [completed(111, 0), ["cheats", CHEAT_ADDON]]),
    "has unit completed0 all1": ([0xFF03, 111], [completed(111, 0)]),
    "has unit both0": ([0xFF03, 111], [completed(111, 0), ["count_all", 111, 0]]),
    "OR prerequisite second exists": ([111, 0xFF01, 112], [completed(111, 0)]),
    "AND second missing": ([111, 112], [completed(112, 0)]),
    "addon absent": ([0xFF04, 120], []),
    "addon matching": ([0xFF04, 120], [["u_other", "union_0xc0"], ["other", "type", 120]]),
    "addon wrong": ([0xFF04, 120], [["u_other", "union_0xc0"], ["other", "type", 119]]),
    "addon cheated": ([0xFF04, 120], [["cheats", CHEAT_ADDON]]),
    "grounded status present": ([0xFF05], []),
    "grounded status absent": ([0xFF05], [status(1)]),
    "not busy empty": ([0xFF07], []),
    "not busy queued": ([0xFF07], [["u", "queue0", 0]]),
    "not busy research byte": ([0xFF07], [tech_byte(0)]),
    "not busy upgrade byte": ([0xFF07], [upgrade_byte(0)]),
    "not constructing addon": ([0xFF08], []),
    "constructing addon": ([0xFF08], [["u", "secondary_order", 0x25], ["u_other", "build_unit"]]),
    "not teching": ([0xFF09], []),
    "teching": ([0xFF09], [tech_byte(0)]),
    "not upgrading": ([0xFF0A], []),
    "upgrading": ([0xFF0A], [upgrade_byte(0)]),
    "SCV constructing": ([0xFF0B], [["u", "order", 0x21]]),
    "nonSCV constructing order": ([0xFF0B], [["u", "type", 64], ["u", "order", 0x21]]),
    "no addon": ([0xFF0C], []),
    "has addon": ([0xFF0C], [["u_other", "union_0xc0"]]),
    "no missile": ([0xFF10], []),
    "has missile": ([0xFF10], [["u_other", "union_0xd0"]]),
    "not burrowed": ([0xFF11], []),
    "is burrowed rejected": ([0xFF11], [status(BURROWED)]),
    "burrow required absent": ([0xFF26], []),
    "burrow required present": ([0xFF26], [status(BURROWED)]),
    "disabled token": ([0xFF22], []),
    "blank token": ([0xFF23], []),
    "expansion true": ([0xFF24], []),
    "expansion false": ([0xFF24], [["expansion", 0]]),
    "tech researched": ([0xFF25, 35], []),
    "tech missing": ([0xFF25, 35], [["researched", 35, 0]]),
    "tech cheated": ([0xFF25, 35], [["researched", 35, 0], ["cheats", CHEAT_TECH]]),
    "immediate failure not rescued by following OR": ([0xFF05, 0xFF01, 7], [status(1)]),
    "has unit count u32 overflow": ([0xFF03, 111], [completed(111, 0xFFFFFFFF), ["count_all", 111, 1]]),
}

CARRIER, REAVER = 72, 83
# The fighter capacity upgrades of the hangar probes (Carrier capacity 43, Reaver capacity 36).
HANGAR_UPGRADE = {CARRIER: 43, REAVER: 36}
HANGAR = [0xFF0E]


def signed(v: int) -> int:
    return v - U32 if v >= U32 // 2 else v


def synthetic(program: list) -> list:
    """The probe's table with a program from word 1: header, program, end."""
    return [TABLE_HEADER] + list(program) + [END]


def check_probe(data: dict, script: str) -> None:
    digest = sha256_file(ROOT / script)
    if data["probe_script_sha256"] != digest:
        raise ExportError(f"{script} changed since the evidence was recorded; update this exporter's mirror")
    if not data["all_matched"] or data["helper_stubs"] or data["case_count"] != len(data["cases"]):
        raise ExportError(f"{script}: the evidence has unmatched cases or helper stubs")
    for c in data["cases"]:
        if not c.get("matched") or c.get("helper_stubs"):
            raise ExportError(f"{script}: case {c['kind']} is not a matched no-stub case")


def result(c: dict) -> dict:
    o = c["observed"]
    return {"ret": signed(o["return_u32"]), "err": o["error_u32"]}


def lookup(table: dict, key: str, kind: str) -> Any:
    if key not in table:
        raise ExportError(f"no fixture mirror for the probe case {kind!r}")
    return table[key]


def general_recipe_patch(name: str, recipe: dict, producer: int, level: int) -> list:
    """The patch of a shipped recipe wrapper case (probe loop over table["recipes"])."""
    values = [t["token"] for t in recipe["sections"][0]["decoded_tokens"]]
    properties = 0
    for token, bit in ((0xFF16, 8), (0xFF17, 0x20), (0xFF19, 0x800), (0xFF1A, 0x10)):
        if token in values:
            properties |= bit
    patch = [flags(producer, properties)]
    if any(x in values for x in (0xFF06, 0xFF12, 0xFF14)):
        patch.append(status(1))
    if 0xFF26 in values:
        patch.append(status(0x11))
    if 0xFF1B in values:
        patch.append(["u8", "union_0xc0", 0, 1])
    if producer == 42:
        patch.append(["upgrade", 24, 1])
    if name == "research":
        patch.append(["researched", recipe["id"], 0])
    if name == "upgrades":
        patch.append(["upgrade", recipe["id"], level])
    return patch


def general_cases() -> tuple[list, str]:
    data, digest = load_evidence("native-dat-requirements-probes.json")
    static, _ = load_evidence("native-dat-requirements-static.json")
    check_probe(data, GENERAL_PROBE)
    cases = []
    for c in data["cases"]:
        kind, inputs = c["kind"], c["fixture_inputs"]
        case = {"kind": kind}
        if kind.endswith(" offset initializer"):
            name = kind[len("original "):-len(" offset initializer")]
            case.update(call="offsets", domain=DOMAINS[name][0], offsets=c["observed"]["offsets"])
            cases.append(case)
            continue
        if kind.startswith("shipped original ") and kind.endswith(" recipe wrapper"):
            name = kind[len("shipped original "):-len(" recipe wrapper")]
            recipe = next(r for r in static["tables"][name]["recipes"] if r["id"] == inputs["id"])
            parents = [t["operands"][0] for t in recipe["sections"][0]["decoded_tokens"] if t["token"] == 0xFF02]
            producer = parents[0] if parents else GENERAL_FIXTURE["producer"]
            if producer != inputs["producer"]:
                raise ExportError(f"{kind} {inputs['id']}: producer {producer} differs from the evidence")
            case.update(call=DOMAINS[name][0], id=inputs["id"], producer=producer,
                        patch=general_recipe_patch(name, recipe, producer, inputs["current_upgrade_level"]))
        elif kind.startswith("general token "):
            words, patch = lookup(GENERAL_TOKENS, kind[len("general token "):], kind)
            if inputs["words"] != words:
                raise ExportError(f"{kind}: words differ from the evidence")
            case.update(call="evaluate", id=inputs["context"], table=list(words) + [END], at=0, patch=patch)
        elif kind == "Lurker context11 research exception":
            case.update(call="evaluate", id=11, producer=inputs["producer"], table=[0xFF0F, END], at=0,
                        patch=[["researched", 11, inputs["researched"]]])
        elif kind == "upgrade level marker selection":
            exists = inputs["exists"]
            patch = [["upgrade", 0, inputs["current_level"]]]
            patch += [completed(t, int(exists and t == inputs["required_selected"])) for t in (111, 112, 113)]
            case.update(call="evaluate", id=0, table=list(inputs["words"]) + [END], at=0, patch=patch)
        elif " wrapper " in kind and kind.split(" wrapper ")[0] in DOMAINS:
            name, test = kind.split(" wrapper ")
            call, capacity = DOMAINS[name]
            patch = list(lookup(WRAPPER_TESTS, test, kind))
            if test == "offset missing" and name == "research":
                patch.append(["researched", 0, 0])
            case.update(call=call, id=capacity if test == "ID bound" else 0, patch=patch)
        elif kind in ("research in-progress bit indexing", "upgrade in-progress bit indexing"):
            upgrade = kind.startswith("upgrade")
            ident = inputs["upgrade" if upgrade else "technology"]
            mask = 1 << (ident & 7)
            field = "upgrade_progress" if upgrade else "research_progress"
            case.update(call=field, id=ident, player=inputs["player"],
                        patch=[[field, inputs["player"], ident >> 3, mask if inputs["active"] else 0]])
        elif kind.startswith("wrapper "):
            name, patch = lookup(WRAPPER_BRANCHES, kind[len("wrapper "):], kind)
            case.update(call=DOMAINS[name][0], id=0, table=synthetic([]), patch=[["offset", 0, PROGRAM_AT]] + patch)
        elif kind == "order wrapper exception or busy restriction":
            case.update(call="orders", id=inputs["order"], producer=inputs["producer"], table=synthetic([]),
                        patch=[["offset", inputs["order"], PROGRAM_AT], status(inputs["status"]),
                               ["u", "order", inputs["primary"]]])
        else:
            raise ExportError(f"no fixture mirror for the probe case {kind!r}")
        case.update(result(c))
        cases.append(case)
    return cases, digest


def unit_cases() -> tuple[list, str]:
    data, digest = load_evidence("native-requirements-probes.json")
    static, _ = load_evidence("native-requirements-static.json")
    check_probe(data, UNIT_PROBE)
    recipes = {r["unit_id"]: r for r in static["unit_requirement_table"]["recipes"]}
    request = UNIT_FIXTURE["request"]
    cases = []
    for c in data["cases"]:
        kind, inputs = c["kind"], c["fixture_inputs"]
        case = {"kind": kind, "call": "units"}
        if kind == "original packed table lazy initialization":
            case.update(call="offsets", domain="units", offsets=c["observed"]["offsets_228"])
            cases.append(case)
            continue
        if kind.startswith("unit gate "):
            test = kind[len("unit gate "):]
            patch = lookup(UNIT_GATE, test, kind)
            case.update(id=228 if test == "out of range ID" else request, table=synthetic([]),
                        patch=[["offset", request, PROGRAM_AT]] + patch)
        elif kind == "shipped original unit recipe":
            recipe = recipes[inputs["requested_unit"]]
            patch = []
            for t in recipe["decoded_tokens"]:
                if t["token"] == 0xFF04:
                    patch += [["u_other", "union_0xc0"], ["other", "type", t["operands"][0]]]
                if t["token"] == 0xFF0E:
                    patch += [tech_byte(0), upgrade_byte(0)]
            case.update(id=inputs["requested_unit"], producer=inputs["producer_type"], patch=patch)
        elif kind.startswith("requirement token "):
            program, patch = lookup(UNIT_TOKENS, kind[len("requirement token "):], kind)
            if inputs["program_words"] != program:
                raise ExportError(f"{kind}: program differs from the evidence")
            case.update(id=request, table=synthetic(program), patch=[["offset", request, PROGRAM_AT]] + patch)
        elif kind == "hangar capacity boundary":
            producer = inputs["producer"]
            case.update(id=request, producer=producer, table=synthetic(HANGAR), patch=[
                ["offset", request, PROGRAM_AT], flags(producer, 0x40 if inputs["hero"] else 0),
                ["upgrade", HANGAR_UPGRADE[producer], inputs["upgrade_level"]], tech_byte(inputs["fighters"]),
                upgrade_byte(0)])
        elif kind == "hangar queued fighter types share counter":
            producer = inputs["producer"]
            case.update(id=request, producer=producer, table=synthetic(HANGAR), patch=[
                ["offset", request, PROGRAM_AT], ["upgrade", HANGAR_UPGRADE[producer], 0],
                tech_byte(inputs["existing_fighters"]), upgrade_byte(0), ["u", "queue0", inputs["queued_type"]]])
        elif kind == "fifth relative queue slot gate":
            head = inputs["head"]
            case.update(id=request, table=synthetic([]), patch=[
                ["offset", request, PROGRAM_AT], ["u", "queue_slot", head],
                ["u", f"queue{(head + 4) % 5}", inputs["relative4_id"]]])
        else:
            raise ExportError(f"no fixture mirror for the probe case {kind!r}")
        case.update(result(c))
        cases.append(case)
    return cases, digest


def general() -> dict:
    cases, digest = general_cases()
    header = {
        "source": "docs/research/evidence/native-dat-requirements-probes.json",
        "source_sha256": digest,
        "probe": GENERAL_PROBE,
        "target_sha256": TARGET_SHA256,
        "functions": ["0x407570 EvaluateRequirements", "0x407bb0 CanIssueOrder", "0x407dc0 CanResearch",
                      "0x407f90 CanUseTech", "0x408140 CanUpgrade", "0x407d50/0x407f20/0x4080d0/0x408280 offsets",
                      "0x4a0560/0x4a0590 in-progress bits"],
        "status": "original-code-executed (isolated evaluator, wrappers and leaf predicates)",
        "fixture": GENERAL_FIXTURE,
    }
    return cases_module(header, cases)


def units() -> dict:
    cases, digest = unit_cases()
    header = {
        "source": "docs/research/evidence/native-requirements-probes.json",
        "source_sha256": digest,
        "probe": UNIT_PROBE,
        "target_sha256": TARGET_SHA256,
        "functions": ["0x4b32a0 CanTrainUnit", "0x4b3230 unit offsets"],
        "status": "original-code-executed (isolated unit-production gate)",
        "fixture": UNIT_FIXTURE,
    }
    return cases_module(header, cases)


OUTPUTS = {"requirements_general.json": general, "requirements_units.json": units}


def main() -> int:
    logging.basicConfig(level=logging.INFO, format="%(message)s")
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--check", action="store_true", help="fail if a golden file is out of date")
    args = parser.parse_args()
    return update_goldens({GOLDEN / name: build() for name, build in OUTPUTS.items()}, args.check,
                          "tools/oracle/export_requirement_cases.py")


if __name__ == "__main__":
    sys.exit(main())
