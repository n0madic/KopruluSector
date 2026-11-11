"""The campaign's scenarios as the runtime reads them from the original data (ADR 0018): the catalog's CHKs, their
mission sounds and resource packs.

`load_catalog` reads every catalog scenario; `survey` adds the resource packs of each (converter/analyze/profile.py) and
checks that the prerelease scenarios outside the catalog are rejected natively, in memory: nothing is written. `scr-manifest`
runs it over the reference install so that the manifest holds every file these read, and `write_packs` records each
mission's packs in `defs/campaign/packs-<profile>.json`, which the runtime compiles (tools/defs_c.py MPAK) for the missions it
reads from their CHK (stage D4).
"""
from __future__ import annotations

import json
import logging
from dataclasses import dataclass
from pathlib import Path

from .analyze import profile
from .catalog import EXTRA_SCENARIOS, MAPDATA_DAT, MAPDATA_TBL, SCENARIO_SUFFIX, CatalogEntry, decode_catalog
from .emit.files import write_if_changed
from .formats import chk
from .kpf import kpm
from .mpq.stormlib import StormLibError
from .provenance import DEFS_ROOT
from .source import Source

log = logging.getLogger(__name__)


@dataclass(frozen=True)
class CatalogScenario:
    entry: CatalogEntry
    scenario: chk.Scenario
    sounds: list[kpm.MissionSound]  # the WAVS sounds in string-id order, the ones that exist and the ones that do not


@dataclass(frozen=True)
class Survey:
    packs: dict[str, list[str]]  # each catalog mission's resource packs by its directory, sorted
    rejected: dict[str, str]  # the prerelease scenarios outside the catalog the native preflight rejects: why
    unreadable_sounds: list[str]  # the install's damaged copies and the WAVs the native parser rejects


def catalog_scenarios(source: Source) -> list[tuple[CatalogEntry, chk.Scenario]]:
    """Every catalog scenario's CHK, in catalog order."""
    tables = chk.LoaderTables.load(source.profile.id)
    catalog = decode_catalog(source.read(MAPDATA_DAT)[1], source.read(MAPDATA_TBL)[1])
    return [(entry, chk.load(source.read(entry.chk_path)[1], tables, vcod_sha256=tables.vcod_sha256))
            for entry in catalog]


def load_catalog(source: Source, unreadable: list[str] | None = None) -> list[CatalogScenario]:
    """Every catalog scenario's CHK and mission sounds. `unreadable` gets the sounds the archives list but cannot read."""
    unreadable = [] if unreadable is None else unreadable
    read = _reader(source, unreadable)
    out = []
    for entry, scenario in catalog_scenarios(source):
        sounds = kpm.mission_sounds(scenario, entry.directory, read, unreadable)
        missing = sum(not s.exists for s in sounds)
        log.info("%s: v%d, %d sounds%s", entry.name, scenario.version, len(sounds), f" ({missing} missing)" if missing else "")
        out.append(CatalogScenario(entry, scenario, sounds))
    return out


def survey(source: Source) -> Survey:
    """The packs of each catalog mission, and the check that the native preflight rejects the extra scenarios."""
    tables = chk.LoaderTables.load(source.profile.id)
    unreadable: list[str] = []
    context = profile.Context.load(lambda p: source.read(p)[1], source.has)
    packs = {m.entry.directory: sorted(profile.build(m.entry.name, m.scenario, context).packs)
             for m in load_catalog(source, unreadable)}
    rejected = {}
    for directory in EXTRA_SCENARIOS:
        try:
            chk.load(source.read(directory + SCENARIO_SUFFIX)[1], tables, vcod_sha256=tables.vcod_sha256)
        except chk.ChkError as exc:
            rejected[directory] = str(exc)
        else:
            raise chk.ChkError(f"{directory}: expected the native preflight to reject it")
    log.info("%d missions, %d scenarios rejected by the native preflight, %d mission sounds unreadable or rejected",
             len(packs), len(rejected), len(unreadable))
    return Survey(packs, rejected, unreadable)


def packs_path(profile_id: str) -> Path:
    return DEFS_ROOT / "campaign" / f"packs-{profile_id}.json"


def load_packs(profile_id: str) -> dict[str, list[str]]:
    """Each campaign mission's resource packs by its directory (`write_packs`)."""
    return json.loads(packs_path(profile_id).read_text())["missions"]


def load_scenario_packs(profile_id: str) -> dict[str, list[str]]:
    """Each test scenario's resource packs by its name (`write_packs`)."""
    return json.loads(packs_path(profile_id).read_text())["scenarios"]


def write_packs(path: Path, profile_id: str, packs: dict[str, list[str]], scenarios: dict[str, list[str]]) -> None:
    """The packs of `survey`'s missions by directory and of the test scenarios by name, one a line."""
    def rows(table: dict[str, list[str]]) -> str:
        return ",\n".join(f"  {json.dumps(k)}: {json.dumps(p)}" for k, p in sorted(table.items(), key=lambda r: r[0].lower()))

    text = (
        "{\n"
        f' "comment": "The resource packs of each campaign mission and each test scenario (converter/analyze/profile.py '
        f'over its scenario and the data): the PACK of a mission the runtime reads from its CHK (ADR 0018, stage D4; '
        f'tools/defs_c.py MPAK), and of a test scenario, which kp_convert scenarios writes into its index (ADR 0019, '
        f'stage C1; SPAK). Written by python3 -m converter scr-manifest --data DIR.",\n'
        f' "profile": {json.dumps(profile_id)},\n'
        ' "missions": {\n' + rows(packs) + "\n },\n"
        ' "scenarios": {\n' + rows(scenarios) + "\n }\n}\n"
    )
    write_if_changed(path, text)


def _reader(source: Source, unreadable: list[str]):
    """Mission WAV reader. A copy the archives hold but StormLib cannot read whole (a disc copied with a bad read: the
    reference install's campaign\\protoss\\protoss10\\staredit\\wav\\PAB05ura.wav, docs/research/campaigns.md) is
    treated as missing and reported, as is one the native WAV parser rejects (kpm.mission_sounds). Neither changes the
    other sounds' indices, which are their WAV strings' positions."""
    def read(path: str) -> bytes | None:
        if not source.has(path):
            return None
        try:
            return source.read(path)[1]
        except StormLibError as exc:  # not MpqFileNotFound: `has` said the archives hold it
            log.warning("damaged mission sound, treated as missing: %s", exc)
            unreadable.append(path)
            return None
    return read
