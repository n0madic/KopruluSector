"""A language's speech in the original's game (localization's stage L5, ADR 0016): the mission's trigger actions
take the TIME of `converter speech`'s overlay (generated/speech/<loc>/missions/<c>/<m>.kpm, the base's TRIG records
with the language's TIME), so a scripted play records the game a language's transmissions make.

The original copies the map's TRIG records into the players' lists (owner expansion 0x4bdb00) when the game loads;
the patch runs at the game's first object step (STEP_OBJECTS), which a frame runs before the trigger scheduler
(0x4be500), so before any trigger ran; it is installed before state.install, whose dump of step 1 must see it. Each
list's node is matched, in the list's order, to the next record of the overlay equal to it apart from the actions'
TIME, and takes that record's TIME. A node with no such record stops the play: the overlay is not the loaded map's. The
briefing's MBRF is left as it is: the scripted plays leave the ready room by a key, the briefing's waits do not reach
the game."""
from __future__ import annotations

import logging
import sys
from pathlib import Path

from . import game
from .state import trigger_nodes

log = logging.getLogger(__name__)
REPO = Path(__file__).resolve().parents[3]
PLAYER_LISTS = 8
TRIGGER_RECORD = 2400  # CHK TRIG: 16 conditions of 20 bytes, 64 actions of 32, the execution flags and owners
NODE_RECORD = 8  # a node's record follows its two list pointers
CONDITIONS, ACTION, ACTIONS, ACTION_TIME = 16 * 20, 32, 64, 0x0C
COMPARED = CONDITIONS + ACTIONS * ACTION  # the conditions and actions; the execution flags and owners are the game's
TIME_OFFSETS = tuple(CONDITIONS + k * ACTION + ACTION_TIME for k in range(ACTIONS))


class SpeechError(RuntimeError):
    pass


def overlay_triggers(path: Path) -> list[bytes]:
    """The TRIG records of a speech overlay (a MISN container)."""
    if str(REPO) not in sys.path:
        sys.path.insert(0, str(REPO))
    from converter.kpf.container import parse

    payload = parse(path.read_bytes()).section("TRIG").payload
    if len(payload) % TRIGGER_RECORD:
        raise SpeechError(f"{path}: TRIG of {len(payload)} bytes is not whole records")
    return [payload[k:k + TRIGGER_RECORD] for k in range(0, len(payload), TRIGGER_RECORD)]


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
                raise SpeechError(f"player {player}'s trigger node {node:#x} matches no record of the overlay "
                                  f"after record {at}")
            at = k + 1
            nodes += 1
            for off in TIME_OFFSETS:
                want = records[k][off:off + 4]
                if m.read(node + NODE_RECORD + off, 4) != want:
                    m.write(node + NODE_RECORD + off, want)
                    written += 1
    return {"nodes": nodes, "times": written}


def install(session, overlay: Path) -> dict:
    """Patches the game's trigger lists at the game's first object step; the returned dict gets the counts."""
    records = overlay_triggers(overlay)
    m = session.m
    done: dict = {}

    def on_step():
        if done or m.r16(game.GAME_MODE) != 3:
            return
        done.update(patch(m, records))
        log.info("speech %s: %d trigger nodes, %d TIME fields written", overlay, done["nodes"], done["times"])

    session.hook(game.STEP_OBJECTS, on_step)
    return done
