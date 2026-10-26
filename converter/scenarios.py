"""Test scenarios for the phase 3 acceptance (docs/phase3.md, level C): a campaign map's
terrain with our own players, units and triggers, without the map's triggers, briefing or doodad units.

A spec (`tests/scenarios/<name>.json`, committed) names the base catalog mission, the slot
controllers and the units; the inputs and the frame count are read by the emulator driver.
`scenario_chk` rewrites the base CHK: OWNR from the spec, UNIT from the spec, THG2 keeps only
its sprites and gets the spec's doodad units (phase 5: doors and traps, which campaign maps place in THG2), MBRF becomes empty and TRIG holds the spec's triggers (none without "triggers"); the
spec's "locations" replace MRGN records and its trigger texts are appended to STR (phase 5,
docs/phase5.md); an action's "script" (Run AI Script's four-character id, phase 7) fills its number field. The emulator substitutes the same bytes for the
campaign map at load (tools/research/sc_emulator/scenario.py), and `convert` writes the
matching `generated/scenarios/<name>.kpm` through the mission pipeline, so both games start
from the same map. Only the spec is committed; the CHK is rebuilt from the retail archive.
"""
from __future__ import annotations

import json
import logging
import struct
from pathlib import Path

from .catalog import MAPDATA_DAT, MAPDATA_TBL, decode_catalog
from .emit.files import write_if_changed
from .formats import chk
from .analyze import profile
from .analyze import triggers as trig
from .formats.palette import TILESETS
from .kpf import kpm
from .mpq.profile import Resolver
from .pe import PeImage

log = logging.getLogger(__name__)

ROOT = Path(__file__).resolve().parents[1]
SPECS = ROOT / "tests" / "scenarios"
CONTROLLERS = {"inactive": 0, "rescuable": 3, "computer": 5, "human": 6, "neutral": 7}
UNIT_RECORD = struct.Struct("<IHHHHHHBBBBIHHII")
THG2_RECORD, THG2_SPRITE = 10, 0x1000
# A THG2 record (id, x, y, owner, unused, flags): a spec's "doodad_units" entry gives type, x, y, owner and the raw
# flags (0x8000 starts the doodad disabled: a door open, a trap off).
THG2 = struct.Struct("<HHHBBH")
# The CHK unit states a spec unit may start in ("state": [...]): valid bits and values alike.
UNIT_STATES = {"cloaked": 0x1, "burrowed": 0x2}
# Optional percentages of a spec unit ("hp": 10, ...) and their CHK valid-property bits.
UNIT_PERCENTS = (("hp", 0x2), ("shields", 0x4), ("energy", 0x8))
# A spec unit's "related": k (0-based spec unit) links it and unit k both ways, a Nydus Canal to its exit or an add-on
# to its building: the CHK relation flag and the other unit's serial.
RELATION_NYDUS, RELATION_ADDON, NYDUS_CANAL = 0x200, 0x400, 134
# A spec unit's resource amount ("resources": n, mineral fields, geysers and gas buildings) and hangar count
# ("hangar": n, Carriers and Reavers), and their CHK valid-property bits.
RESOURCES_VALID, HANGAR_VALID = 0x10, 0x20
EMPTIED = ("MBRF",)
LOCATION = struct.Struct("<iiiiHH")  # MRGN record: left, top, right, bottom, name string, elevation flags
# The player tech sections ("researched": {"slot": [tech, ...]} marks techs available and researched for a slot):
# available[12][n], researched[12][n], default available[n], default researched[n], use default[12][n].
TECH_SECTIONS = {"PTEC": 24, "PTEx": 44}
# the PTEC and PTEx layouts above, per player slot
PLAYER_SLOTS = 12
# A trigger action's AI script id (Run AI Script, Run AI Script At Location): four characters for its number field.
SCRIPT_FIELD = "script"


def load_spec(path: Path) -> dict:
    spec = json.loads(Path(path).read_text())
    spec.setdefault("name", Path(path).stem)
    return spec


def _chunks(data: bytes) -> list[tuple[str, bytes]]:
    return [(c.tag, data[c.payload_offset:c.payload_offset + c.size]) for c in chk.enumerate_chunks(data)]


def _units(spec: dict) -> bytes:
    out = []
    units = spec["units"]
    related = {k: u["related"] for k, u in enumerate(units) if "related" in u}
    for k, other in list(related.items()):
        if not 0 <= other < len(units) or other == k:
            raise ValueError(f"spec unit {k}: related unit {other} is not another spec unit")
        if related.setdefault(other, k) != k:  # both records name each other
            raise ValueError(f"spec unit {other}: related to both {related[other]} and {k}")
    for serial, u in enumerate(units, start=1):
        state = 0
        for name in u.get("state", []):
            state |= UNIT_STATES[name]
        valid = 0
        for name, bit in UNIT_PERCENTS:
            if name in u:
                valid |= bit
        if "resources" in u:
            valid |= RESOURCES_VALID
        if "hangar" in u:
            valid |= HANGAR_VALID
        relation = link = 0
        if serial - 1 in related:
            relation = RELATION_NYDUS if u["type"] == NYDUS_CANAL else RELATION_ADDON
            link = related[serial - 1] + 1
        out.append(UNIT_RECORD.pack(serial, u["x"], u["y"], u["type"], relation, state, valid, u["owner"], u.get("hp", 100),
                                    u.get("shields", 100), u.get("energy", 100), u.get("resources", 0), u.get("hangar", 0), state, 0, link))
    return b"".join(out)


def _sprites_only(thg2: bytes) -> bytes:
    keep = []
    for at in range(0, len(thg2) - THG2_RECORD + 1, THG2_RECORD):
        record = thg2[at:at + THG2_RECORD]
        if struct.unpack_from("<H", record, 8)[0] & THG2_SPRITE:
            keep.append(record)
    return b"".join(keep)


def _doodad_units(spec: dict) -> bytes:
    out = []
    for k, d in enumerate(spec.get("doodad_units", [])):
        if d["flags"] & THG2_SPRITE:
            raise ValueError(f"spec doodad unit {k}: flags {d['flags']:#x} make it a sprite")
        out.append(THG2.pack(d["type"], d["x"], d["y"], d["owner"], 0, d["flags"]))
    return b"".join(out)


def _researched(payload: bytes, techs: int, researched: dict) -> bytes:
    out = bytearray(payload)
    for slot, ids in researched.items():
        p = int(slot)
        if not 0 <= p < PLAYER_SLOTS:
            raise ValueError(f"researched: player slot {slot}")
        for t in ids:
            if t < techs:
                out[p * techs + t] = 1
                out[PLAYER_SLOTS * techs + p * techs + t] = 1
                out[2 * PLAYER_SLOTS * techs + 2 * techs + p * techs + t] = 0
    return bytes(out)


def _id(names: dict, value, what: str) -> int:
    if isinstance(value, str):
        if value not in names:
            raise ValueError(f"unknown {what} {value!r}")
        return names[value]
    return value


def _strings(payload: bytes, texts: list[bytes]) -> bytes:
    """STR with `texts` appended (ids count + 1, ...): u16 count, u16 offsets, the NUL-terminated strings."""
    count = struct.unpack_from("<H", payload, 0)[0]
    offsets = struct.unpack_from(f"<{count}H", payload, 2)
    shift = 2 * len(texts)
    data = payload[2 + 2 * count:]
    at = 2 + 2 * (count + len(texts)) + len(data)
    added = []
    for t in texts:
        added.append(at)
        at += len(t) + 1
    if at > 0x10000:
        raise ValueError("STR: the appended trigger texts pass the 16-bit offsets")
    return struct.pack(f"<H{count + len(texts)}H", count + len(texts), *(o + shift for o in offsets), *added) + data + \
        b"".join(t + b"\0" for t in texts)


def _triggers(spec: dict, string_count: int) -> tuple[bytes, list[bytes]]:
    """The spec's TRIG records and the texts they add to STR (a string field given as text)."""
    texts: list[bytes] = []

    def string(value) -> int:
        if isinstance(value, str):
            texts.append(value.encode("latin1"))
            return string_count + len(texts)
        return value

    out = []
    for k, t in enumerate(spec.get("triggers", [])):
        record = bytearray(trig.RECORD)
        if len(t.get("conditions", [])) > trig.CONDITIONS or len(t.get("actions", [])) > trig.ACTIONS:
            raise ValueError(f"trigger {k}: more than {trig.CONDITIONS} conditions or {trig.ACTIONS} actions")
        for j, c in enumerate(t.get("conditions", [])):
            unknown = set(c) - set(trig.CONDITION_FIELDS)
            if unknown:
                raise ValueError(f"trigger {k} condition {j}: unknown fields {sorted(unknown)}")
            f = {**c, "type": _id(trig.CONDITION_IDS, c["type"], "condition")}
            trig.CONDITION.pack_into(record, j * trig.CONDITION_SIZE, *(f.get(n, 0) for n in trig.CONDITION_FIELDS))
        for j, a in enumerate(t.get("actions", [])):
            unknown = set(a) - set(trig.ACTION_FIELDS) - {SCRIPT_FIELD}
            if unknown:
                raise ValueError(f"trigger {k} action {j}: unknown fields {sorted(unknown)}")
            f = {**a, "type": _id(trig.ACTION_IDS, a["type"], "action"), "string": string(a.get("string", 0)),
                 "wav": string(a.get("wav", 0))}
            if SCRIPT_FIELD in a:  # Run AI Script's four-byte id, as a campaign TRIG holds it in the number field
                if "number" in a or len(a[SCRIPT_FIELD].encode("latin1")) != 4:
                    raise ValueError(f"trigger {k} action {j}: script {a[SCRIPT_FIELD]!r} (four characters, no number)")
                f["number"] = struct.unpack("<I", f.pop(SCRIPT_FIELD).encode("latin1"))[0]
            trig.ACTION.pack_into(record, trig.ACTIONS_OFFSET + j * trig.ACTION_SIZE,
                                  *(f.get(n, 0) for n in trig.ACTION_FIELDS))
        struct.pack_into("<I", record, trig.EXECUTION_FLAGS, t.get("flags", 0))
        for owner in t["owners"]:
            if not 0 <= owner < trig.OWNER_COUNT:
                raise ValueError(f"trigger {k}: owner {owner}")
            record[trig.OWNERS + owner] = 1
        out.append(bytes(record))
    return b"".join(out), texts


def _locations(payload: bytes, locations: dict) -> bytes:
    """MRGN with the spec's records ("n": {"rect": [left, top, right, bottom], "elevation": flags}, n 1-based)."""
    out = bytearray(payload)
    for key, loc in locations.items():
        n = int(key)
        if not 1 <= n <= len(out) // LOCATION.size:
            raise ValueError(f"location {key}: MRGN holds {len(out) // LOCATION.size} records")
        LOCATION.pack_into(out, (n - 1) * LOCATION.size, *loc["rect"], loc.get("name", 0), loc.get("elevation", 0))
    return bytes(out)


def scenario_chk(base: bytes, spec: dict) -> bytes:
    """The base CHK with the spec's players, units, doodad units, triggers, locations and researched techs, no
    briefing or doodad units of the base."""
    owners = bytearray(12)
    for slot, kind in spec["players"].items():
        owners[int(slot)] = CONTROLLERS[kind]
    chunks = _chunks(base)
    strs = next(payload for tag, payload in chunks if tag == "STR ")
    triggers, texts = _triggers(spec, struct.unpack_from("<H", strs, 0)[0])
    out = []
    for tag, payload in chunks:
        if tag == "OWNR":
            payload = bytes(owners)
        elif tag == "UNIT":
            payload = _units(spec)
        elif tag == "THG2":
            payload = _sprites_only(payload) + _doodad_units(spec)
        elif tag == "TRIG":
            payload = triggers
        elif tag == "STR " and texts:
            payload = _strings(payload, texts)
        elif tag == "MRGN" and "locations" in spec:
            payload = _locations(payload, spec["locations"])
        elif tag in EMPTIED:
            payload = b""
        elif tag in TECH_SECTIONS and "researched" in spec:
            payload = _researched(payload, TECH_SECTIONS[tag], spec["researched"])
        out.append(tag.encode("latin1") + struct.pack("<I", len(payload)) + payload)
    return b"".join(out)


def base_chk(resolver: Resolver, spec: dict) -> bytes:
    catalog = decode_catalog(resolver.read(MAPDATA_DAT)[1], resolver.read(MAPDATA_TBL)[1])
    entry = next(e for e in catalog if e.name == spec["base"])
    return resolver.read(entry.chk_path)[1]


def convert(resolver: Resolver, out: Path) -> list[Path]:
    from .missions import analysis_inputs  # imports the image emitters; the emulator only needs scenario_chk

    tables = chk.LoaderTables.load(resolver.profile.id)
    vcod = PeImage.load(resolver.root / resolver.profile.executable.name).resource(*tables.vcod_resource)
    data_set = resolver.profile.data_set_hash()
    written = []
    index = []
    context = profile.Context.load(lambda p: resolver.read(p)[1], resolver.has)
    for path in sorted(SPECS.glob("*.json")):
        spec = load_spec(path)
        scenario = chk.load(scenario_chk(base_chk(resolver, spec), spec), tables, vcod=vcod)
        era = scenario.u16("ERA ") & 7
        name = TILESETS[era]
        tileset = (resolver.read(f"tileset\\{name}.cv5")[1], resolver.read(f"tileset\\{name}.vf4")[1])
        container = kpm.build(scenario, [], profile_id=resolver.profile.id, data_set_hash=data_set, tileset=tileset)
        kpm.add_packs(container, list(profile.build(spec["name"], scenario, context).packs),
                      analysis_inputs(container, data_set))
        target = out / "scenarios" / f"{spec['name']}.kpm"
        changed = write_if_changed(target, container.to_bytes())
        log.info("%s %s", "wrote" if changed else "same ", target)
        written.append(target)
        index.append(f"{spec['name']} {spec['base']} {era} {scenario.u16('DIM ')} {scenario.u16('DIM ', 1)}")
    # The product's list of scenarios (kp_app's --mission): name, base map, tileset, width, height.
    write_if_changed(out / "scenarios" / "index.txt", ("\n".join(index) + "\n").encode())
    return written
