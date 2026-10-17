"""TRIG/MBRF record decoding for the dependency analyzer.

Record layout (2,400 bytes, docs/research/campaigns.md, atlas P17/P40): 16 conditions of
20 bytes (type byte at +0x0f), 64 actions of 32 bytes from +320 (location +0, string +4,
WAV string +8, time +0x0c, player +0x10, number +0x14, unit type +0x18, action +0x1a,
modifier +0x1b, flags +0x1c), execution flags u32 at +2368 and 28 owner bytes at +2372.
Counts are static upper bounds: disabled actions and impossible conditions are included.
"""
from __future__ import annotations

import struct
from collections import Counter
from dataclasses import dataclass

RECORD = 2400
CONDITIONS, CONDITION_SIZE = 16, 20
ACTIONS, ACTIONS_OFFSET, ACTION_SIZE = 64, 320, 32
EXECUTION_FLAGS, OWNERS = 2368, 2372

# Gameplay action ids (TRIG table 0x50df68, atlas P40).
RUN_AI_SCRIPT, RUN_AI_SCRIPT_AT = 15, 16
CREATE_UNIT_WITH_PROPERTIES, CREATE_UNIT = 11, 44
SET_NEXT_SCENARIO = 41
TRANSMISSION, PLAY_WAV, TALKING_PORTRAIT = 7, 8, 29
UNIT_CREATING_ACTIONS = frozenset({CREATE_UNIT, CREATE_UNIT_WITH_PROPERTIES})
# Briefing action ids (table 0x503908, atlas P17).
BRIEFING_SHOW_PORTRAIT, BRIEFING_TALKING_PORTRAIT, BRIEFING_TRANSMISSION = 5, 7, 8

# Names of the TRIG condition and action ids (tables 0x50e058 and 0x50df68, atlas P40; BWAPI's names in snake case).
CONDITION_IDS = {name: k for k, name in enumerate((
    "none", "countdown_timer", "command", "bring", "accumulate", "kill", "command_the_most", "commands_the_most_at",
    "most_kills", "highest_score", "most_resources", "switch", "elapsed_time", "mission_briefing", "opponents",
    "deaths", "command_the_least", "command_the_least_at", "least_kills", "lowest_score", "least_resources", "score",
    "always", "never"))}
ACTION_IDS = {name: k for k, name in enumerate((
    "none", "victory", "defeat", "preserve_trigger", "wait", "pause_game", "unpause_game", "transmission", "play_wav",
    "display_text_message", "center_view", "create_unit_with_properties", "set_mission_objectives", "set_switch",
    "set_countdown_timer", "run_ai_script", "run_ai_script_at_location", "leaderboard_control",
    "leaderboard_control_at_location", "leaderboard_resources", "leaderboard_kills", "leaderboard_points",
    "kill_unit", "kill_unit_at_location", "remove_unit", "remove_unit_at_location", "set_resources", "set_score",
    "minimap_ping", "talking_portrait", "mute_unit_speech", "unmute_unit_speech", "leaderboard_computer_players",
    "leaderboard_goal_control", "leaderboard_goal_control_at_location", "leaderboard_goal_resources",
    "leaderboard_goal_kills", "leaderboard_goal_points", "move_location", "move_unit", "leaderboard_greed",
    "set_next_scenario", "set_doodad_state", "set_invincibility", "create_unit", "set_deaths", "order", "comment",
    "give_units_to_player", "modify_unit_hit_points", "modify_unit_energy", "modify_unit_shield_points",
    "modify_unit_resource_amount", "modify_unit_hangar_count", "pause_timer", "unpause_timer", "draw",
    "set_alliance_status", "disable_debug_mode", "enable_debug_mode"))}
# The raw field layouts: a condition (20 bytes) and an action (32 bytes) in CHK order.
CONDITION = struct.Struct("<IIIHBBBBxx")
CONDITION_FIELDS = ("location", "player", "number", "unit", "comparison", "type", "resource", "flags")
ACTION = struct.Struct("<IIIIIIHBBBxxx")
ACTION_FIELDS = ("location", "string", "wav", "time", "player", "number", "unit", "type", "modifier", "flags")
OWNER_COUNT = 27
# An ignored condition or action (flags bit 0x02): the runners skip it (0x4bded0, 0x4bdf10).
ENTRY_IGNORED = 0x02
CONDITION_FLAGS, ACTION_FLAGS = 0x11, 0x1C


@dataclass(frozen=True)
class Action:
    trigger: int
    index: int
    location: int
    string: int
    wav: int
    time: int
    player: int
    number: int
    unit: int
    kind: int
    modifier: int
    flags: int


@dataclass(frozen=True)
class Trigger:
    index: int
    conditions: tuple[int, ...]  # condition type bytes, zeros included
    actions: tuple[Action, ...]  # every slot with a nonzero action id
    execution_flags: int
    owners: bytes
    condition_flags: tuple[int, ...] = ()
    action_end: int = ACTIONS  # the first slot that ends the actions: type 0, not ignored

    def reachable_conditions(self) -> list[tuple[int, int]]:
        """(slot, type) of the conditions EvaluateTriggerConditions 0x4bded0 evaluates: ignored ones are skipped and
        the first type 0 ends them."""
        out = []
        for j, c in enumerate(self.conditions):
            if self.condition_flags and self.condition_flags[j] & ENTRY_IGNORED:
                continue
            if c == 0:
                break
            out.append((j, c))
        return out

    def reachable_actions(self) -> list[Action]:
        """The actions the runner 0x4bdf10 executes: before the end slot, not ignored."""
        return [a for a in self.actions if a.index < self.action_end and not a.flags & ENTRY_IGNORED]


def decode(payload: bytes) -> list[Trigger]:
    if len(payload) % RECORD:
        raise ValueError(f"trigger payload of {len(payload)} bytes is not a multiple of {RECORD}")
    out = []
    for t in range(len(payload) // RECORD):
        base = t * RECORD
        cond = [dict(zip(CONDITION_FIELDS, CONDITION.unpack_from(payload, base + j * CONDITION_SIZE)))
                for j in range(CONDITIONS)]
        conditions = tuple(c["type"] for c in cond)
        condition_flags = tuple(c["flags"] for c in cond)
        actions = []
        action_end = ACTIONS
        for j in range(ACTIONS):
            f = dict(zip(ACTION_FIELDS, ACTION.unpack_from(payload, base + ACTIONS_OFFSET + j * ACTION_SIZE)))
            if f["type"]:
                actions.append(Action(t, j, f["location"], f["string"], f["wav"], f["time"], f["player"], f["number"],
                                      f["unit"], f["type"], f["modifier"], f["flags"]))
            elif action_end == ACTIONS and not f["flags"] & ENTRY_IGNORED:
                action_end = j
        out.append(Trigger(t, conditions, tuple(actions), struct.unpack_from("<I", payload, base + EXECUTION_FLAGS)[0],
                           payload[base + OWNERS : base + RECORD], condition_flags, action_end))
    return out


def condition_histogram(triggers: list[Trigger]) -> Counter:
    return Counter(c for t in triggers for c in t.conditions if c)


def action_histogram(triggers: list[Trigger]) -> Counter:
    return Counter(a.kind for t in triggers for a in t.actions)


def ai_launches(triggers: list[Trigger]) -> list[Action]:
    return [a for t in triggers for a in t.actions if a.kind in (RUN_AI_SCRIPT, RUN_AI_SCRIPT_AT)]


# Order reads its destination as a full location number (0x4b65a0); Move Unit as the number's low byte less one
# (0x4b5840), so a nonzero number with a low byte of 0 is location 256 (atlas P40).
ORDER, MOVE_UNIT = ACTION_IDS["order"], ACTION_IDS["move_unit"]
LOCATIONS = 255
# Types outside the V1 profile (IMPLEMENTATION_PLAN.md Scope V1): no campaign map reaches them, so kp_core has no
# handler for them.
# TODO: when a map that needs them comes into scope (custom and UMS maps), implement Opponents (CountOpponents
# 0x4ba060), Modify Unit Resource Amount (0x4b5d10) and Modify Unit Hangar Count (0x4b5dc0) in kp_triggers.c and
# drop them from these sets.
UNSUPPORTED_CONDITIONS = frozenset({CONDITION_IDS["opponents"]})
UNSUPPORTED_ACTIONS = frozenset({ACTION_IDS["modify_unit_resource_amount"], ACTION_IDS["modify_unit_hangar_count"]})
NAMES = {"condition": {v: k for k, v in CONDITION_IDS.items()}, "action": {v: k for k, v in ACTION_IDS.items()}}


def validate(payload: bytes) -> None:
    """Rejects what the original's tables would index past (no bounds check in 0x4bded0/0x4bdf10): a reachable
    condition of a type above 23, a reachable action of a type above 59, an Order or Move Unit destination past the
    255 locations; and a reachable condition or action outside the V1 profile (UNSUPPORTED_CONDITIONS,
    UNSUPPORTED_ACTIONS). Entries the runners never reach (ignored, after the end) are left as the map has them.
    Raises ValueError."""
    for t in decode(payload):
        for j, c in t.reachable_conditions():
            if c >= len(CONDITION_IDS):
                raise ValueError(f"trigger {t.index} condition {j}: type {c} (the table has {len(CONDITION_IDS)})")
            if c in UNSUPPORTED_CONDITIONS:
                raise ValueError(f"trigger {t.index} condition {j}: {NAMES['condition'][c]} is outside the V1 profile")
        for a in t.reachable_actions():
            if a.kind >= len(ACTION_IDS):
                raise ValueError(f"trigger {t.index} action {a.index}: type {a.kind} (the table has {len(ACTION_IDS)})")
            if a.kind in UNSUPPORTED_ACTIONS:
                raise ValueError(f"trigger {t.index} action {a.index}: {NAMES['action'][a.kind]} is outside the V1 "
                                 "profile")
            if a.kind == ORDER and a.number > LOCATIONS:
                raise ValueError(f"trigger {t.index} action {a.index}: Order to location {a.number}")
            if a.kind == MOVE_UNIT and a.location and a.number and not a.number & 0xFF:
                raise ValueError(f"trigger {t.index} action {a.index}: Move Unit to location 256 (number {a.number})")
