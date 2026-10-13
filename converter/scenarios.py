"""Test scenarios for the phase 3 acceptance (docs/phase3.md, level C): a campaign map's
terrain with our own players and units, without triggers, briefing or map doodad units.

A spec (`tests/scenarios/<name>.json`, committed) names the base catalog mission, the slot
controllers and the units; the inputs and the frame count are read by the emulator driver.
`scenario_chk` rewrites the base CHK: OWNR from the spec, UNIT from the spec, THG2 keeps only
its sprites, TRIG and MBRF become empty. The emulator substitutes the same bytes for the
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
from .emit.defold import write_if_changed
from .formats import chk
from .analyze import profile
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
# The CHK unit states a spec unit may start in ("state": [...]): valid bits and values alike.
UNIT_STATES = {"cloaked": 0x1, "burrowed": 0x2}
EMPTIED = ("TRIG", "MBRF")


def load_spec(path: Path) -> dict:
    spec = json.loads(Path(path).read_text())
    spec.setdefault("name", Path(path).stem)
    return spec


def _chunks(data: bytes) -> list[tuple[str, bytes]]:
    return [(c.tag, data[c.payload_offset:c.payload_offset + c.size]) for c in chk.enumerate_chunks(data)]


def _units(spec: dict) -> bytes:
    out = []
    for serial, u in enumerate(spec["units"], start=1):
        state = 0
        for name in u.get("state", []):
            state |= UNIT_STATES[name]
        out.append(UNIT_RECORD.pack(serial, u["x"], u["y"], u["type"], 0, state, 0, u["owner"], 100, 100, 100, 0, 0,
                                    state, 0, 0))
    return b"".join(out)


def _sprites_only(thg2: bytes) -> bytes:
    keep = []
    for at in range(0, len(thg2) - THG2_RECORD + 1, THG2_RECORD):
        record = thg2[at:at + THG2_RECORD]
        if struct.unpack_from("<H", record, 8)[0] & THG2_SPRITE:
            keep.append(record)
    return b"".join(keep)


def scenario_chk(base: bytes, spec: dict) -> bytes:
    """The base CHK with the spec's players and units, no triggers, briefing or doodad units."""
    owners = bytearray(12)
    for slot, kind in spec["players"].items():
        owners[int(slot)] = CONTROLLERS[kind]
    out = []
    for tag, payload in _chunks(base):
        if tag == "OWNR":
            payload = bytes(owners)
        elif tag == "UNIT":
            payload = _units(spec)
        elif tag == "THG2":
            payload = _sprites_only(payload)
        elif tag in EMPTIED:
            payload = b""
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
    # The viewer's list of scenarios (viewer/viewer.script): name, base map, tileset, width, height.
    write_if_changed(out / "scenarios" / "index.txt", ("\n".join(index) + "\n").encode())
    return written
