"""Convert every catalog scenario to `generated/missions/<campaign>/<mission>.kpm`, with the
terrain-derived sections WALK/RGNS/CONT built from the tileset's CV5/VF4 (`ERA & 7`).

Also writes:
- `generated/missions/index.txt`: one line per mission in catalog order,
  `<name> <tileset> <width> <height>` (read by the product's mission list);
- `generated/reports/missions.json`: sections, ignored tags and mission sounds per map,
  and the physically present scenarios the native preflight rejects.
"""
from __future__ import annotations

import json
import logging
from pathlib import Path

from .catalog import EXTRA_SCENARIOS, MAPDATA_DAT, MAPDATA_TBL, SCENARIO_SUFFIX, decode_catalog
from .emit.files import write_if_changed
from .formats import chk
from .formats.palette import TILESETS
from .analyze import profile
from .kpf import kpm
from .mpq.stormlib import StormLibError
from .source import Source, SourceUnreadable

log = logging.getLogger(__name__)


def convert(resolver: Source, out: Path) -> dict:
    tables = chk.LoaderTables.load(resolver.profile.id)
    data_set = resolver.data_set_hash()
    catalog = decode_catalog(resolver.read(MAPDATA_DAT)[1], resolver.read(MAPDATA_TBL)[1])
    report = {"profile": resolver.profile.id, "missions": [], "rejected": []}
    index = []
    unreadable: list[str] = []
    read = _reader(resolver, unreadable)
    tilesets: dict[int, tuple[bytes, bytes]] = {}
    context = profile.Context.load(lambda p: resolver.read(p)[1], resolver.has)
    profiles = []
    for entry in catalog:
        scenario = chk.load(resolver.read(entry.chk_path)[1], tables, vcod_sha256=tables.vcod_sha256)
        sounds = kpm.mission_sounds(scenario, entry.directory, read, unreadable)
        era = scenario.u16("ERA ") & 7
        if era not in tilesets:
            name = TILESETS[era]
            tilesets[era] = (resolver.read(f"tileset\\{name}.cv5")[1], resolver.read(f"tileset\\{name}.vf4")[1])
        imported = kpm.import_terrain(scenario.u16("DIM "), scenario.u16("DIM ", 1), scenario.sections["MTXM"],
                                      *tilesets[era])
        container = kpm.build(scenario, sounds, profile_id=resolver.profile.id, data_set_hash=data_set,
                              tileset=tilesets[era], imported=imported)
        prof = profile.build(entry.name, scenario, context)
        profiles.append(prof)
        kpm.add_packs(container, list(prof.packs), analysis_inputs(container, data_set))
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
            chk.load(resolver.read(directory + SCENARIO_SUFFIX)[1], tables, vcod_sha256=tables.vcod_sha256)
        except chk.ChkError as exc:
            report["rejected"].append({"directory": directory, "reason": str(exc)})
        else:
            raise chk.ChkError(f"{directory}: expected the native preflight to reject it")
    report["unreadable_sounds"] = unreadable
    write_if_changed(out / "reports" / "analysis.json", json.dumps(summary(profiles), indent=1, sort_keys=True) + "\n")
    write_if_changed(out / "missions" / "index.txt", "\n".join(index) + "\n")
    write_if_changed(out / "reports" / "missions.json", json.dumps(report, indent=1, sort_keys=True) + "\n")
    return report


def _reader(resolver: Source, unreadable: list[str]):
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
        except (StormLibError, SourceUnreadable) as exc:
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
