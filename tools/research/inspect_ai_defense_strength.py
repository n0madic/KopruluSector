#!/usr/bin/env python3
"""Static check that no stock AI script makes a defense recipe without strength.

RunRegionalMilitaryProduction 0x4af930 loops while a region's need exceeds its credit, and FulfilStrengthRecipe
0x4ac3c0 credits the build list's types by their AI strength in the list's direction (ComputeTypeStrength
0x4a9000): a list whose every type has strength 0 there makes no progress and the original hangs inside the
frame (kp fails with the address, kp_ai_military.c recipe_stalled). The lists are filled only by the
defensebuild/defenseuse opcodes, so the hang needs one that names such a type. This walks every instruction of
aiscript.bin and bwscript.bin (inspect_ai.py's decoder, the pinned PyMS schema) and computes the strengths as
kp_strength.c does from units.dat and weapons.dat. Exit status 1 when an opcode names a zero-strength type.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import inspect_ai  # noqa: E402
from scdata.formats import dat  # noqa: E402

TYPES = 228
TYPE_NONE = 228
WEAPON_NONE = 130
SCV, DRONE, PROBE = 7, 41, 64
SPIDER_MINE, INTERCEPTOR, SCARAB = 13, 73, 85
FIREBAT, MUTALISK, ZEALOT = 32, 43, 65
SCOURGE, INFESTED_TERRAN = 47, 50
REAVER, WARBRINGER, CARRIER, GANTRITHOR = 83, 81, 72, 82
NO_STRENGTH = (35, 36, 59, 97)  # Larva, Egg, Cocoon, Lurker Egg
STRENGTH_SCALE, STRENGTH_DIVISOR = 758, 100
U32 = 0xFFFFFFFF
# The lists by opcode suffix and the strength FulfilStrengthRecipe credits for them (kp_ai_military.c fulfil: gg and
# ga for ground, ag and aa for an enemy in the air).
AIR_LISTS = ("ag", "aa")
DEFENSE_LABELS = tuple(f"defense{kind}_{lst}" for kind in ("build", "use") for lst in ("gg", "ag", "ga", "aa"))


def s32(v: int) -> int:
    return ((v & U32) ^ 0x80000000) - 0x80000000


def tdiv(a: int, b: int) -> int:
    """C's truncating division (kp_idiv64)."""
    q = abs(a) // abs(b)
    return q if (a < 0) == (b < 0) else -q


class Strengths:
    """kp_strength_init over the stock DAT files: [air, ground] per type."""

    def __init__(self, units: bytes, weapons: bytes):
        layouts = {layout.path: layout for layout in dat.load_layouts()}
        self.units, self.weapons = units, weapons
        self.ul, self.wl = layouts["arr\\units.dat"], layouts["arr\\weapons.dat"]
        self.table = [self.of(t) for t in range(TYPES)]

    def unit(self, name: str, t: int) -> int:
        return dat.value(self.ul, self.units, name, t)

    def weapon(self, name: str, w: int) -> int:
        return dat.value(self.wl, self.weapons, name, w)

    def type_strength(self, t: int, w: int) -> int:
        """ComputeTypeStrength 0x4a9000 (kp_strength.c type_strength) for a weapon w that exists."""
        shields = self.unit("shield_amount", t) if self.unit("shield_enabled", t) != 0 else 0
        life = (s32(self.unit("hit_points", t)) >> 8) + shields
        if life == 0:
            return 0
        damage, cooldown = self.weapon("damage_amount", w), self.weapon("weapon_cooldown", w)
        factor = self.weapon("damage_factor", w)
        rate = (((self.weapon("maximum_range", w) & U32) // (cooldown & U32)) * factor & U32) * damage & U32
        burst = tdiv(s32((damage * factor & U32) << 11), cooldown)
        total = (((burst * life) & U32) >> 8) + rate & U32
        return math.floor(math.sqrt(total) * STRENGTH_SCALE / STRENGTH_DIVISOR) % (1 << 32)

    @staticmethod
    def modifier(t: int, v: int) -> int:
        """ApplyStrengthTypeModifier 0x4a8f60."""
        if t in (SCV, DRONE, PROBE):
            return (v & U32) >> 2
        if t in (SPIDER_MINE, INTERCEPTOR, SCARAB):
            return 0
        if t in (FIREBAT, MUTALISK, ZEALOT):
            return v * 2 % (1 << 32)
        if t in (SCOURGE, INFESTED_TERRAN):
            return (v & U32) >> 4
        if t == REAVER:
            return (v & U32) // 10
        return v

    def weapon_type(self, t: int) -> int:
        if t in (CARRIER, GANTRITHOR):
            return INTERCEPTOR
        if t in (REAVER, WARBRINGER):
            return SCARAB
        sub = self.unit("subunit1", t)
        return t if sub == TYPE_NONE else sub

    def of(self, t: int) -> tuple[int, int]:
        a = g = 0
        if t not in NO_STRENGTH:
            wt = self.weapon_type(t)
            w = self.unit("air_weapon", wt)
            a = 1 if w == WEAPON_NONE else self.modifier(t, self.type_strength(t, w))
            w = self.unit("ground_weapon", wt)
            g = 1 if w == WEAPON_NONE else self.modifier(t, self.type_strength(t, w))
        return (0 if a == 1 and g > 1 else a), (0 if g == 1 and a > 1 else g)


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--data", type=Path, required=True,
                   help="inspect_campaigns.py's output: scripts/aiscript.bin, scripts/bwscript.bin, arr/units.dat, arr/weapons.dat")
    p.add_argument("--schema", type=Path, required=True, help="the pinned PyMS AIBIN.py")
    p.add_argument("--out", type=Path, help="write the evidence JSON here")
    args = p.parse_args()
    try:
        labels, params = inspect_ai.load_schema(args.schema)
    except ValueError as e:
        p.error(str(e))
    units, weapons = (args.data / "arr" / "units.dat").read_bytes(), (args.data / "arr" / "weapons.dat").read_bytes()
    strengths = Strengths(units, weapons)
    lists = {label: {"opcodes": 0, "types": set(), "weakest": None} for label in DEFENSE_LABELS}
    flagged, files = [], {"units.dat": sha256(units), "weapons.dat": sha256(weapons)}
    for filename, stride in inspect_ai.FILES:
        data = (args.data / "scripts" / filename).read_bytes()
        files[filename] = sha256(data)
        _table, _entries, instructions, _counts, _padding = inspect_ai.decode(data, stride, labels, params)
        for at, node in sorted(instructions.items()):
            label = node["label"]
            if label not in lists:
                continue
            count, t = node["values"]
            air = label.endswith(AIR_LISTS)
            strength = strengths.table[t][0 if air else 1] if t < TYPES else None
            row = lists[label]
            row["opcodes"] += 1
            row["types"].add(t)
            if strength is not None and (row["weakest"] is None or strength < row["weakest"][1]):
                row["weakest"] = (t, strength)
            if not strength:
                flagged.append({"file": filename, "offset": at, "label": label, "count": count, "type": t,
                                "strength": strength})
    result = {
        "scope": "Static: every decoded instruction of both script files (a superset of the campaigns' reachable code); "
                 "strengths as kp_strength_init computes them",
        "schema_sha256": inspect_ai.SCHEMA_SHA256,
        "schema_source": inspect_ai.SCHEMA_SOURCE,
        "files_sha256": files,
        "zero_air_strength_types": [t for t in range(TYPES) if strengths.table[t][0] == 0],
        "zero_ground_strength_types": [t for t in range(TYPES) if strengths.table[t][1] == 0],
        # The credited direction matters: a list's types with no strength in the other direction would stall it.
        "lists": {label: {"direction": "air" if label.endswith(AIR_LISTS) else "ground", "opcodes": row["opcodes"],
                          "types": sorted(row["types"]),
                          "weakest": None if row["weakest"] is None
                          else {"type": row["weakest"][0], "strength": row["weakest"][1]},
                          "zero_in_the_other_direction": sorted(
                              t for t in row["types"]
                              if t < TYPES and strengths.table[t][1 if label.endswith(AIR_LISTS) else 0] == 0)}
                  for label, row in lists.items()},
        "strengths": [list(row) for row in strengths.table],
        "defense_opcodes": sum(row["opcodes"] for row in lists.values()),
        "zero_strength_opcodes": flagged,
    }
    text = json.dumps(result, indent=2) + "\n"
    if args.out:
        args.out.write_text(text)
    print(json.dumps({"defense_opcodes": result["defense_opcodes"], "zero_strength_opcodes": len(flagged),
                      "weakest": {label: row["weakest"] for label, row in result["lists"].items()}}))
    return 1 if flagged else 0


if __name__ == "__main__":
    sys.exit(main())
