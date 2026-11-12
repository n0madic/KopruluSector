"""Mission dependency profiles (plan §5.2): what a mission can need, with the reason for every
inclusion, and the resource packs (`PACK`) to load.

The profile is a conservative upper bound built from static data:
- unit types: preplaced UNIT/THG2 records, units created by triggers (actions 11, 44),
  units the AI closure names, and the units each human player can make (available in the
  mission's PUNI result and of the player's race); then subunits and infestation results
  through units.dat;
- AI: launches of Run AI Script (15/16) resolved by `0x494d20`, closed over jumps, calls and
  multirun (scdata/analyze/ai.py);
- resources: unit graphics -> flingy -> sprite -> image, weapons, construction images,
  overlays/LO files, the IScript closure of every image (images, sprites, flingies, sounds,
  weapons it can create), portraits and unit sounds;
- packs: the pack of every GRP, sound and portrait (scdata/packs.py). `effects` is always
  loaded: native code creates fixed images (selection circles 561-580, shield hit 424,
  damage overlays 450-493, spell effects) that no data reference names.
"""
from __future__ import annotations

import struct
from collections import defaultdict
from dataclasses import dataclass, field

from .. import packs
from ..derive import rules, unit_records
from ..formats import chk, dat, scripts, tbl
from . import ai, iscript, triggers

UNITS, IMAGES, SPRITES, FLINGIES, WEAPONS = 228, 999, 517, 209, 130
NONE_UNIT, NONE_WEAPON = 228, 130
SELECTION_CIRCLE_BASE = 561
RACE_GROUP = {0: 0x01, 1: 0x02, 2: 0x04}  # SIDE race -> staredit group bit (zerg, terran, protoss)
ANY_RACE = 3  # techdata/upgrades staredit_race of race-independent entries
# Units the engine creates outside the recipes: hatcheries spawn larvae, larva morphs pass
# through an egg, mutalisk morphs through a cocoon, the lurker morph through its egg.
NATIVE_TRANSITIONS = {35: {131, 132, 133}, 36: {35}, 59: {43}, 97: {38}}
HUMAN = 6  # OWNR controller: the player's production is limited only by availability
# Computer players produce what their AI closure names; rescuable and neutral players do not
# produce (their units are preplaced or created by triggers, both already listed).
ALWAYS_PACKS = {"effects": "hardcoded native images: selection circles 561-580, shield hit 424, "
                           "building damage overlays 450-493, spell and hit effects"}
PORTRAIT_GROUP_PACK = {"t": "terran", "u": "terran", "p": "protoss", "z": "zerg", "n": "neutral"}
UNIT_SOUND_COLUMNS = (("what_sound_start", "what_sound_end"), ("pissed_sound_start", "pissed_sound_end"),
                      ("yes_sound_start", "yes_sound_end"))


def load_recipes(profile_id: str = "bw-1.17.0.1") -> dict[int, set[int]]:
    """defs/requirements: unit -> producer types named by `ff02` in its recipe."""
    import json
    from pathlib import Path

    raw = json.loads((Path(__file__).resolve().parents[4] / "defs" / "requirements" / f"{profile_id}.json").read_text())
    out = {}
    for r in raw["recipes"]:
        words = [int(w, 16) for w in r["words"]]
        out[r["unit"]] = {words[i + 1] for i, w in enumerate(words[:-1]) if w == 0xFF02}
    return out


@dataclass
class Context:
    """Global data shared by all mission profiles."""
    columns: dict[str, dict[str, tuple[int, ...]]]
    image_paths: list[str | None]
    image_tbl: list[bytes]
    sfx_paths: list[str | None]
    iscript_data: bytes
    iscript_headers: dict[int, int]
    iscript_table: iscript.Table
    ai_table: ai.Table
    ai_bins: tuple[bytes, bytes]
    portrait_paths: dict[int, list[str]]  # portdata row -> existing SMKs
    associations: list[tuple[int, int, int]]
    recipes: dict[int, set[int]]  # unit -> producer types (ff02 tokens) of its native recipe
    closures: dict[int, iscript.Closure] = field(default_factory=dict)  # by header offset, shared by missions

    def col(self, table: str, name: str) -> tuple[int, ...]:
        return self.columns[table][name]

    def iscript_closure(self, header: int) -> iscript.Closure:
        """The IScript closure of a header; images and missions sharing it share the result,
        which callers only read."""
        cl = self.closures.get(header)
        if cl is None:
            cl = iscript.walk(self.iscript_table, self.iscript_data,
                              iscript.entry_points(self.iscript_table, self.iscript_data, header))
            self.closures[header] = cl
        return cl

    @classmethod
    def load(cls, read, has) -> "Context":
        from ..kpf import kpd

        layouts = dat.load_layouts()
        columns: dict[str, dict[str, tuple[int, ...]]] = {}
        for f in layouts:
            data = read(f.path)
            columns[f.table] = {}
            for c in f.columns:
                values = dat.values(f, data, c.name)
                full = [0] * (f.records * c.per_id)
                full[c.first_id * c.per_id : c.first_id * c.per_id + len(values)] = values
                columns[f.table][c.name] = tuple(full)
        image_tbl = tbl.decode_tbl(read("arr\\images.tbl"))
        isc = read("scripts\\iscript.bin")
        ps = kpd.portrait_set(read, has)
        portraits: dict[int, list[str]] = defaultdict(list)
        for row, entries in enumerate(ps.rows):
            for first, mask in entries:
                if first != kpd.NONE16:
                    portraits[row] += ps.paths[first : first + bin(mask).count("1")]
        return cls(columns, tbl.image_grp_paths(read("arr\\images.dat"), read("arr\\images.tbl")), image_tbl,
                   tbl.sfx_paths(read("arr\\sfxdata.dat"), read("arr\\sfxdata.tbl")), isc,
                   dict(scripts.iscript_headers(isc)), iscript.Table.load(), ai.Table.load(),
                   (read("scripts\\aiscript.bin"), read("scripts\\bwscript.bin")), dict(portraits),
                   rules.associations(read("arr\\units.dat")), load_recipes())


@dataclass
class Profile:
    """Everything a mission can need, each item with the reasons it was included."""
    mission: str
    units: dict[int, list[str]] = field(default_factory=lambda: defaultdict(list))
    images: dict[int, list[str]] = field(default_factory=lambda: defaultdict(list))
    sprites: set[int] = field(default_factory=set)
    flingies: set[int] = field(default_factory=set)
    weapons: set[int] = field(default_factory=set)
    sounds: set[int] = field(default_factory=set)
    upgrades: set[int] = field(default_factory=set)
    techs: set[int] = field(default_factory=set)
    portraits: set[str] = field(default_factory=set)
    packs: dict[str, list[str]] = field(default_factory=lambda: defaultdict(list))
    ai_roots: dict[str, list[str]] = field(default_factory=dict)  # script id -> launch sites
    ai_opcodes: set[str] = field(default_factory=set)
    ai_instructions: int = 0
    iscript_opcodes: set[str] = field(default_factory=set)
    conditions: set[int] = field(default_factory=set)
    actions: set[int] = field(default_factory=set)
    briefing_actions: set[int] = field(default_factory=set)
    next_scenarios: list[str] = field(default_factory=list)
    players: list[dict] = field(default_factory=list)

    def size(self) -> int:
        """Ranking measure for the M1 choice: distinct mechanisms the mission needs."""
        return (len(self.units) + len(self.ai_opcodes) + len(self.actions) + len(self.conditions)
                + len(self.iscript_opcodes) + len(self.techs) + len(self.upgrades))

    def report(self) -> dict:
        return {
            "mission": self.mission, "size": self.size(), "players": self.players,
            "units": {str(k): sorted(set(v)) for k, v in sorted(self.units.items())},
            "images": len(self.images), "sprites": len(self.sprites), "flingies": len(self.flingies),
            "weapons": sorted(self.weapons), "sounds": len(self.sounds), "portraits": sorted(self.portraits),
            "upgrades": sorted(self.upgrades), "techs": sorted(self.techs),
            "packs": {k: sorted(set(v))[:12] for k, v in sorted(self.packs.items())},
            "ai_roots": self.ai_roots, "ai_opcodes": sorted(self.ai_opcodes), "ai_instructions": self.ai_instructions,
            "iscript_opcodes": sorted(self.iscript_opcodes),
            "trigger_conditions": sorted(self.conditions), "trigger_actions": sorted(self.actions),
            "briefing_actions": sorted(self.briefing_actions), "next_scenarios": self.next_scenarios,
        }


def _unit_closure(ctx: Context, p: Profile) -> None:
    """Subunits and infestation results of every listed unit type."""
    sub1, sub2 = ctx.col("units", "subunit1"), ctx.col("units", "subunit2")
    infest = ctx.col("units", "infestation")
    work = list(p.units)
    while work:
        u = work.pop()
        linked = [(sub1[u], "subunit of"), (sub2[u], "subunit of")]
        if u < len(infest) and infest[u] and infest[u] < UNITS:
            linked.append((infest[u], "infestation result of"))
        for v, why in linked:
            if v < UNITS:
                if v not in p.units:
                    work.append(v)
                p.units[v].append(f"{why} {u}")


def _resources(ctx: Context, p: Profile) -> None:
    c = ctx.col
    for u, _ in list(p.units.items()):
        p.flingies.add(c("units", "graphics")[u])
        cons = c("units", "construction_animation")[u]
        if cons:
            p.images[cons].append(f"construction of unit {u}")
        for w in (c("units", "ground_weapon")[u], c("units", "air_weapon")[u]):
            if w != NONE_WEAPON:
                p.weapons.add(w)
        for lo, hi in UNIT_SOUND_COLUMNS:
            if u < 106 or lo.startswith("what"):
                a, b = c("units", lo)[u], c("units", hi)[u]
                if a:
                    p.sounds.update(range(a, b + 1))
        if u < 106 and c("units", "ready_sound")[u]:
            p.sounds.add(c("units", "ready_sound")[u])
        p.portraits.update(ctx.portrait_paths.get(c("units", "portrait")[u], ()))
    # Spells: castspell takes the weapon of the unit's order (orders.dat targeting 0x51b740);
    # include the weapons of every order whose technology is enabled.
    for order, tech in enumerate(c("orders", "technology_energy")):
        weapon = c("orders", "targeting")[order]
        if tech in p.techs and weapon < NONE_WEAPON:
            p.weapons.add(weapon)
    done_images: set[int] = set()
    while True:
        for w in list(p.weapons):
            p.flingies.add(c("weapons", "graphics")[w])
        for f in list(p.flingies):
            p.sprites.add(c("flingy", "sprite")[f])
        for s in list(p.sprites):
            p.images[c("sprites", "image")[s]].append(f"sprite {s}")
            if s >= 130:
                p.images[SELECTION_CIRCLE_BASE + c("sprites", "selection_circle_image")[s]].append(f"selection circle of sprite {s}")
        pending = [i for i in p.images if i not in done_images and i < IMAGES]
        if not pending:
            break
        for i in pending:
            done_images.add(i)
            header = ctx.iscript_headers.get(c("images", "iscript_id")[i])
            if header is None:
                continue
            cl = ctx.iscript_closure(header)
            p.iscript_opcodes |= cl.opcodes
            for j in cl.refs["image"]:
                p.images[j].append(f"iscript of image {i}")
            if cl.next_image:
                p.images[i + 1].append(f"imgulnextid of image {i}")
            if cl.gas_overlays:
                for j in range(430, 440):
                    p.images[j].append(f"creategasoverlays of image {i}")
            p.sprites |= cl.refs["sprite"]
            if cl.sprol:
                p.sprites.add(ctx.iscript_table.raw["sprol_goliath_sprite"])
            p.flingies |= cl.refs["flingy"]
            p.sounds |= cl.refs["sound"]
            p.weapons |= cl.refs["weapon"] - {NONE_WEAPON}


def _packs(ctx: Context, p: Profile) -> None:
    for name, why in ALWAYS_PACKS.items():
        p.packs[name].append(why)
    for i in p.images:
        path = ctx.image_paths[i] if i < IMAGES else None
        if path:
            p.packs[packs.pack_of_grp(path)].append(f"image {i}")
    for s in p.sounds:
        path = ctx.sfx_paths[s] if s < len(ctx.sfx_paths) else None
        if path:
            p.packs[packs.pack_of_sound(path)].append(f"sound {s}")
    for path in p.portraits:
        p.packs[PORTRAIT_GROUP_PACK[path.lower().removeprefix("portrait\\")[0]]].append(f"portrait {path}")


def build(name: str, scenario: chk.Scenario, ctx: Context) -> Profile:
    s = scenario.sections
    p = Profile(name)
    owners, sides = s["OWNR"], s["SIDE"]
    for player in range(8):
        p.players.append({"player": player, "controller": owners[player], "race": sides[player]})
    for rec in unit_records.decode(s["UNIT"]):
        if rec.type != unit_records.START_LOCATION and rec.type < UNITS:
            p.units[rec.type].append("UNIT record")
    for at in range(0, len(s["THG2"]), 10):
        sid, _x, _y, _owner, _unused, flags = struct.unpack_from("<HHHBBH", s["THG2"], at)
        if flags & 0x1000:
            p.sprites.add(sid)
        elif sid < UNITS:
            p.units[sid].append("THG2 unit")
    trig = triggers.decode(s["TRIG"])
    p.conditions = set(triggers.condition_histogram(trig))
    p.actions = set(triggers.action_histogram(trig))
    p.briefing_actions = set(triggers.action_histogram(triggers.decode(s["MBRF"])))
    for t in trig:
        for a in t.actions:
            if a.kind in triggers.UNIT_CREATING_ACTIONS and a.unit < UNITS:
                p.units[a.unit].append(f"trigger {a.trigger} action {a.index} creates it")
            if a.kind == triggers.SET_NEXT_SCENARIO:
                from ..kpf.kpm import map_string
                p.next_scenarios.append((map_string(s["STR "], a.string) or b"").decode("latin1"))
    # AI closure from every launch.
    roots = []
    for a in triggers.ai_launches(trig):
        sid = ai.script_id(a.number)
        p.ai_roots.setdefault(sid.decode("latin1"), []).append(f"trigger {a.trigger} action {a.index}")
        found = ai.resolve(*ctx.ai_bins, sid)
        if found:
            roots.append(found)
    cl = ai.closure(ctx.ai_table, ctx.ai_bins, roots)
    p.ai_opcodes, p.ai_instructions = cl.opcode_names(), len(cl.instructions)
    for u in cl.operand_values("unit"):
        if u < UNITS:
            p.units[u].append("named by the AI closure")
    p.upgrades |= cl.operand_values("upgrade")
    p.techs |= cl.operand_values("technology")
    # Human players: production closure over the native recipes from the units they start
    # with (or that triggers create), limited by the mission's availability; enabled tech
    # and upgrades of their race.
    effects = rules.effects_of(rules.rule_sections(s), ctx.associations)
    created = {a.unit for t in trig for a in t.actions if a.kind in triggers.UNIT_CREATING_ACTIONS}
    records = unit_records.decode(s["UNIT"])
    for slot in p.players:
        race = slot["race"]
        if slot["controller"] != HUMAN or race not in RACE_GROUP:
            continue
        player = slot["player"]
        start = {r.type for r in records if r.owner == player and r.type < UNITS} | (created & set(range(UNITS)))
        available = {u for u in range(UNITS) if effects.availability.get((player, u), 0)}
        for u, why in production_closure(ctx, start, available).items():
            p.units[u].append(f"player {player} can make it ({why})")
        for (field_, pl, i), v in effects.upgrade_levels.items():
            if pl == player and field_ == 0 and v and ctx.col("upgrades", "staredit_race")[i] in (race, ANY_RACE):
                p.upgrades.add(i)
        for (field_, pl, i), v in effects.tech_states.items():
            if pl == player and v and ctx.col("techdata", "staredit_race")[i] in (race, ANY_RACE):
                p.techs.add(i)
    _unit_closure(ctx, p)
    _resources(ctx, p)
    _packs(ctx, p)
    return p


def production_closure(ctx: "Context", start: set[int], available: set[int]) -> dict[int, str]:
    """Units reachable from `start` through native recipes (`ff02` producer tokens) and the
    hardcoded zerg transitions; returns unit -> producer description."""
    have = set(start)
    out: dict[int, str] = {}
    changed = True
    while changed:
        changed = False
        for unit, producers in ctx.recipes.items():
            if unit in have or unit not in available:
                continue
            by = [x for x in producers if x in have]
            if by or not producers:
                have.add(unit)
                out[unit] = f"recipe, producer {by[0]}" if by else "recipe without producer"
                changed = True
        for unit, sources in NATIVE_TRANSITIONS.items():
            if unit not in have and have & sources:
                have.add(unit)
                out[unit] = f"native transition from {sorted(have & sources)[0]}"
                changed = True
    return out

