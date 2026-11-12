"""A language's speech in the original's game (localization's stage L5, ADR 0016; ADR 0019, stage C2): the mission's
trigger actions take the TIME of the language's map in a StarCraft: Remastered install
(locales/<loc>/Assets/campaign/<c>/<m>/staredit/scenario.chk), so a scripted play records the game a language's
transmissions make.

The records are the 1.17 map's (the game this emulator runs) with the language's TIME merged as Koprulu merges it
(kp_host_game_speech): an action alike but for its TIME takes the language's, one that differs in more keeps the map's
whole; a record that differs in its conditions, flags or actions stops the play unless defs/l10n/locales.json's
`speech` excepts the mission (Remastered's own fixes, `expterran/terran03`).

The original copies the map's TRIG records into the players' lists (owner expansion 0x4bdb00) when the game loads;
the patch runs at the game's first object step (STEP_OBJECTS), which a frame runs before the trigger scheduler
(0x4be500), so before any trigger ran; it is installed before state.install, whose dump of step 1 must see it. Each
list's node is matched, in the list's order, to the next merged record equal to it apart from the actions' TIME, and
takes that record's TIME. A node with no such record stops the play: the map is not the loaded one. The briefing's
MBRF is left as it is: the scripted plays leave the ready room by a key, the briefing's waits do not reach the game."""
from __future__ import annotations

import json
import logging
import sys
from pathlib import Path

from . import game
from .process import DEFAULT_EXE
from .state import trigger_nodes

log = logging.getLogger(__name__)
REPO = Path(__file__).resolve().parents[3]
RESEARCH = REPO / "tools" / "research"
LOCALES = REPO / "defs" / "l10n" / "locales.json"
LANGUAGE_MAP = "locales/{locale}/Assets/campaign/{mission}/staredit/scenario.chk"
PLAYER_LISTS = 8
TRIGGER_RECORD = 2400  # CHK TRIG: 16 conditions of 20 bytes, 64 actions of 32, the execution flags and owners
NODE_RECORD = 8  # a node's record follows its two list pointers
CONDITIONS, ACTION, ACTIONS, ACTION_TIME, TIME_SIZE = 16 * 20, 32, 64, 0x0C, 4
COMPARED = CONDITIONS + ACTIONS * ACTION  # the conditions and actions; the execution flags and owners are the game's
TIME_OFFSETS = tuple(CONDITIONS + k * ACTION + ACTION_TIME for k in range(ACTIONS))


class SpeechError(RuntimeError):
    pass


def time_merged(base: bytes, theirs: bytes) -> tuple[bytes, int, list[str]]:
    """(the base's records with the language's TIME, the actions whose TIME changed, the differences other than TIME):
    an action that differs from the base's in more than its TIME keeps the base's whole."""
    if len(base) != len(theirs) or len(base) % TRIGGER_RECORD:
        raise SpeechError(f"the language's TRIG of {len(theirs)} bytes for the map's {len(base)}")
    out, changed, differ = bytearray(base), 0, []
    for r in range(0, len(base), TRIGGER_RECORD):
        if base[r:r + CONDITIONS] != theirs[r:r + CONDITIONS]:
            differ.append(f"record {r // TRIGGER_RECORD} conditions")
        tail = r + COMPARED
        if base[tail:r + TRIGGER_RECORD] != theirs[tail:r + TRIGGER_RECORD]:
            differ.append(f"record {r // TRIGGER_RECORD} flags")
        for k in range(ACTIONS):
            at = r + CONDITIONS + k * ACTION
            time = at + ACTION_TIME
            if base[at:time] + base[time + TIME_SIZE:at + ACTION] != theirs[at:time] + theirs[time + TIME_SIZE:at + ACTION]:
                differ.append(f"record {r // TRIGGER_RECORD} action {k}")
            elif base[time:time + TIME_SIZE] != theirs[time:time + TIME_SIZE]:
                out[time:time + TIME_SIZE] = theirs[time:time + TIME_SIZE]
                changed += 1
    return bytes(out), changed, differ


def excepted(locale: str, mission: str) -> bool:
    """locales.json's `speech` lets the language's map of the mission differ in more than TIME."""
    entries = json.loads(LOCALES.read_text(encoding="utf-8")).get("speech", ())
    return any(e["locale"] == locale and e["mission"] == mission for e in entries)


def merged_triggers(locale: str, mission: str, scr: Path, original: Path) -> list[bytes]:
    """The 1.17 map's TRIG records of `mission` (`terran/terran01`) with the TIME of the language's map in `scr`."""
    if str(RESEARCH) not in sys.path:  # scdata
        sys.path.insert(0, str(RESEARCH))
    from scdata.casc.source import ScrSource
    from scdata.formats import chk
    from scdata.missions import catalog_scenarios
    from scdata.mpq.profile import Resolver

    with Resolver(original) as r:
        base = next((s.sections["TRIG"] for e, s in catalog_scenarios(r) if e.name == mission), None)
        tables = chk.LoaderTables.load(r.profile.id)
    if base is None:
        raise SpeechError(f"{original}'s catalog has no mission {mission}")
    want = LANGUAGE_MAP.format(locale=locale, mission=mission).lower()
    with ScrSource(scr) as source:
        name = next((n for n in source.storage.names() if n.lower() == want), None)
        if name is None:
            raise SpeechError(f"{scr} has no map of {mission} in {locale}")
        theirs = chk.load(source.storage.read(name), tables, vcod_sha256=tables.vcod_sha256).sections.get("TRIG", b"")
    merged, changed, differ = time_merged(base, theirs)
    if differ and not excepted(locale, mission):
        raise SpeechError(f"{locale} {mission}: {len(differ)} records differ from the map's in more than TIME, the first "
                          f"{differ[0]} (defs/l10n/locales.json's speech lists a mission's own fixes)")
    log.info("speech %s %s: %d actions' TIME", locale, mission, changed)
    return [merged[k:k + TRIGGER_RECORD] for k in range(0, len(merged), TRIGGER_RECORD)]


def untimed(record: bytes) -> bytes:
    b = bytearray(record[:COMPARED])
    for off in TIME_OFFSETS:
        b[off:off + 4] = b"\0\0\0\0"
    return bytes(b)


def patch(m, records: list[bytes]) -> dict:
    """Writes the records' TIME into the players' trigger lists; returns the counts."""
    keys = [untimed(r) for r in records]
    nodes = written = 0
    for player in range(PLAYER_LISTS):
        at = 0
        for node in trigger_nodes(m, player):
            # TODO: two records apart only in their owners match the same node; one the player lacks could give its TIME
            # (not met: ruRU's 71 plays equal Koprulu's, which lays each record's own TIME, at every step)
            mine = untimed(m.read(node + NODE_RECORD, COMPARED))
            k = next((k for k in range(at, len(keys)) if keys[k] == mine), None)
            if k is None:
                raise SpeechError(f"player {player}'s trigger node {node:#x} matches no record of the language's "
                                  f"map after record {at}")
            at = k + 1
            nodes += 1
            for off in TIME_OFFSETS:
                want = records[k][off:off + 4]
                if m.read(node + NODE_RECORD + off, 4) != want:
                    m.write(node + NODE_RECORD + off, want)
                    written += 1
    return {"nodes": nodes, "times": written}


def install(session, locale: str, mission: str, scr: Path, original: Path = DEFAULT_EXE.parent) -> dict:
    """Patches the game's trigger lists at the game's first object step with `mission`'s TIME in `locale`; the returned
    dict gets the counts."""
    records = merged_triggers(locale, mission, scr, original)
    m = session.m
    done: dict = {}

    def on_step():
        if done or m.r16(game.GAME_MODE) != 3:
            return
        done.update(patch(m, records))
        log.info("speech %s %s: %d trigger nodes, %d TIME fields written", locale, mission, done["nodes"], done["times"])

    session.hook(game.STEP_OBJECTS, on_step)
    return done
