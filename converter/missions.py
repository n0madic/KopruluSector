"""Convert every catalog scenario to `generated/missions/<campaign>/<mission>.kpm`, with the
terrain-derived sections WALK/RGNS/CONT built from the tileset's CV5/VF4 (`ERA & 7`).

Also writes:
- `generated/missions/index.txt`: one line per mission in catalog order,
  `<name> <tileset> <width> <height>` (read by the map viewer);
- `generated/reports/missions.json`: sections, ignored tags and mission sounds per map,
  and the physically present scenarios the native preflight rejects.
"""
from __future__ import annotations

import json
import logging
from pathlib import Path

from .catalog import EXTRA_SCENARIOS, MAPDATA_DAT, MAPDATA_TBL, SCENARIO_SUFFIX, decode_catalog
from .emit.defold import write_if_changed
from .formats import chk
from .formats.palette import TILESETS
from .analyze import profile
from .emit import debug_overlays, minimap
from .pe import PeImage
from .formats import tileset
from .kpf import kpm
from .mpq.profile import Resolver
from .mpq.stormlib import StormLibError
from .pe import PeImage

log = logging.getLogger(__name__)


def convert(resolver: Resolver, out: Path) -> dict:
    tables = chk.LoaderTables.load(resolver.profile.id)
    vcod = PeImage.load(resolver.root / resolver.profile.executable.name).resource(*tables.vcod_resource)
    data_set = resolver.profile.data_set_hash()
    catalog = decode_catalog(resolver.read(MAPDATA_DAT)[1], resolver.read(MAPDATA_TBL)[1])
    report = {"profile": resolver.profile.id, "missions": [], "rejected": []}
    index = []
    unreadable: list[str] = []
    read = _reader(resolver, unreadable)
    tilesets: dict[int, tuple[bytes, bytes]] = {}
    minimap_sources: dict = {}  # per era the minimap terrain sources, and the interface colour (_minimap)
    context = profile.Context.load(lambda p: resolver.read(p)[1], resolver.has)
    profiles = []
    for entry in catalog:
        scenario = chk.load(resolver.read(entry.chk_path)[1], tables, vcod=vcod)
        sounds = kpm.mission_sounds(scenario, entry.directory, read, unreadable)
        era = scenario.u16("ERA ") & 7
        if era not in tilesets:
            name = TILESETS[era]
            tilesets[era] = (resolver.read(f"tileset\\{name}.cv5")[1], resolver.read(f"tileset\\{name}.vf4")[1])
        imported = kpm.import_terrain(scenario.u16("DIM "), scenario.u16("DIM ", 1), scenario.sections["MTXM"],
                                      *tilesets[era])
        container = kpm.build(scenario, sounds, profile_id=resolver.profile.id, data_set_hash=data_set,
                              tileset=tilesets[era], imported=imported)
        image, idx = _minimap(resolver, scenario, era, tilesets[era], imported[0], minimap_sources)
        write_if_changed(out / "missions" / f"{entry.name}_minimap.png", image)
        write_if_changed(out / "missions" / f"{entry.name}_minimap.idx", idx)
        prof = profile.build(entry.name, scenario, context)
        profiles.append(prof)
        kpm.add_packs(container, list(prof.packs), analysis_inputs(container, data_set))
        overlay = out / "viewer" / entry.name.replace("/", "_")
        write_if_changed(overlay.with_name(overlay.name + "_walk.png"), debug_overlays.walk_png(
            scenario.u16("DIM "), scenario.u16("DIM ", 1), container.section("WALK").payload))
        write_if_changed(overlay.with_name(overlay.name + "_regions.png"),
                         debug_overlays.regions_png(container.section("RGNS").payload))
        write_if_changed(out / "reports" / "profiles" / f"{entry.name}.json",
                         json.dumps(prof.report(), indent=1, sort_keys=True) + "\n")
        path = out / "missions" / f"{entry.name}.kpm"
        changed = write_if_changed(path, container.to_bytes())
        index.append(f"{entry.name} {scenario.u16('ERA ') & 7} {scenario.u16('DIM ')} {scenario.u16('DIM ', 1)}")
        report["missions"].append({"catalog_index": entry.index, **kpm.manifest_entry(entry.name, scenario, sounds)})
        missing = sum(not s.exists for s in sounds)
        log.info("%s %s: v%d, %d sounds%s", "wrote" if changed else "same ", entry.name, scenario.version,
                 len(sounds), f" ({missing} missing)" if missing else "")
    for directory in EXTRA_SCENARIOS:
        try:
            chk.load(resolver.read(directory + SCENARIO_SUFFIX)[1], tables, vcod=vcod)
        except chk.ChkError as exc:
            report["rejected"].append({"directory": directory, "reason": str(exc)})
        else:
            raise chk.ChkError(f"{directory}: expected the native preflight to reject it")
    report["unreadable_sounds"] = unreadable
    write_if_changed(out / "reports" / "analysis.json", json.dumps(summary(profiles), indent=1, sort_keys=True) + "\n")
    write_if_changed(out / "missions" / "index.txt", "\n".join(index) + "\n")
    write_if_changed(out / "reports" / "missions.json", json.dumps(report, indent=1, sort_keys=True) + "\n")
    return report


def _reader(resolver: Resolver, unreadable: list[str]):
    """Mission WAV reader. A file the archives list but StormLib cannot decompress is
    treated as missing and reported (as is one the native WAV parser rejects, by
    kpm.mission_sounds): campaign\\protoss\\protoss10\\staredit\\wav\\PAB05ura.wav
    in the retail StarCraft.mpq has sectors 218-219 of 244 that fail to decompress (the
    research extraction failed on it too, evidence/campaign-media.json)."""
    def read(path: str) -> bytes | None:
        if not resolver.has(path):
            return None
        try:
            return resolver.read(path)[1]
        except StormLibError as exc:
            log.warning("unreadable mission sound, treated as missing: %s", exc)
            unreadable.append(path)
            return None
    return read


def analysis_inputs(container, data_set: bytes) -> bytes:
    """What the PACK producer read: the mission's canonical sections and the data set."""
    return data_set + b"".join(sec.fourcc + sec.payload for sec in container.sections if sec.section_class == 0)


def summary(profiles: list[profile.Profile]) -> dict:
    """Campaign-wide scope and the M1 choice: the mission with the smallest dependency set."""
    ranking = sorted(profiles, key=lambda p: (p.size(), p.mission))
    union = lambda attr: sorted(set().union(*(getattr(p, attr) for p in profiles)))  # noqa: E731
    return {
        "measure": "unit types + AI opcode types + trigger condition and action types + IScript opcode types "
                   "+ enabled techs + enabled upgrades (converter/analyze/profile.py Profile.size)",
        "ranking": [{"mission": p.mission, "size": p.size(), "units": len(p.units), "ai_opcodes": len(p.ai_opcodes),
                     "trigger_actions": len(p.actions), "trigger_conditions": len(p.conditions),
                     "packs": sorted(p.packs)} for p in ranking],
        "m1": ranking[0].mission,
        "scope": {
            "unit_types": union("units"), "ai_opcodes": union("ai_opcodes"),
            "ai_roots": sorted(set().union(*(p.ai_roots for p in profiles))),
            "trigger_conditions": union("conditions"), "trigger_actions": union("actions"),
            "briefing_actions": union("briefing_actions"), "iscript_opcodes": union("iscript_opcodes"),
            "techs": union("techs"), "upgrades": union("upgrades"), "packs": union("packs"),
        },
    }


UI_COLOUR_KEY = "ui colour"  # the minimap source cache's entry besides the eras'


def _minimap(resolver: Resolver, scenario: chk.Scenario, era: int, cv5_vf4: tuple[bytes, bytes], ids: list[int],
             cache: dict) -> tuple[bytes, bytes]:
    """`<mission>_minimap.png` and `.idx`: the native minimap terrain (converter/emit/minimap.py) of the imported
    megatile ids, as an image and as the indices the presentation's fog darkens."""
    if UI_COLOUR_KEY not in cache:  # the interface colour does not depend on the era
        cache[UI_COLOUR_KEY] = minimap.ui_colour(PeImage(resolver.executable()), minimap.UNEXPLORED_ENTRY)
    if era not in cache:
        name = TILESETS[era]
        raw = {ext: resolver.read(f"tileset\\{name}.{ext}")[1] for ext in ("vx4", "vr4", "wpe")}
        ts = tileset.decode(cv5_vf4[0], raw["vx4"], raw["vr4"], cv5_vf4[1])
        rgb = minimap.tileset_rgb(raw["wpe"])
        cycle = minimap.cycling(name)
        unexplored = minimap.nearest_static(rgb, cycle, cache[UI_COLOUR_KEY])
        cache[era] = (ts, rgb, minimap.palette_map(rgb, cycle), unexplored)
    ts, rgb, colours, unexplored = cache[era]
    width, height = scenario.u16("DIM "), scenario.u16("DIM ", 1)
    w, h, pixels = minimap.render(ids, width, height, ts, colours)
    return minimap.png(w, h, pixels, rgb), minimap.indices(w, h, pixels, unexplored)
