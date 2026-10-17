#!/usr/bin/env python3
"""Decodes a simulation state dump of the original (tools/research/sc_emulator/state.py)
into the normalized state model that Koprulu's simulation also prints
(kp_core game:dump, native/kp_core/src/core/kp_dump.c): one line per record, `<category> <key> <field>=<value> ...`.

Pointers become record numbers: units 1..1700 (CUnit 0x5da3a0 + (n-1)*0x150), sprites
0..2499, images in the shared key space of game/images/core.lua, order nodes 0..1999,
path records 1..1024 (the pool [0x69b574] + (k-1)*0x80); null is 0 for units and paths and
-1 for the others. Lists are walked in native order. The field
layout of CUnit follows the atlas (P08/P09, [docs/research/native-unit-creation.md]) and the
BWAPI 1.16.1 structure it confirms.

Usage: python3 tools/native_state.py STATE [--category unit] [--fields a,b,...]
"""
from __future__ import annotations

import argparse
import logging
import struct
import zlib
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools" / "research"))

from sc_emulator.state import State  # noqa: E402

log = logging.getLogger("native_state")

UNIT_BASE, UNIT_SIZE, UNITS = 0x5DA3A0, 0x150, 1700
SPRITE_BASE, SPRITE_SIZE, SPRITES = 0x5AF238, 0x24, 2500
ORDER_BASE, ORDER_SIZE, ORDERS = 0x5A52C0, 0x14, 2000
IMAGE_SIZE = 0x40
IMAGE_REDRAW = 0x01  # image flag of the original's dirty-rectangle redraw (native/kp_core/src/core/kp_images.c)
IMAGE_POOLS = ((0x544698, 5000, 0), (0x5928A0, 12, 5000), (0x592C50, 80, 5012), (0x594070, 64, 5092),
               (0x595078, 64, 5156))  # game/images/core.lua key space
UNIT_LISTS = (("active", 0x665AE0), ("pending", 0x665AE8), ("free", 0x665AF0), ("hidden", 0x665AF8),
              ("special", 0x665B00))
PLAYER_LISTS, PLAYERS = 0x665B08, 12
UNIT_ALLOCATED = 0x665B38
CLOAKED_HEAD = 0x665D40  # cloaked units, linked through +0xf0/+0xf4 (0x4340d0)
ORDER_FREE, ORDER_ALLOCATED = 0x5AEF00, 0x5AEF08
PATH_FREE, PATH_SIZE, PATHS = 0x69B570, 0x80, 1024  # the pool itself is the dump's "paths" blob
# Path record header (PackPathRecord 0x48e730, SetDirectPath 0x48ef10, NextPathPoint 0x48ec50):
# name, offset, struct format. The collision states keep the blocking unit's reference (+0x10),
# the step speed (+0x14) and the slide direction (+0x1a) there; flags bit 0 marks a changed
# move target (FlagPathMoveTargetChanged 0x42c790).
PATH = (("src_x", 0x00, "h"), ("src_y", 0x02, "h"), ("next_x", 0x04, "h"), ("next_y", 0x06, "h"),
        ("dest_x", 0x08, "h"), ("dest_y", 0x0A, "h"), ("frame", 0x0C, "I"), ("blocker", 0x10, "I"),
        ("speed", 0x14, "I"), ("flags", 0x18, "B"), ("retry", 0x19, "B"),
        ("slide", 0x1A, "B"), ("full_regions", 0x1B, "B"), ("regions", 0x1C, "b"),
        ("region_index", 0x1D, "b"), ("points", 0x1E, "b"), ("point_index", 0x1F, "b"))
# The Bullet pool (InitBullets 0x40a0e0, docs/research/native-projectiles.md): 100 records of
# 0x70 bytes, the allocated count and the active and free lists (head, tail).
BULLET_BASE, BULLET_SIZE, BULLETS = 0x53DB90, 0x70, 100
BULLET_ALLOCATED, BULLET_LISTS = 0x53DB8C, (("bullet_active", 0x540750), ("bullet_free", 0x540758))
# The computer players' AI (InitAiState 0x48db70): the AI region records of each player (the "ai_regions<p>" blobs, 0x34
# bytes each, docs/research/atlas/p37.md) and the PlayerAi records (0x67d068, 0x4e8 bytes each, p28.md).
AI_REGION = (("dest", 0x02, "H"), ("state", 0x05, "B"), ("priority", 0x06, "B"), ("retry", 0x07, "B"),
             ("flags", 0x08, "B"), ("wait", 0x09, "B"), ("ground_count", 0x0A, "H"), ("need_ground", 0x0C, "H"),
             ("need_air", 0x0E, "H"), ("local_ground", 0x10, "H"), ("local_air", 0x12, "H"), ("all_ground", 0x14, "H"),
             ("all_air", 0x16, "H"), ("enemy_air", 0x18, "H"), ("enemy_ground", 0x1A, "H"), ("target_air", 0x1C, "I"),
             ("target_ground", 0x20, "I"), ("slowest", 0x24, "I"), ("detector", 0x28, "I"))
AI_REGION_UNITS = ("target_air", "target_ground", "slowest", "detector")
AI_REACH, AI_REACH_TIME, AI_REACH_SOURCE = 0x6B1AA0, 0x6B68C0, 0x6B68E0  # BuildMilitaryReachCache 0x4abe90
PLAYER_AI_BASE, PLAYER_AI_SIZE = 0x67D068, 0x4E8
PLAYER_AI = (("heap_count", 0x210, "B"), ("placement_throttle", 0x211, "B"), ("nuke_rate", 0x212, "B"),
             ("expansion_attacks", 0x213, "B"), ("last_nuke", 0x214, "I"), ("flags", 0x218, "H"), ("panic", 0x21A, "H"),
             ("max_force", 0x21C, "H"), ("attack_target", 0x21E, "H"), ("wanted_unit", 0x220, "H"),
             ("default_min", 0x222, "B"), ("spend_counter", 0x223, "B"), ("timed_request", 0x224, "I"),
             ("grouping_time", 0x228, "I"), ("countdown0", 0x22C, "B"), ("countdown1", 0x22D, "B"),
             ("countdown2", 0x22E, "B"), ("ai_state", 0x22F, "B"))
ROW_HEADS, ROWS = 0x5C51D0, 256
RNG_STATE = 0x67393C
SPATIAL_X, SPATIAL_Y, SPATIAL_COUNT = 0x5C99B8, 0x5D03F8, 0x5C99B0
COUNTERS = (("all", 0x529604), ("completed", 0x52C0C4), ("kills", 0x52EB84), ("deaths", 0x531644))
TYPES = 228
PSTAT_BASE, PSTAT_ARRAYS = 0x5290C4, 28
PLAYER_RECORDS, PLAYER_RECORD_SIZE = 0x6726D0, 0x24
SHARED_VISION = 0x5264CC
ALLIANCES = 0x534914  # 12 x 12 bytes (InitAlliancesAndVision 0x477610)
# The resources of each player: current and cumulative minerals and gas (u32 arrays of 12).
RESOURCES = (("minerals", 0x5263D0), ("gas", 0x526400), ("cumulative_minerals", 0x526460), ("cumulative_gas", 0x526430))
# Globals of the object step and the frame (the step, the AI and the trigger clocks of
# kp_step.c, the creep of kp_creep.c): name, address, struct format.
STEP_GLOBALS = (
    ("tile_countdown", 0x6663E4, "I"), ("order_spread", 0x5DA390, "I"), ("secondary_spread", 0x5DA394, "I"),
    ("recede_timer", 0x6C46D4, "I"), ("recede_free", 0x6C46D2, "H"), ("recede_retry", 0x6C46D8, "I"),
    ("ai_terrain_changed", 0x69B560, "I"), ("ai_period", 0x69B564, "I"), ("ai_cycle", 0x69B568, "I"),
    ("ai_rounds", 0x69B56C, "I"), ("ai_region_player", 0x6B1A94, "I"), ("ai_region_cursor", 0x6B1A98, "I"),
    ("ai_spending_player", 0x67F7A8, "I"), ("ai_town_player", 0x685574, "I"), ("hit_slot", 0x541768, "I"),
    ("power_dirty", 0x665B60, "I"), ("elapsed", 0x5349D8, "I"), ("clock_second", 0x6CA744, "H"),
    ("clock_pass", 0x6CA748, "H"), ("clock_result", 0x6CA74C, "H"), ("clock_leaderboard", 0x6CA750, "H"),
)
# The trigger engine (atlas P40, P41): the copies of the player lists (the dump's "triggers" blob: a player byte and
# the 2400-byte record per node), the current player, the trigger pause, the waits, pending and committed results,
# the participants, objectives, switches, countdown, quiet mode, the next scenario, the leaderboard and MRGN.
TRIGGER_RECORD = 2400
TRIGGER_EXECUTION_FLAGS, TRIGGER_CONTINUATION = 0x940, 0x95F  # a record's u32 flags and continuation byte
TRIGGER_PLAYER, TRIGGER_PAUSED, GAME_PAUSED, GAME_SPEED = 0x6CA754, 0x6CA740, 0x67517C, 0x6D3130
WAITS, WAITS_REMAINING, PENDING_RESULTS, PARTICIPANTS = 0x6CA760, 0x6CA768, 0x6CA758, 0x6CA788
OBJECTIVES, SWITCHES, RESULTS, COUNTDOWN, COUNTDOWN_PAUSED = 0x5349A4, 0x534F20, 0x5349E0, 0x5349D4, 0x53632C
QUIET, NEXT_SCENARIO, RESULT_DIALOG = 0x667280, 0x526526, 0x6CA73C
LB_COMPUTERS, LB_ACTION, LB_GOAL, LB_LABEL, LB_ENTRIES = 0x5349E8, 0x5349EC, 0x5349F0, 0x5349F4, 0x6CA6D8
LOCATION_BASE, LOCATIONS = 0x534F40, 255
# The unit-count cache of Command and Bring: key and location bytes, all-unit and completed counters, statistics.
COUNT_KEY, COUNT_ALL, COUNT_COMPLETED, COUNT_PSTAT, COUNT_TABLE = 0x6CA6D0, 0x6C7C10, 0x6C5150, 0x6C4C10, 0x2AC0
PYLON = 156
POWER_HEAD = 0x665B54  # the first psi provider (kp_power.c)
RECEDE_BUCKETS = 0x6C46C0  # the WORD counts of the 9 receding-creep buckets
TURN_COUNTER = 0x672658
LOCAL_SELECTION, SELECTIONS = 0x6A6070, 0x6A5EF0  # 12 CUnit pointers; 8 players x 12
CONTROL_GROUPS, GROUPS = 0x527140, 18  # 8 players x 18 groups x 12 unit references
RECENT_STAMPS = 0x66CAD8  # 8 players x 8 WORD turn stamps of groups 10..17
GUARD_BLOCK, GUARD_SIZE, GUARD_RECORDS = 0x6B6908, 0x20, 1000
GUARD_FREE, GUARD_HEADS = 0x6B6908 + 32000, 0x6BE614

# CUnit fields: name, offset, struct format, pointer kind (None = value).
CUNIT = (
    ("hp", 0x08, "i", None), ("sprite", 0x0C, "I", "sprite"),
    ("move_x", 0x10, "h", None), ("move_y", 0x12, "h", None), ("move_unit", 0x14, "I", "unit"),
    ("waypoint_x", 0x18, "h", None), ("waypoint_y", 0x1A, "h", None),
    ("target_waypoint_x", 0x1C, "h", None), ("target_waypoint_y", 0x1E, "h", None),
    ("movement_flags", 0x20, "B", None), ("heading", 0x21, "B", None), ("turn_speed", 0x22, "B", None),
    ("velocity_direction", 0x23, "B", None), ("flingy", 0x24, "H", None), ("flingy_0x26", 0x26, "B", None),
    ("movement_type", 0x27, "B", None), ("x", 0x28, "h", None), ("y", 0x2A, "h", None),
    ("halt_x", 0x2C, "i", None), ("halt_y", 0x30, "i", None), ("top_speed", 0x34, "i", None),
    ("speed1", 0x38, "i", None), ("speed2", 0x3C, "i", None), ("speed_x", 0x40, "i", None),
    ("speed_y", 0x44, "i", None), ("acceleration", 0x48, "H", None), ("heading2", 0x4A, "B", None),
    ("velocity_direction2", 0x4B, "B", None), ("owner", 0x4C, "B", None), ("order", 0x4D, "B", None),
    ("order_state", 0x4E, "B", None), ("order_signal", 0x4F, "B", None), ("order_type", 0x50, "H", None),
    ("order_timer", 0x54, "B", None), ("ground_cooldown", 0x55, "B", None), ("air_cooldown", 0x56, "B", None),
    ("spell_cooldown", 0x57, "B", None), ("order_x", 0x58, "h", None), ("order_y", 0x5A, "h", None),
    ("order_unit", 0x5C, "I", "unit"), ("shields", 0x60, "i", None), ("type", 0x64, "H", None),
    ("player_prev", 0x68, "I", "unit"), ("player_next", 0x6C, "I", "unit"), ("subunit", 0x70, "I", "unit"),
    ("queue_head", 0x74, "I", "order"), ("queue_tail", 0x78, "I", "order"), ("auto_target", 0x7C, "I", "unit"),
    ("connected", 0x80, "I", "unit"), ("queue_count", 0x84, "B", None), ("queue_timer", 0x85, "B", None),
    ("unknown_0x86", 0x86, "B", None), ("attack_notify_timer", 0x87, "B", None),
    ("previous_type", 0x88, "H", None), ("last_event_timer", 0x8A, "B", None),
    ("last_event_color", 0x8B, "B", None), ("unknown_0x8c", 0x8C, "H", None), ("rank", 0x8E, "B", None),
    ("kills", 0x8F, "B", None), ("last_attacker", 0x90, "B", None), ("secondary_timer", 0x91, "B", None),
    ("ai_action", 0x92, "B", None), ("user_action", 0x93, "B", None), ("button_set", 0x94, "H", None),
    ("is_cloaked", 0x96, "B", None), ("movement_state", 0x97, "B", None),
    ("queue0", 0x98, "H", None), ("queue1", 0x9A, "H", None), ("queue2", 0x9C, "H", None),
    ("queue3", 0x9E, "H", None), ("queue4", 0xA0, "H", None), ("energy", 0xA2, "H", None),
    ("queue_slot", 0xA4, "B", None), ("uniqueness", 0xA5, "B", None), ("secondary_order", 0xA6, "B", None),
    ("overlay_state", 0xA7, "B", None), ("hp_gain", 0xA8, "H", None), ("shield_gain", 0xAA, "H", None),
    ("build_time", 0xAC, "H", None), ("previous_hp", 0xAE, "H", None),
    *((f"loaded{k}", 0xB0 + 2 * k, "H", None) for k in range(8)),
    # 0xC0..0xDB: the type-dependent union, compared as raw DWORDs (pointers included as
    # numbers would be meaningless, so the union words are decoded per type by consumers).
    *((f"union_{0xC0 + 4 * k:#x}", 0xC0 + 4 * k, "I", None) for k in range(7)),
    ("status", 0xDC, "I", None), ("resource_type", 0xE0, "B", None), ("wireframe", 0xE1, "B", None),
    ("secondary_state", 0xE2, "B", None), ("recent_order_timer", 0xE3, "B", None),
    ("visibility", 0xE4, "I", None), ("secondary_x", 0xE8, "h", None), ("secondary_y", 0xEA, "h", None),
    ("build_unit", 0xEC, "I", "unit"), ("burrow_prev", 0xF0, "I", "unit"), ("burrow_next", 0xF4, "I", "unit"),
    ("rally_x", 0xF8, "h", None), ("rally_y", 0xFA, "h", None), ("rally_unit", 0xFC, "I", "unit"),
    ("path", 0x100, "I", "path"), ("collision_interval", 0x104, "B", None), ("pathing_flags", 0x105, "B", None),
    ("unknown_0x106", 0x106, "B", None), ("being_healed", 0x107, "B", None),
    ("contour_left", 0x108, "h", None), ("contour_top", 0x10A, "h", None),
    ("contour_right", 0x10C, "h", None), ("contour_bottom", 0x10E, "h", None),
    ("remove_timer", 0x110, "H", None), ("matrix_damage", 0x112, "H", None), ("matrix_timer", 0x114, "B", None),
    ("stim_timer", 0x115, "B", None), ("ensnare_timer", 0x116, "B", None), ("lockdown_timer", 0x117, "B", None),
    ("irradiate_timer", 0x118, "B", None), ("stasis_timer", 0x119, "B", None), ("plague_timer", 0x11A, "B", None),
    ("storm_timer", 0x11B, "B", None), ("irradiated_by", 0x11C, "I", "unit"), ("irradiate_player", 0x120, "B", None),
    ("parasites", 0x121, "B", None), ("cycle_counter", 0x122, "B", None), ("blind", 0x123, "B", None),
    ("maelstrom_timer", 0x124, "B", None), ("unknown_0x125", 0x125, "B", None), ("acid_spores", 0x126, "B", None),
    *((f"acid_time{k}", 0x127 + k, "B", None) for k in range(9)),
    ("attack_sequence", 0x130, "H", None), ("unknown_0x132", 0x132, "H", None), ("ai", 0x134, "I", None),
    ("air_strength", 0x138, "H", None), ("ground_strength", 0x13A, "H", None),
    ("bound_left", 0x13C, "i", None), ("bound_right", 0x140, "i", None), ("bound_top", 0x144, "i", None),
    ("bound_bottom", 0x148, "i", None), ("repulse_flags", 0x14C, "B", None), ("repulse_angle", 0x14D, "B", None),
    ("repulse_x", 0x14E, "B", None), ("repulse_y", 0x14F, "B", None),
)
# CBullet fields after its CFlingy part (the same offsets as CUnit's up to 0x4b): name, offset,
# struct format, pointer kind.
BULLET = (*(f for f in CUNIT if f[1] < 0x4C and f[0] != "hp"),
          ("owner", 0x4C, "B", None), ("state", 0x4D, "B", None), ("order_state", 0x4E, "B", None),
          ("order_signal", 0x4F, "B", None), ("order_type", 0x50, "H", None), ("unknown_0x52", 0x52, "H", None),
          ("order_timer", 0x54, "B", None), ("ground_cooldown", 0x55, "B", None), ("air_cooldown", 0x56, "B", None),
          ("spell_cooldown", 0x57, "B", None), ("order_x", 0x58, "h", None), ("order_y", 0x5A, "h", None),
          ("order_unit", 0x5C, "I", "unit"), ("weapon", 0x60, "B", None), ("time", 0x61, "B", None),
          ("hit_flags", 0x62, "B", None), ("bounces", 0x63, "B", None), ("source", 0x64, "I", "unit"),
          ("previous_target", 0x68, "I", "unit"), ("ring", 0x6C, "B", None), ("unknown_0x6d", 0x6D, "B", None),
          ("unknown_0x6e", 0x6E, "H", None))
SPRITE = (("id", 0x08, "H"), ("owner", 0x0A, "B"), ("selection", 0x0B, "B"), ("visibility", 0x0C, "B"),
          ("elevation", 0x0D, "B"), ("flags", 0x0E, "B"), ("sel_timer", 0x0F, "B"), ("index", 0x10, "H"),
          ("width", 0x12, "B"), ("height", 0x13, "B"), ("x", 0x14, "h"), ("y", 0x16, "h"))
IMAGE = (("id", 0x08, "H"), ("method", 0x0A, "B"), ("dir", 0x0B, "B"), ("flags", 0x0C, "H"),
         ("xoff", 0x0E, "b"), ("yoff", 0x0F, "b"), ("header", 0x10, "H"), ("pc", 0x12, "H"),
         ("ret", 0x14, "H"), ("anim", 0x16, "B"), ("wait", 0x17, "B"), ("frameset", 0x18, "H"),
         ("frame", 0x1A, "H"), ("x", 0x1C, "h"), ("y", 0x1E, "h"))


class Decoder:
    def __init__(self, state: State):
        self.s = state

    def u32(self, a: int) -> int:
        return struct.unpack("<I", self.s.read(a, 4))[0]

    def unit_index(self, p: int) -> int:
        if p == 0:
            return 0
        k, r = divmod(p - UNIT_BASE, UNIT_SIZE)
        if r or not 0 <= k < UNITS:
            raise ValueError(f"unit pointer {p:#x}")
        return k + 1

    def sprite_index(self, p: int) -> int:
        if p == 0:
            return -1
        k, r = divmod(p - SPRITE_BASE, SPRITE_SIZE)
        if r or not 0 <= k < SPRITES:
            raise ValueError(f"sprite pointer {p:#x}")
        return k

    def order_index(self, p: int) -> int:
        if p == 0:
            return -1
        k, r = divmod(p - ORDER_BASE, ORDER_SIZE)
        if r or not 0 <= k < ORDERS:
            raise ValueError(f"order pointer {p:#x}")
        return k

    def path_index(self, p: int) -> int:
        if p == 0:
            return 0
        k, r = divmod(p - self.s.blob_base("paths"), PATH_SIZE)
        if r or not 0 <= k < PATHS:
            raise ValueError(f"path pointer {p:#x}")
        return k + 1

    def path(self, k: int) -> dict:
        """Header fields of path record k, its points (x:y) and its region ids."""
        raw = self.s.blob("paths")[(k - 1) * PATH_SIZE:k * PATH_SIZE]
        f = {name: struct.unpack_from("<" + fmt, raw, off)[0] for name, off, fmt in PATH}
        pts = [struct.unpack_from("<hh", raw, 0x20 + 4 * i) for i in range(max(f["points"], 0))]
        at = 0x20 + 4 * len(pts)
        f["point_list"] = ",".join(f"{x}:{y}" for x, y in pts)
        f["region_list"] = ",".join(str(struct.unpack_from("<H", raw, at + 2 * i)[0])
                                    for i in range(max(f["regions"], 0)))
        f["record"] = k
        return f

    def image_index(self, p: int) -> int:
        if p == 0:
            return -1
        for base, count, first in IMAGE_POOLS:
            k, r = divmod(p - base, IMAGE_SIZE)
            if 0 <= k < count and not r:
                return first + k
        raise ValueError(f"image pointer {p:#x}")

    def image_address(self, key: int) -> int:
        for base, count, first in IMAGE_POOLS:
            if first <= key < first + count:
                return base + (key - first) * IMAGE_SIZE
        raise ValueError(key)

    def chain(self, head: int, next_offset: int, to_index, limit: int) -> list[int]:
        out, p = [], head
        while p:
            out.append(to_index(p))
            if len(out) > limit:
                raise ValueError("list cycle")
            p = self.u32(p + next_offset)
        return out

    def bullet_index(self, p: int) -> int:
        if p == 0:
            return 0
        k, r = divmod(p - BULLET_BASE, BULLET_SIZE)
        if r or not 0 <= k < BULLETS:
            raise ValueError(f"bullet pointer {p:#x}")
        return k + 1

    def record(self, raw: bytes, layout) -> dict:
        out = {}
        for name, off, fmt, kind in layout:
            v = struct.unpack_from("<" + fmt, raw, off)[0]
            if kind == "unit":
                v = self.unit_index(v)
            elif kind == "sprite":
                v = self.sprite_index(v)
            elif kind == "order":
                v = self.order_index(v)
            elif kind == "path":
                v = self.path_index(v)
            elif name.startswith("union_") and v and not (v - SPRITE_BASE) % SPRITE_SIZE \
                    and 0 <= (v - SPRITE_BASE) // SPRITE_SIZE < SPRITES:
                v = f"s{self.sprite_index(v)}"  # a sprite in the union: a Pylon's Psi field (+0xd0)
            elif name.startswith("union_") and v and not (v - UNIT_BASE) % UNIT_SIZE \
                    and 0 <= (v - UNIT_BASE) // UNIT_SIZE < UNITS:
                v = f"u{self.unit_index(v)}"  # a unit reference in the type-dependent union
            out[name] = v
        return out

    def guard_index(self, p: int) -> int:
        if p == 0:
            return -1
        k, r = divmod(p - GUARD_BLOCK, GUARD_SIZE)
        if r or not 0 <= k < GUARD_RECORDS:
            raise ValueError(f"guard pointer {p:#x}")
        return k

    def unit(self, n: int) -> dict:
        out = self.record(self.s.read(UNIT_BASE + (n - 1) * UNIT_SIZE, UNIT_SIZE), CUNIT)
        if out["type"] == PYLON:  # +0xf8 is the previous psi provider, which the power category's list holds
            out["rally_x"] = out["rally_y"] = 0
        out["ai"] = f"g{self.guard_index(out['ai'])}" if out["ai"] else 0
        return out

    def triggers(self, m: dict) -> None:
        """The trigger engine (kp_triggers.c): the copies of the player lists, its globals and the locations."""
        copies = []
        blob = self.s.blob("triggers") if "triggers" in self.s.blobs else b""
        places = [0] * 8
        for at in range(0, len(blob), 1 + TRIGGER_RECORD):
            p, rec = blob[at], blob[at + 1:at + 1 + TRIGGER_RECORD]
            copies.append((f"{p}.{places[p]}", {"flags": struct.unpack_from("<I", rec, TRIGGER_EXECUTION_FLAGS)[0],
                                                "continuation": rec[TRIGGER_CONTINUATION],
                                                "record": f"{zlib.crc32(rec):08x}"}))
            places[p] += 1
        m["trigger"] = copies
        lb = struct.unpack("<16i", self.s.read(LB_ENTRIES, 64))
        g = {"player": self.u32(TRIGGER_PLAYER), "paused": self.s.read(TRIGGER_PAUSED, 1)[0],
             "result_dialog": self.u32(RESULT_DIALOG),
             "game_paused": self.u32(GAME_PAUSED), "speed": self.s.read(GAME_SPEED, 1)[0],
             "waits": _bytes(self.s.read(WAITS, 8)), "remaining": _u32s(self.s.read(WAITS_REMAINING, 32)),
             "pending": _bytes(self.s.read(PENDING_RESULTS, 8)), "participants": _bytes(self.s.read(PARTICIPANTS, 8)),
             "objectives": _u32s(self.s.read(OBJECTIVES, 32)), "switches": _u32s(self.s.read(SWITCHES, 32)),
             "results": _bytes(self.s.read(RESULTS, 8)), "countdown": self.u32(COUNTDOWN),
             "countdown_paused": self.u32(COUNTDOWN_PAUSED), "quiet": self.u32(QUIET),
             "next_scenario": self.s.read(NEXT_SCENARIO, 32).hex(), "lb_computers": self.u32(LB_COMPUTERS),
             "lb_action": self.s.read(LB_ACTION, 1)[0], "lb_location": self.s.read(LB_ACTION + 1, 1)[0],
             "lb_type": struct.unpack("<H", self.s.read(LB_ACTION + 2, 2))[0], "lb_goal": self.u32(LB_GOAL),
             "lb_label": self.u32(LB_LABEL), "lb_entries": ",".join(str(v & 0xFFFFFFFF) for v in lb)}
        m["triggers"] = [("0", g)]
        m["trigger_counts"] = [("0", {"key": self.s.read(COUNT_KEY, 1)[0], "location": self.s.read(COUNT_KEY + 1, 1)[0],
                                      "all": f"{zlib.crc32(self.s.read(COUNT_ALL, COUNT_TABLE)):08x}",
                                      "completed": f"{zlib.crc32(self.s.read(COUNT_COMPLETED, COUNT_TABLE)):08x}",
                                      "pstat": f"{zlib.crc32(self.s.read(COUNT_PSTAT, 0x540)):08x}"})]
        locations = []
        for n in range(1, LOCATIONS + 1):
            rec = struct.unpack("<iiiiHH", self.s.read(LOCATION_BASE + 20 * (n - 1), 20))
            if any(rec):
                locations.append((str(n), dict(zip(("left", "top", "right", "bottom", "name", "elevation"), rec))))
        m["location"] = locations

    def model(self) -> dict[str, list[tuple[str, dict]]]:
        """Category -> [(key, fields)] in native order."""
        m: dict[str, list[tuple[str, dict]]] = {}
        g = {"rng": self.u32(RNG_STATE), "units_allocated": self.u32(UNIT_ALLOCATED),
             "orders_allocated": self.u32(ORDER_ALLOCATED), "spatial_count": self.u32(SPATIAL_COUNT),
             "cloaked_head": self.unit_index(self.u32(CLOAKED_HEAD))}
        lists = {}
        for name, head in UNIT_LISTS:
            lists[name] = self.chain(self.u32(head), 4, self.unit_index, UNITS)
            g[f"{name}_tail"] = self.unit_index(self.u32(head + 4))
        for p in range(PLAYERS):
            lists[f"player{p}"] = self.chain(self.u32(PLAYER_LISTS + 4 * p), 0x6C, self.unit_index, UNITS)
        m["global"] = [("0", g)]
        # The Bullet pool in categories of its own, so the goldens of the earlier categories stay.
        pool = {"allocated": self.u32(BULLET_ALLOCATED)}
        for name, head in BULLET_LISTS:
            lists[name] = self.chain(self.u32(head), 4, self.bullet_index, BULLETS)
            pool[name[7:]] = ",".join(map(str, lists[name]))
            pool[f"{name[7:]}_tail"] = self.bullet_index(self.u32(head + 4))
        m["bullets"] = [("0", pool)]
        # The provider list of psi power (head 0x665b54, next +0xfc), a category of its own as well.
        m["power"] = [("0", {"providers": ",".join(map(str, self.chain(self.u32(POWER_HEAD), 0xFC, self.unit_index,
                                                                      UNITS)))})]
        m["list"] = [(name, {"units": ",".join(map(str, v))}) for name, v in lists.items()
                     if not name.startswith("bullet")]
        units = []
        for name in ("active", "hidden", "special", "pending"):
            for n in lists[name]:
                fields = self.unit(n)
                fields["list"] = name
                units.append((str(n), fields))
        m["unit"] = units
        m["bullet"] = [(str(b), self.record(self.s.read(BULLET_BASE + (b - 1) * BULLET_SIZE, BULLET_SIZE), BULLET))
                       for b in lists["bullet_active"]]
        orders = []
        for key, u in units:
            node = u["queue_head"]
            k = 0
            while node != -1:
                a = ORDER_BASE + node * ORDER_SIZE
                order, otype, x, y, target = struct.unpack("<HHhhI", self.s.read(a + 8, 12))
                orders.append((f"{key}.{k}", {"node": node, "order": order & 0xFF, "type": otype, "x": x, "y": y,
                                              "unit": self.unit_index(target)}))
                node = self.order_index(self.u32(a + 4))
                k += 1
        m["order"] = orders
        m["order_free"] = [("0", {"nodes": ",".join(map(str, self.chain(self.u32(ORDER_FREE), 4, self.order_index,
                                                                          ORDERS)))})]
        m["path"] = [(key, self.path(u["path"])) for key, u in units if u["path"]]
        free, k = [], self.path_index(self.u32(PATH_FREE))
        while k:  # a free record's first DWORD is the next free record's number
            free.append(k)
            if len(free) > PATHS:
                raise ValueError("path free list cycle")
            k = struct.unpack_from("<I", self.s.blob("paths"), (k - 1) * PATH_SIZE)[0]
        m["path_free"] = [("0", {"records": ",".join(map(str, free))})]
        m["density"] = [("0", {"cells": ",".join(f"{k}:{v}" for k, v in enumerate(self.s.blob("density")) if v)})]
        sprites, images = [], []
        for r in range(ROWS):
            for s in self.chain(self.u32(ROW_HEADS + 4 * r), 4, self.sprite_index, SPRITES):
                raw = self.s.read(SPRITE_BASE + s * SPRITE_SIZE, SPRITE_SIZE)
                f = {name: struct.unpack_from("<" + fmt, raw, off)[0] for name, off, fmt in SPRITE}
                f["row"] = r
                main, head = struct.unpack_from("<II", raw, 0x18)
                f["main"] = self.image_index(main)
                ims = self.chain(head, 4, self.image_index, 5300)
                f["images"] = ",".join(map(str, ims))
                sprites.append((str(s), f))
                for i in ims:
                    raw_i = self.s.read(self.image_address(i), IMAGE_SIZE)
                    fi = {name: struct.unpack_from("<" + fmt, raw_i, off)[0] for name, off, fmt in IMAGE}
                    fi["flags"] &= ~IMAGE_REDRAW  # the redraw bit is presentation state, not kept by Koprulu
                    fi["sprite"] = self.sprite_index(struct.unpack_from("<I", raw_i, 0x3C)[0])
                    images.append((str(i), fi))
        m["sprite"] = sprites
        m["image"] = images
        counters = []
        for name, base in COUNTERS:
            for t in range(TYPES):
                row = struct.unpack("<12I", self.s.read(base + t * 48, 48))
                if any(row):
                    counters.append((f"{name}.{t}", {"players": ",".join(map(str, row))}))
        m["counter"] = counters
        spatial = []
        count = self.u32(SPATIAL_COUNT)
        for axis, base in (("x", SPATIAL_X), ("y", SPATIAL_Y)):
            entries = [struct.unpack("<II", self.s.read(base + 8 * k, 8)) for k in range(count)]
            spatial.append((axis, {"entries": ",".join(f"{u}:{c}" for u, c in entries)}))
        m["spatial"] = spatial
        m["pstat"] = [(f"{PSTAT_BASE + 0x30 * a:#x}",
                       {"players": ",".join(map(str, struct.unpack("<12I", self.s.read(PSTAT_BASE + 0x30 * a, 48))))})
                      for a in range(PSTAT_ARRAYS)]
        shared = struct.unpack("<12I", self.s.read(SHARED_VISION, 48))
        m["player"] = [(str(p), {"controller": self.s.read(PLAYER_RECORDS + PLAYER_RECORD_SIZE * p, 1)[0],
                                 "force": self.s.read(PLAYER_RECORDS + PLAYER_RECORD_SIZE * p + 2, 1)[0],
                                 "alliance": self.s.read(ALLIANCES + 12 * p, 12).hex(),
                                 "shared_vision": shared[p]}) for p in range(12)]
        # The resources in a category of their own, so the goldens of the earlier categories stay valid.
        m["resources"] = [(str(p), {name: self.u32(base + 4 * p) for name, base in RESOURCES}) for p in range(12)]
        step = {name: struct.unpack("<" + fmt, self.s.read(a, struct.calcsize(fmt)))[0]
                for name, a, fmt in STEP_GLOBALS}
        step["recede_buckets"] = "".join(f"{c:02x}" for c in struct.unpack("<9H", self.s.read(RECEDE_BUCKETS, 18)))
        step["turn"] = self.u32(TURN_COUNTER)
        m["step"] = [("0", step)]

        def units(a: int, count: int) -> str:
            return ",".join(str(self.unit_index(self.u32(a + 4 * k))) for k in range(count))

        sel = [("local", {"units": units(LOCAL_SELECTION, 12)})]
        for p in range(8):
            groups = {}
            for g in range(GROUPS):
                refs = struct.unpack("<12I", self.s.read(CONTROL_GROUPS + (p * GROUPS + g) * 48, 48))
                if any(refs):
                    groups[f"group{g}"] = ",".join(map(str, refs))
            stamps = struct.unpack("<8H", self.s.read(RECENT_STAMPS + 16 * p, 16))
            sel.append((str(p), {"units": units(SELECTIONS + 48 * p, 12), "recent": ",".join(map(str, stamps)),
                                 **groups}))
        m["selection"] = sel
        guards = []
        for p in range(8):
            recs, g = [], self.u32(GUARD_HEADS + 8 * p)
            while g:
                k = self.guard_index(g)
                raw = self.s.read(g, GUARD_SIZE)
                kind, deaths, unit, gtype, x1, y1, x2, y2 = struct.unpack_from("<BBxxIHhhhh", raw, 8)
                recs.append(":".join(map(str, (k, kind, self.unit_index(unit), gtype, x1, y1, x2, y2, deaths))))
                g = struct.unpack_from("<I", raw, 0)[0]
            guards.append((str(p), {"records": ",".join(recs)}))
        guards.append(("free", {"head": self.guard_index(self.u32(GUARD_FREE))}))
        m["guard"] = guards
        regions = []
        for p in range(8):
            name = f"ai_regions{p}"
            if name not in self.s.blobs:
                continue
            blob = self.s.blob(name)
            for r in range(len(blob) // 0x34):
                f = {n: struct.unpack_from("<" + fmt, blob, 0x34 * r + off)[0] for n, off, fmt in AI_REGION}
                for n in AI_REGION_UNITS:
                    f[n] = self.unit_index(f[n])
                # Records still as InitAiRegions 0x4b0640 left them (ID r, owner p, the rest 0) are left out.
                if any(f.values()):
                    regions.append((f"{p}.{r}", f))
        m["ai_region"] = regions
        if "regions" in self.s.blobs:
            count = struct.unpack_from("<H", self.s.blob("regions"), 0)[0]
            m["ai_reach"] = [(str(p), {"time": self.u32(AI_REACH_TIME + 4 * p), "source": self.u32(AI_REACH_SOURCE + 4 * p),
                                       "marks": self.s.read(AI_REACH + 2500 * p, min(count, 2500)).hex()})
                             for p in range(8)]
        m["ai_player"] = [(str(p), {n: struct.unpack_from("<" + fmt, self.s.read(PLAYER_AI_BASE + PLAYER_AI_SIZE * p + off,
                                                                                  struct.calcsize(fmt)))[0]
                                    for n, off, fmt in PLAYER_AI}) for p in range(8)]
        self.triggers(m)
        w, h = self.s.header["map"]
        tiles = self.s.blob("tile_flags")
        m["tiles"] = [(str(ty), {"flags": "".join(f"{struct.unpack_from('<I', tiles, 4 * (ty * w + tx))[0]:08x}"
                                                  for tx in range(w))}) for ty in range(h)]
        mtxm, backup = self.s.blob("mtxm"), self.s.blob("creep_backup")
        border, display = self.s.blob("creep_border"), self.s.blob("megatiles")

        def words(blob: bytes, ty: int) -> str:
            return "".join(f"{v:04x}" for v in struct.unpack_from(f"<{w}H", blob, 2 * ty * w))

        m["terrain"] = [(str(ty), {"mtxm": words(mtxm, ty), "backup": words(backup, ty),
                                   "border": border[ty * w:(ty + 1) * w].hex(), "display": words(display, ty)})
                        for ty in range(h)]
        return m


def _u32s(raw: bytes) -> str:
    return ",".join(map(str, struct.unpack(f"<{len(raw) // 4}I", raw)))


def _bytes(raw: bytes) -> str:
    return ",".join(map(str, raw))


def lines(model: dict, category: str, fields: list[str] | None = None) -> list[str]:
    """Canonical lines (fields sorted), as kp_core game:dump prints them."""
    out = []
    for key, f in model[category]:
        names = sorted(fields if fields is not None else f)
        out.append(" ".join([category, str(key)] + [f"{n}={f[n]}" for n in names]))
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("state", type=Path)
    ap.add_argument("--category", action="append")
    ap.add_argument("--fields")
    a = ap.parse_args()
    logging.basicConfig(level=logging.INFO, format="%(levelname)s %(name)s: %(message)s")
    model = Decoder(State(a.state)).model()
    fields = a.fields.split(",") if a.fields else None
    for c in a.category or list(model):
        for line in lines(model, c, fields):
            sys.stdout.write(line + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
