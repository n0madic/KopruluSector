"""Scripted wins of the campaign rows (plan phase 8, docs/phase8.md): the toolkit and the plays.

Every play starts a campaign row in a fresh process (the Ophelia warp, or the mission injection for Bonus, which the
warp refuses), types the helper cheats the user allowed (scenarios.HELPER_CHEATS without "war aint what it used to
be": power overwhelming, show me the money, operation cwal, the gathering, medieval man, modify the phase variance,
food for thought, black sheep wall) and then wins the mission through its own objectives with ordinary input:
selection boxes and clicks, hotkeys, the minimap. The cheats that end a mission are never typed. Like the scripts of
scenarios.py, a play only reads the game's memory to decide where to click.

`python -m sc_emulator play NAME OUT --state STEPS [--fog FRAMES]` runs a play with the logs and state dumps Koprulu
replays (tools/state_check.py --kind plays); the play's own record (trace, events, result) goes to
emu/plays-records/OUT.

Toolkit (Kit):
- the unit table of units.dat read from the game's memory (defs/dat column addresses): buildings, workers, flyers,
  weapons;
- the players of a trigger's player code (0-11, 13 current, 17 all, 18-21 a force, 26 the non-allied victory ones);
- `conquer`: sends the idle army, in groups of up to 12 near each other, attack-moving onto the nearest target of
  the given players and types, until the mission has a result or no target is left;
- `bring` / `bring_all`: move units of a type (of several types) into a location;
- `strike`: idle army groups move without fighting to the nearest target and attack that target itself (A + a
  click on it), units near a target but busy with something else are sent at it again;
- `travel`: units walk through waypoints (a teleporter pad's leg checks its destination);
- `approach`: one unit walks to an exact point (a right click in the view, finer than the minimap);
- `withdraw`: army units near buildings that must survive are called back;
- `wait_result`: lets the game run until the mission's result;
- `build` / `build_and_wait` / `refinery` / `addon`: an SCV places a building at the first valid point of rings
  around a spot, a building builds an add-on (clicking the place the game asks for);
- `train` / `queue_empty` / `rally` / `money`: producers train a unit (operation cwal makes it fast), "show me the
  money" again when minerals run low;
- `build` with `worker=T_DRONE`: a Drone places a Zerg building (B or V menu);
- `morph`: the larvae of each Hatchery (Lair, Hive) morph into a unit (S, then the unit's hotkey);
- `ventral_sacs`: a Lair (morphed from a Hatchery when there is none) researches Ventral Sacs;
- `airlift`: a unit boards an Overlord, which unloads it at a point (Unload All and a click);
- `carry`: a worker picks up a powerup (Chrysalis, Khaydarin Crystal) and carries it into a location;
- `build` with `worker=T_PROBE`: a Probe warps in a Protoss building (B or V menu);
- `ferry`: units board a transport (a Shuttle), which unloads them at a point (Unload All and a click);
- `select_box` / `assemble`: a unit selected with a small selection box (a crowd of enemies takes a click); units
  of some types move into a location and hold their position there (H);
- `land`: a lifted Terran building lands (L and a click at the first valid point);
- `halt`: units stop (S), ending an attack order a click turned on the player's own unit.
Play helpers outside the Kit: `march`, `march_unit`, `rescue` (rescuable units), `production` (Marines),
`starport_tick` (Battlecruisers or Wraiths), `brood` (Drones, Hatcheries and a Spire), `brood_tick` (larvae into
Mutalisks), `crystal_carrier`, `gateways` (a Pylon and Gateways or Stargates), `gate_tick` (Zealots, or Scouts),
`air_strike` (Starports, then the flyers strike), `gone`, `escort` (walkers one by one onto beacons).
"""
from __future__ import annotations

import json
import logging
import struct
from collections.abc import Callable, Iterable
from pathlib import Path
from typing import Any

from . import game
from .machine import Machine
from .play import U_QUEUE_SLOT, Player
from .scenario import ALLIANCES
from .scenarios import (BUTTON_SET_SHOWN, BUILD_ORDERS, HELPER_CHEATS, IDLE_ORDERS, PLACE_ACTIVE, S_IN_TRANSPORT,
                        Recorder, StopAfterResult, enter_game, in_rect, place_first_valid, select_building,
                        select_group, spiral, start, type_cheats, unit_brief)
from .session import Session

log = logging.getLogger(__name__)
REPO = Path(__file__).resolve().parents[3]
RECORDS = (Path.home() / "koprulu-private/emu/plays-records")
PLAY_CHEATS = tuple(c for c in ("power overwhelming", "show me the money", "operation cwal", "the gathering",
                                "medieval man", "modify the phase variance", "food for thought", "black sheep wall")
                    if c in HELPER_CHEATS)
NO_WEAPON = 130
FLAG_BUILDING, FLAG_FLYER, FLAG_WORKER, FLAG_HERO = 0x1, 0x4, 0x8, 0x40000
STATUS_COMPLETED = 0x1
S_IN_AIR = 0x4  # CUnit status: flying (a lifted building too)
LIFTED_CLICKS = ((0, 0), (0, -24), (0, -40), (-24, -24), (24, -24))  # a lifted building's sprite floats above it
PLAYER_RECORDS, PLAYER_RECORD_SIZE = 0x6726D0, 36
CURRENT_PLAYER, FOES, ALL_PLAYERS, FORCE_1, FORCE_4, NON_ALLIED_VICTORY = 13, 14, 17, 18, 21, 26
NEUTRAL = 11
# Units that attack without their own weapon entry (they launch or hold an attacking subunit).
INDIRECT_ATTACKERS = {72, 81, 82, 83, 98}  # Carrier, Warbringer, Gantrithor, Reaver, Hero Carrier... (units.dat)
GROUP = 12
NEAR = 256  # pixels: units this close form one group
T_SCV, T_GEYSER = 7, 188
# Terran buildings of the SCV's advanced build menu (V); the others are in the basic one (B).
ADVANCED_BUILDINGS = {113, 114, 116, 123}
T_LARVA, T_DRONE = 35, 41
HATCHERIES = (131, 132, 133)  # Hatchery, Lair, Hive
# Zerg buildings of the Drone's advanced build menu (V): Nydus Canal, Defiler Mound, Greater Spire, Queen's Nest,
# Ultralisk Cavern, Spire; the others are in the basic one (B).
ZERG_ADVANCED = {134, 136, 137, 138, 140, 141}
T_PROBE = 64
# Protoss buildings of the Probe's advanced build menu (V): Robotics Facility, Observatory, Citadel of Adun,
# Cybernetics Core, Templar Archives, Stargate, Fleet Beacon, Arbiter Tribunal, Robotics Support Bay, Shield Battery;
# the others are in the basic one (B).
PROTOSS_ADVANCED = {155, 159, 163, 164, 165, 167, 169, 170, 171, 172}
LARVA_NEAR = 200  # pixels: larvae this close to a hatchery are taken as its own
PICK_UP_AGAIN = 480  # frames: a worker still without its powerup is sent at it again after this
T_LAIR, T_OVERLORD = 132, 42
ORDER_NOTHING = 0x17  # a building's idle order
ORDER_PICKUP_IDLE = 0x5D  # a transport's idle order
HATCHERY_CLICKS = ((0, -30), (0, -16), (-32, -16), (32, -16))  # above the larvae at a hatchery's foot
UNLOAD_ALL_SLOT = 9  # the transport's command-card button
ORDER_MOVE = 6
ORDER_MEDIC_IDLE = 0xAF  # a Medic's idle order
UNIT_IDLE_ORDERS = IDLE_ORDERS + (ORDER_MEDIC_IDLE,)
STATUS_EVERY = 1000  # frames between the status events of a long loop
STAGE_RADIUS = 256  # pixels: a unit this close to a staging point has reached it
MONEY_FLOOR = 1000  # minerals: below this a play types "show me the money" again


def dist(u: dict, x: int, y: int) -> int:
    return abs(u["x"] - x) + abs(u["y"] - y)


def dat_column(m: Machine, name: str) -> list[int]:
    """A units.dat column from the game's memory (its address in defs/dat)."""
    raw = json.loads((REPO / "defs/dat/bw-1.17.0.1.json").read_text())
    f = next(x for x in raw["files"] if x["path"].endswith("units.dat"))
    col = next(c for c in f["columns"] if c[0] == name)
    _, size, count, _, _, va = col
    fmt = {1: "B", 2: "H", 4: "I"}[size]
    return list(struct.unpack(f"<{count}{fmt}", m.read(int(va, 16), size * count)))


class Kit:
    def __init__(self, s: Session, pl: Player, rec: Recorder) -> None:
        self.s, self.pl, self.rec, self.m = s, pl, rec, s.m
        self.flags = dat_column(self.m, "special_ability_flags")
        self.ground = dat_column(self.m, "ground_weapon")
        self.air = dat_column(self.m, "air_weapon")
        self.subunit = dat_column(self.m, "subunit1")

    # ---------------------------------------------------------------- unit table
    def is_building(self, t: int) -> bool:
        return bool(self.flags[t] & FLAG_BUILDING)

    def is_worker(self, t: int) -> bool:
        return bool(self.flags[t] & FLAG_WORKER)

    def is_flyer(self, t: int) -> bool:
        return bool(self.flags[t] & FLAG_FLYER)

    def attacks(self, t: int) -> bool:
        if t in INDIRECT_ATTACKERS:
            return True
        s = self.subunit[t]
        return (self.ground[t] != NO_WEAPON or self.air[t] != NO_WEAPON
                or (s < 228 and (self.ground[s] != NO_WEAPON or self.air[s] != NO_WEAPON)))

    def hits_air(self, t: int) -> bool:
        s = self.subunit[t]
        return self.air[t] != NO_WEAPON or t in (72, 82) or (s < 228 and self.air[s] != NO_WEAPON)

    def hits_ground(self, t: int) -> bool:
        s = self.subunit[t]
        return self.ground[t] != NO_WEAPON or t in INDIRECT_ATTACKERS or (s < 228 and self.ground[s] != NO_WEAPON)

    # ---------------------------------------------------------------- players
    def force(self, p: int) -> int:
        return self.m.read(PLAYER_RECORDS + PLAYER_RECORD_SIZE * p + 2, 1)[0]

    def players(self, code: int) -> list[int]:
        """The players of a trigger's player code (TrigCondPlayers, atlas P40)."""
        if code < 12:
            return [code]
        if code == CURRENT_PLAYER:
            return [self.pl.local]
        if code == FOES:
            me = self.pl.local
            return [p for p in range(8) if p != me and not self.m.read(ALLIANCES + 12 * me + p, 1)[0]]
        if code == ALL_PLAYERS:
            return list(range(8))
        if FORCE_1 <= code <= FORCE_4:
            return [p for p in range(8) if self.force(p) == code - FORCE_1 + 1]
        if code == NON_ALLIED_VICTORY:
            return [p for p in range(8) if p != self.pl.local]
        raise ValueError(f"player code {code}")

    # ---------------------------------------------------------------- units
    def army(self, flyers: bool | None = None) -> list[dict]:
        out = []
        for u in self.pl.mine():
            t = u["type"]
            if self.is_building(t) or self.is_worker(t) or not self.attacks(t) or not u["status"] & STATUS_COMPLETED:
                continue
            if flyers is not None and self.is_flyer(t) != flyers:
                continue
            out.append(u)
        return out

    def aim(self, u: dict) -> list[int] | None:
        """[type, owner] of the unit's order target, or None."""
        if not u["target_unit"]:
            return None
        t = self.pl.unit(u["target_unit"])
        return [t["type"], t["owner"]]

    def targets(self, players: list[int], types: Iterable[int] | None = None, buildings_only: bool = False,
                rects: list[tuple[int, int, int, int]] | None = None) -> list[dict]:
        """The units of the players (of the types, only buildings, only inside one of the rectangles)."""
        out = []
        for p in players:
            for u in self.pl.units(owner=p):
                if types is not None and u["type"] not in types:
                    continue
                if buildings_only and not self.is_building(u["type"]):
                    continue
                if rects is not None and not any(in_rect(u, r) for r in rects):
                    continue
                out.append(u)
        return out

    @staticmethod
    def groups(units: list[dict]) -> list[list[dict]]:
        """Units in groups of up to 12 whose members stand within NEAR of the group's first unit."""
        left, out = list(units), []
        while left:
            first = left.pop(0)
            grp = [first]
            for u in list(left):
                if len(grp) >= GROUP:
                    break
                if abs(u["x"] - first["x"]) + abs(u["y"] - first["y"]) <= NEAR:
                    grp.append(u)
                    left.remove(u)
            out.append(grp)
        return out

    def select(self, grp: list[dict]) -> list[dict]:
        """Selects a group of units (a click per unit); a subclass may select it another way."""
        return select_group(self.pl, grp)

    def send(self, units: list[dict], x: int, y: int, attack: bool = True) -> int:
        """Each group of the units attack-moves (A + minimap) or moves (right click on the minimap) to (x, y)."""
        sent = 0
        for grp in self.groups(units):
            sel = self.select(grp)
            if not sel:
                continue
            if attack:
                self.pl.hotkey(ord("A"))
                self.pl.minimap_click(x, y)
            else:
                self.pl.minimap_click(x, y, right=True)
            sent += len(sel)
        return sent

    # ---------------------------------------------------------------- objectives
    def cheats(self, texts: Iterable[str] = PLAY_CHEATS) -> None:
        type_cheats(self.pl, self.rec, texts)

    def wait_result(self, max_frames: int = 40000, every: int = 240,
                    tick: Callable[[], None] | None = None) -> None:
        """Lets the game run (calling `tick` every `every` frames) until the mission has a result."""
        with StopAfterResult(self.rec):
            while self.rec.result is None and self.pl.frame() < max_frames:
                if tick:
                    tick()
                self.pl.frames(every)

    def conquer(self, players: list[int], types: Iterable[int] | None = None, buildings_only: bool = True,
                every: int = 96, max_frames: int = 40000, tick: Callable[[], None] | None = None,
                stage: tuple[int, int] | None = None, flyers: bool | None = None,
                rects: list[tuple[int, int, int, int]] | None = None, done: Callable[[], bool] | None = None,
                near: int = 480) -> None:
        """Until the result (or `done()`): the idle army (only its flyers or ground units with `flyers`)
        attack-moves onto the nearest target (inside one of `rects`) able to be hit. With `stage`, an idle unit
        first moves (without attacking) to that point and attacks only once it has been there. A unit within
        `near` of a target but busy with something else counts as idle."""
        staged: set[int] = set()
        last = -STATUS_EVERY
        with StopAfterResult(self.rec):
            while self.rec.result is None and self.pl.frame() < max_frames and not (done and done()):
                if tick:
                    tick()
                targets = self.targets(players, types, buildings_only, rects)
                army = self.army(flyers)
                aimed = {t["ptr"] for t in targets}
                # idle, or near a target but busy with something else (the AI's units keep coming)
                idle = [u for u in army if u["order"] in IDLE_ORDERS
                        or (u["order"] != ORDER_MOVE and u["target_unit"] not in aimed
                            and any(dist(u, t["x"], t["y"]) <= near for t in targets))]
                if self.pl.frame() - last >= STATUS_EVERY:
                    last = self.pl.frame()
                    self.rec.event("status", army=[[u["type"], u["x"], u["y"], u["order"]] for u in army],
                                   targets=[[t["type"], t["owner"], t["x"], t["y"]] for t in targets])
                if stage:
                    staged |= {u["index"] for u in idle if dist(u, *stage) <= STAGE_RADIUS}
                    away = [u for u in idle if u["index"] not in staged]
                    if away:
                        self.send(away, *stage, attack=False)
                    idle = [u for u in idle if u["index"] in staged]
                for grp in self.groups(idle):
                    hit = [t for t in targets if (self.is_flyer(t["type"]) and any(self.hits_air(u["type"]) for u in grp))
                           or (not self.is_flyer(t["type"]) and any(self.hits_ground(u["type"]) for u in grp))]
                    if not hit:
                        continue
                    gx, gy = grp[0]["x"], grp[0]["y"]
                    t = min(hit, key=lambda t: abs(t["x"] - gx) + abs(t["y"] - gy))
                    self.send(grp, t["x"], t["y"])
                    self.rec.event("attack", group=len(grp), target=unit_brief(t), targets=len(targets))
                self.pl.frames(every)

    def bring(self, unit_type: int, rect: tuple[int, int, int, int], count: int = 1, max_frames: int = 40000,
              point: tuple[int, int] | None = None) -> None:
        """Moves `count` units of the type into the location rectangle (left, top, right, bottom)."""
        self.bring_all({unit_type: count}, rect, max_frames, point)

    def bring_all(self, wanted: dict[int, int], rect: tuple[int, int, int, int], max_frames: int = 40000,
                  point: tuple[int, int] | None = None) -> None:
        """Moves the units of each type (type -> count) into the location rectangle until the result; `point`
        (default: the rectangle's centre) is where they are sent."""
        cx, cy = point or ((rect[0] + rect[2]) // 2, (rect[1] + rect[3]) // 2)
        with StopAfterResult(self.rec):
            while self.rec.result is None and self.pl.frame() < max_frames:
                for unit_type, count in wanted.items():
                    units = self.pl.mine([unit_type])
                    inside = [u for u in units if rect[0] <= u["x"] <= rect[2] and rect[1] <= u["y"] <= rect[3]]
                    if len(inside) >= count:
                        continue
                    # not only the idle ones: a new transport stands with an order of its own
                    idle = [u for u in units if u not in inside and u["order"] != ORDER_MOVE]
                    if idle:
                        self.send(idle, cx, cy, attack=False)
                        self.rec.event("bring", type=unit_type, sent=len(idle), inside=len(inside),
                                       orders=[u["order"] for u in idle])
                self.pl.frames(96)

    def strike(self, players: list[int], types: Iterable[int] | None = None, buildings_only: bool = False,
               near: int = 480, every: int = 96, max_frames: int = 40000, tick: Callable[[], None] | None = None,
               flyers: bool | None = None, done: Callable[[], bool] | None = None,
               army: Callable[[list[dict]], list[dict]] | None = None,
               rects: list[tuple[int, int, int, int]] | None = None) -> None:
        """Until the result: idle army groups (only flyers or ground units with `flyers`) far from the nearest
        target move without fighting to a point `near` / 2 short of it (so that none stands on it); groups within
        `near` attack that target itself (A + a click on it, again a little aside while the click took one of the
        player's own units). A unit near a target but fighting something else is sent at the target again, so the
        enemy's stream of units does not keep it from the objective. Ends early once `done()`; `army(units)`
        narrows the units that take part; `rects` the targets (those inside one of the rectangles)."""
        last = -STATUS_EVERY
        pick = army
        with StopAfterResult(self.rec):
            while self.rec.result is None and self.pl.frame() < max_frames and not (done and done()):
                if tick:
                    tick()
                targets = self.targets(players, types, buildings_only, rects)
                army = self.army(flyers)
                if pick:
                    army = pick(army)
                aimed = {t["ptr"] for t in targets}
                # idle, or near a target but busy with something else (the AI's units keep coming)
                idle = [u for u in army if u["order"] in IDLE_ORDERS
                        or (u["order"] != ORDER_MOVE and u["target_unit"] not in aimed
                            and any(dist(u, t["x"], t["y"]) <= near for t in targets))]
                if self.pl.frame() - last >= STATUS_EVERY:
                    last = self.pl.frame()
                    self.rec.event("status", army=[[u["type"], u["x"], u["y"], u["order"], self.aim(u)] for u in army],
                                   targets=[[t["type"], t["owner"], t["x"], t["y"], t["hp"], hex(t["status"])]
                                            for t in targets])
                for grp in self.groups(idle) if targets else ():
                    gx, gy = grp[0]["x"], grp[0]["y"]
                    t = min(targets, key=lambda t: dist(t, gx, gy))
                    d = dist(grp[0], t["x"], t["y"])
                    if d > near:
                        k = near // 2
                        x, y = t["x"] + (gx - t["x"]) * k // d, t["y"] + (gy - t["y"]) * k // d
                        self.send(grp, x, y, attack=False)
                        self.rec.event("approach", group=len(grp), at=[gx, gy], to=[x, y], target=unit_brief(t))
                    elif self.select(grp):
                        was, t = t, self.pl.unit(t["ptr"])
                        if (t["type"], t["owner"]) != (was["type"], was["owner"]):
                            continue  # the target died meanwhile and its slot holds another unit
                        self.pl.center(t["x"], t["y"])  # before A: a minimap click after it would attack-move
                        for dx, dy in ((0, 0), (-24, 16), (24, 16), (0, -24)):
                            self.pl.hotkey(ord("A"))
                            self.pl.click_map(t["x"] + dx, t["y"] + dy)
                            if self.pl.unit(grp[0]["ptr"])["target_unit"] == t["ptr"]:
                                break
                        else:  # no click took the target: a click that took one of the player's own units ends
                            aimed_at = self.pl.unit(grp[0]["ptr"])["target_unit"]
                            if aimed_at and self.pl.unit(aimed_at)["owner"] == self.pl.local:
                                self.halt(grp)
                                self.rec.event("strike_missed", group=len(grp), target=unit_brief(t))
                                continue
                        self.rec.event("strike", group=len(grp), target=unit_brief(t), targets=len(targets))
                self.pl.frames(every)

    def travel(self, types: Iterable[int], waypoints: Iterable[tuple[int, ...]], radius: int = 128, every: int = 96,
               leg_frames: int = 6000, attack: bool = False) -> bool:
        """The units of the types move (without fighting, or attack-moving with `attack`) through the waypoints,
        each (x, y) or (x, y, cx, cy):
        a leg ends when one of them stands within `radius` of (cx, cy) (default: the waypoint itself; a teleporter
        pad's leg checks its destination). False when a leg times out or the mission ends."""
        for w in waypoints:
            x, y, cx, cy = (*w, *w) if len(w) == 2 else w
            end = self.pl.frame() + leg_frames
            with StopAfterResult(self.rec):
                while self.rec.result is None:
                    units = self.pl.mine(list(types))
                    if any(dist(u, cx, cy) <= radius for u in units):
                        self.rec.event("waypoint", at=[x, y], frame=self.pl.frame())
                        break
                    if self.pl.frame() > end:
                        self.rec.event("waypoint_timeout", at=[x, y], units=[unit_brief(u) for u in units])
                        return False
                    idle = [u for u in units if u["order"] in IDLE_ORDERS]
                    if idle:
                        self.send(idle, x, y, attack=attack)
                        self.rec.event("travel", to=[x, y], sent=len(idle),
                                       units=[[u["x"], u["y"]] for u in units])
                    self.pl.frames(every)
            if self.rec.result is not None:
                return False
        return True

    def select_box(self, u: dict, r: int = 24) -> bool:
        """Selects the player's units in a small box dragged around the unit (a box takes no enemy, which a click
        on a crowded spot may); True when the unit is among them."""
        pl = self.pl
        u = pl.unit(u["ptr"])
        pl.ensure_visible(u["x"], u["y"])
        u = pl.unit(u["ptr"])
        sx, sy = pl.screen_point(u["x"], u["y"])
        pl.inp.drag(max(sx - r, 2), max(sy - r, 2), min(sx + r, 637), min(sy + r, 330))
        pl.wait(150)
        return u["index"] in {v["index"] for v in pl.selection()}

    def assemble(self, wanted: dict[int, int], rect: tuple[int, int, int, int], point: tuple[int, int] | None = None,
                 near: int = 1600, max_frames: int = 40000, every: int = 96) -> None:
        """Like `bring_all`, until the result the units of each type (type -> count; those within `near` of the
        rectangle) move into the location rectangle, but each is selected with a box (enemies crowding a unit
        take a click) and, once inside, holds its position (H), so that it does not chase an enemy out again."""
        pl = self.pl
        cx, cy = point or ((rect[0] + rect[2]) // 2, (rect[1] + rect[3]) // 2)
        held: set[int] = set()
        with StopAfterResult(self.rec):
            while self.rec.result is None and pl.frame() < max_frames:
                for unit_type in wanted:
                    for u in pl.mine([unit_type]):
                        if dist(u, cx, cy) > near or u["status"] & S_IN_TRANSPORT:
                            continue
                        if in_rect(u, rect):
                            if u["index"] not in held and self.select_box(u):
                                pl.hotkey(ord("H"))
                                held.add(u["index"])
                                self.rec.event("hold", unit=unit_brief(pl.unit(u["ptr"])))
                            continue
                        held.discard(u["index"])
                        if u["order"] != ORDER_MOVE and self.select_box(u):
                            pl.minimap_click(cx, cy, right=True)
                            self.rec.event("assemble", unit=unit_brief(u), selection=len(pl.selection()))
                pl.frames(every)

    def approach(self, u: dict, x: int, y: int, done: Callable[[], bool], max_frames: int = 4000) -> bool:
        """One unit moves (without fighting) to the exact point (x, y) until `done()`: selected with a box, a right
        click on the point in the view (a minimap click lands on its coarse grid, on a large map up to a tile and
        a half away), again whenever it stands idle."""
        pl = self.pl
        end = pl.frame() + max_frames
        with StopAfterResult(self.rec):
            while self.rec.result is None and pl.frame() < end:
                if done():
                    return True
                v = pl.unit(u["ptr"])
                if v["order"] in UNIT_IDLE_ORDERS and self.select_box(v):
                    pl.click_map(x, y, right=True)
                    self.rec.event("approach_point", at=[v["x"], v["y"]], to=[x, y])
                pl.frames(48)
        return False

    def halt(self, units: list[dict]) -> None:
        """Each group of the units stops (S): an attack order that a click turned on one of the player's own
        units ends."""
        for grp in self.groups(units):
            if select_group(self.pl, grp):
                self.pl.hotkey(ord("S"))

    def withdraw(self, players: list[int], radius: int, x: int, y: int) -> None:
        """Army units within `radius` of a building of the players (e.g. ones that must survive) move (without
        fighting) to (x, y)."""
        keep = self.targets(players, buildings_only=True)
        near = [u for u in self.army() if u["order"] != ORDER_MOVE and any(dist(u, b["x"], b["y"]) <= radius
                                                                         for b in keep)]
        if near:
            self.send(near, x, y, attack=False)
            self.rec.event("withdraw", units=[[u["type"], u["x"], u["y"], u["order"]] for u in near])

    # ---------------------------------------------------------------- base
    def complete(self, types: Iterable[int]) -> list[dict]:
        return [u for u in self.pl.mine(list(types)) if u["status"] & STATUS_COMPLETED]

    def build(self, building_type: int, near: tuple[int, int], r0: int = 96, r1: int = 480, step: int = 64,
              points: list[tuple[int, int]] | None = None, worker: int = T_SCV) -> tuple[int, int] | None:
        """A worker of the type (an SCV, or a Drone) that is not building (the nearest first) places the building
        at the first valid point of square rings around `near` (or of `points`)."""
        scvs = sorted((u for u in self.pl.mine([worker]) if u["order"] not in BUILD_ORDERS),
                      key=lambda u: dist(u, *near))
        if not scvs:
            return None
        advanced = {T_DRONE: ZERG_ADVANCED, T_PROBE: PROTOSS_ADVANCED}.get(worker, ADVANCED_BUILDINGS)
        menu = ord("V") if building_type in advanced else ord("B")
        at = place_first_valid(self.pl, self.rec, scvs, menu, building_type,
                               points or spiral(near[0], near[1], r0, r1, step), f"build_{building_type}")
        self.rec.event("build", building=building_type, at=at)
        return at

    def build_and_wait(self, building_type: int, near: tuple[int, int], max_frames: int = 3000,
                       **kw: Any) -> bool:
        """Places one more building of the type and waits until one more of them is completed."""
        n = len(self.complete([building_type]))
        if self.build(building_type, near, **kw) is None:
            return False
        return self.pl.until(lambda: len(self.complete([building_type])) > n, max_frames=max_frames)

    def refinery(self, near: tuple[int, int]) -> bool:
        """Builds a Refinery on the geyser nearest to `near` and waits for it."""
        gs = self.pl.units(owner=NEUTRAL, types=[T_GEYSER])
        if not gs:
            return False
        g = min(gs, key=lambda g: dist(g, *near))
        return self.build_and_wait(110, near, points=[(g["x"] + dx, g["y"] + dy) for dy in range(-64, 33, 16)
                                                      for dx in range(-96, 33, 16)])

    def addon(self, building: dict, key: int, addon_type: int, max_frames: int = 3000) -> bool:
        """The building builds an add-on (its hotkey). The game asks for the place of the building with its add-on:
        a click where the building stands with it keeps it there (a click elsewhere would fly it there), then points
        near it. Waits until the add-on is completed."""
        n = len(self.complete([addon_type]))
        b = self.pl.unit(building["ptr"])
        if not select_building(self.pl, b):
            return False
        self.pl.hotkey(key)
        if self.pl.m.r8(PLACE_ACTIVE):
            # the place is the building's with the add-on: its centre lies 32 px right of the building's
            for dx, dy in [(32, 0), (0, 0)] + [(dx, dy) for dy in (0, -32, 32) for dx in (64, -32) if dx or dy]:
                self.pl.click_map(b["x"] + dx, b["y"] + dy)
                if not self.pl.m.r8(PLACE_ACTIVE):
                    break
            else:
                self.pl.hotkey(0x1B)
                self.rec.event("addon_no_valid_spot", building=unit_brief(b), addon=addon_type)
                return False
        self.rec.event("addon", building=unit_brief(b), addon=addon_type)
        ok = self.pl.until(lambda: len(self.complete([addon_type])) > n, max_frames=max_frames)
        self.rec.event("addon_done", ok=ok, building=unit_brief(self.pl.unit(building["ptr"])))
        return ok

    def land(self, building: dict, near: tuple[int, int], r1: int = 480, max_frames: int = 1500) -> bool:
        """A lifted Terran building lands (L, then clicks at the first valid point of square rings around
        `near`); waits until it stands on the ground."""
        pl = self.pl
        b = pl.unit(building["ptr"])
        if not b["status"] & S_IN_AIR:
            return True
        if not select_building(pl, b, offsets=LIFTED_CLICKS):
            self.rec.event("land_not_selected", building=unit_brief(b))
            return False
        pl.hotkey(ord("L"))
        if not pl.m.r8(PLACE_ACTIVE):
            self.rec.event("land_not_started", building=unit_brief(b))
            return False
        for x, y in spiral(near[0], near[1], 0, r1, 32):
            pl.ensure_visible(x, y)
            pl.inp.click(*pl.screen_point(x, y))
            pl.wait(150)
            if not pl.m.r8(PLACE_ACTIVE):
                break
        else:
            pl.hotkey(0x1B)
            self.rec.event("land_no_valid_spot", building=unit_brief(b))
            return False
        ok = pl.until(lambda: not pl.unit(b["ptr"])["status"] & S_IN_AIR, max_frames=max_frames)
        self.rec.event("landed", ok=ok, building=unit_brief(pl.unit(b["ptr"])))
        return ok

    def rally(self, buildings: list[dict], x: int, y: int) -> None:
        """Each building's rally point is set to (x, y): select it, right-click the minimap."""
        for b in buildings:
            if select_building(self.pl, self.pl.unit(b["ptr"])):
                self.pl.minimap_click(x, y, right=True)
                self.rec.event("rally", building=unit_brief(b), point=[x, y])

    def money(self) -> None:
        """Types "show me the money" again when the minerals run low."""
        if self.pl.resources()[0] < MONEY_FLOOR:
            self.cheats(("show me the money",))

    def queue_empty(self, b: dict) -> bool:
        """The building trains nothing: its queue is a ring whose current slot is at +0xc8."""
        return b["queue"][self.pl.m.r8(b["ptr"] + U_QUEUE_SLOT) % 5] in (0, 228)

    def train(self, producer_types: Iterable[int], key: int, limit: int = 1) -> int:
        """Each completed producer of the types with an empty queue gets `limit` presses of the train hotkey."""
        n = 0
        for b in self.pl.mine(list(producer_types)):
            if not b["status"] & STATUS_COMPLETED or not self.queue_empty(b):
                continue
            if select_building(self.pl, self.pl.unit(b["ptr"])):
                for _ in range(limit):
                    self.pl.hotkey(key)
                n += 1
        return n

    # ---------------------------------------------------------------- Zerg
    def morph(self, key: int) -> int:
        """Each completed Hatchery (Lair, Hive) with larvae near it: select it (a click above the larvae at its
        foot), select its larvae (S), and morph them all (the unit's hotkey). Returns the larvae morphed."""
        n = 0
        for h in self.complete(HATCHERIES):
            if not any(dist(lv, h["x"], h["y"]) <= LARVA_NEAR for lv in self.pl.mine([T_LARVA])):
                continue
            if not select_building(self.pl, self.pl.unit(h["ptr"]), offsets=HATCHERY_CLICKS):
                continue
            self.pl.hotkey(ord("S"))
            sel = self.pl.selection()
            if sel and all(u["type"] == T_LARVA for u in sel):
                self.pl.hotkey(key)
                n += len(sel)
        return n

    def ventral_sacs(self, max_frames: int = 6000) -> bool:
        """A Hatchery morphs into a Lair (L), unless there is one, and the Lair researches Ventral Sacs (V), so that
        Overlords carry units. Waits until the Lair stands idle again."""
        pl = self.pl
        lairs = self.complete([T_LAIR])
        if not lairs:
            h = self.complete([HATCHERIES[0]])[0]
            if select_building(pl, pl.unit(h["ptr"]), offsets=HATCHERY_CLICKS):
                pl.hotkey(ord("L"))
            ok = pl.until(lambda: pl.unit(h["ptr"])["type"] == T_LAIR
                          and pl.unit(h["ptr"])["order"] == ORDER_NOTHING, max_frames=max_frames)
            self.rec.event("lair", ok=ok, unit=unit_brief(pl.unit(h["ptr"])))
            lairs = [pl.unit(h["ptr"])]
        lair = lairs[0]
        if not select_building(pl, pl.unit(lair["ptr"]), offsets=HATCHERY_CLICKS):
            return False
        pl.hotkey(ord("V"))
        pl.frames(8)
        started = pl.unit(lair["ptr"])["order"] != ORDER_NOTHING
        ok = pl.until(lambda: pl.unit(lair["ptr"])["order"] == ORDER_NOTHING, max_frames=max_frames)
        self.rec.event("ventral_sacs", started=started, ok=ok)
        return ok

    def airlift(self, passenger: dict, x: int, y: int, max_frames: int = 12000) -> bool:
        """The passenger boards the nearest Overlord (selected, right click on the Overlord), which then unloads it
        at (x, y) (Unload All and a click there)."""
        pl = self.pl
        p = pl.unit(passenger["ptr"])
        ovl = min(pl.mine([T_OVERLORD]), key=lambda o: dist(o, p["x"], p["y"]))
        end = pl.frame() + max_frames
        with StopAfterResult(self.rec):
            while not pl.unit(p["ptr"])["status"] & S_IN_TRANSPORT:
                if pl.frame() > end or self.rec.result is not None:
                    self.rec.event("airlift_not_loaded", passenger=unit_brief(pl.unit(p["ptr"])))
                    return False
                if pl.unit(p["ptr"])["order"] in IDLE_ORDERS:
                    pl.select_unit(pl.unit(p["ptr"]))
                    o = pl.unit(ovl["ptr"])
                    pl.click_map(o["x"], o["y"], right=True)
                    self.rec.event("board", passenger=unit_brief(pl.unit(p["ptr"])), overlord=unit_brief(o))
                pl.frames(96)
            self.rec.event("boarded", overlord=unit_brief(pl.unit(ovl["ptr"])))
            while pl.unit(p["ptr"])["status"] & S_IN_TRANSPORT:
                if pl.frame() > end or self.rec.result is not None:
                    self.rec.event("airlift_not_unloaded", overlord=unit_brief(pl.unit(ovl["ptr"])))
                    return False
                o = pl.unit(ovl["ptr"])
                if o["order"] in IDLE_ORDERS + (ORDER_MOVE, ORDER_PICKUP_IDLE) and select_building(pl, o):
                    pl.button(UNLOAD_ALL_SLOT)
                    pl.center(x, y)
                    pl.inp.click(*pl.screen_point(x, y))
                    pl.wait(150)
                    self.rec.event("unload", overlord=unit_brief(pl.unit(ovl["ptr"])), at=[x, y])
                pl.frames(240)
        self.rec.event("airlifted", passenger=unit_brief(pl.unit(p["ptr"])))
        return True

    def ferry(self, passengers: list[dict], transport_type: int, x: int, y: int, max_frames: int = 12000) -> bool:
        """The passengers board the transport of the type nearest to the first (each selected alone, right click
        on the transport), which then unloads them at (x, y) (Unload All and a click there)."""
        pl = self.pl
        ps = [pl.unit(p["ptr"]) for p in passengers]
        tr = min(pl.mine([transport_type]), key=lambda o: dist(o, ps[0]["x"], ps[0]["y"]))
        end = pl.frame() + max_frames

        def aboard() -> list[bool]:
            return [bool(pl.unit(p["ptr"])["status"] & S_IN_TRANSPORT) for p in ps]
        with StopAfterResult(self.rec):
            while not all(aboard()):
                if pl.frame() > end or self.rec.result is not None:
                    self.rec.event("ferry_not_loaded", passengers=[unit_brief(pl.unit(p["ptr"])) for p in ps])
                    return False
                for p, inside in zip(ps, aboard()):
                    u = pl.unit(p["ptr"])
                    if inside or u["order"] not in UNIT_IDLE_ORDERS:
                        continue
                    pl.select_unit(u)
                    if [v["index"] for v in pl.selection()] != [u["index"]]:
                        continue
                    o = pl.unit(tr["ptr"])
                    pl.click_map(o["x"], o["y"], right=True)
                    self.rec.event("board", passenger=unit_brief(pl.unit(u["ptr"])), transport=unit_brief(o))
                pl.frames(96)
            self.rec.event("boarded", transport=unit_brief(pl.unit(tr["ptr"])))
            while any(aboard()):
                if pl.frame() > end or self.rec.result is not None:
                    self.rec.event("ferry_not_unloaded", transport=unit_brief(pl.unit(tr["ptr"])))
                    return False
                o = pl.unit(tr["ptr"])
                if o["order"] in IDLE_ORDERS + (ORDER_MOVE, ORDER_PICKUP_IDLE) and select_building(pl, o):
                    pl.button(UNLOAD_ALL_SLOT)
                    pl.center(x, y)
                    pl.inp.click(*pl.screen_point(x, y))
                    pl.wait(150)
                    self.rec.event("unload", transport=unit_brief(pl.unit(tr["ptr"])), at=[x, y])
                pl.frames(240)
        self.rec.event("ferried", passengers=[unit_brief(pl.unit(p["ptr"])) for p in ps])
        return True

    def carry(self, worker: dict, powerup_type: int, rect: tuple[int, int, int, int],
              point: tuple[int, int] | None = None, max_frames: int = 20000) -> None:
        """The worker picks up the powerup of the type (a right click on it) and carries it into the location
        rectangle, until the result; it picks the powerup up again if it lost it."""
        cx, cy = point or ((rect[0] + rect[2]) // 2, (rect[1] + rect[3]) // 2)
        last = -PICK_UP_AGAIN
        with StopAfterResult(self.rec):
            while self.rec.result is None and self.pl.frame() < max_frames:
                u = self.pl.unit(worker["ptr"])
                if u["type"] != worker["type"] or u["owner"] != self.pl.local:
                    self.rec.event("carrier_lost", unit=unit_brief(u))
                    return
                if not self.pl.m.r32(u["ptr"] + POWERUP):
                    items = self.pl.units(types=[powerup_type])
                    if items and (u["order"] in IDLE_ORDERS or self.pl.frame() - last >= PICK_UP_AGAIN):
                        last = self.pl.frame()
                        it = min(items, key=lambda p: dist(p, u["x"], u["y"]))
                        self.pl.select_unit(u)
                        self.pl.click_map(it["x"], it["y"], right=True)
                        self.rec.event("pick_up", at=[u["x"], u["y"]], item=[it["x"], it["y"]],
                                       order=self.pl.unit(u["ptr"])["order"])
                elif u["order"] in IDLE_ORDERS and not in_rect(u, rect):
                    self.send([u], cx, cy, attack=False)
                    self.rec.event("carry", at=[u["x"], u["y"]], powerup=hex(self.pl.m.r32(u["ptr"] + POWERUP)))
                self.pl.frames(96)


def begin(name: str, warp: str, cd: str, inject: tuple[int, ...] | None = None,
          cheats: Iterable[str] = PLAY_CHEATS) -> tuple[Session, Player, Recorder, Kit]:
    s, pl = start(warp, cd, inject=inject)
    rec = Recorder(s, pl, RECORDS / name)
    enter_game(s, pl)
    kit = Kit(s, pl, rec)
    kit.cheats(cheats)
    return s, pl, rec, kit


def finish(rec: Recorder, pl: Player, extra: dict | None = None) -> dict:
    pl.frames(48)
    return rec.finish({"status": "victory" if rec.result and rec.result["value"] == 3 else "no victory",
                       **(extra or {})})


NEXT_SCENARIO = 0x526526  # the file name the last SetNextScenario action left


def next_scenario(pl: Player) -> str:
    return pl.m.cstr(NEXT_SCENARIO, 64).decode("latin-1")


def finish_next(rec: Recorder, pl: Player, extra: dict | None = None) -> dict:
    """`finish`, with the next scenario the mission's triggers left."""
    nxt = next_scenario(pl)
    rec.event("next_scenario", file=nxt)
    return finish(rec, pl, {"next_scenario": nxt, **(extra or {})})


# ==================================================================== the plays (run names as the idle runs)

def destroy_play(name: str, warp: str, cd: str, code: int, types: Iterable[int] | None = None,
                 buildings_only: bool = True, inject: tuple[int, ...] | None = None,
                 max_frames: int = 40000) -> dict:
    """A row won by destroying the targets of a Command condition ("at most 0"): conquer them."""
    s, pl, rec, kit = begin(name, warp, cd, inject)
    kit.conquer(kit.players(code), types, buildings_only, max_frames=max_frames)
    return finish(rec, pl)


def terran_m03(out_name: str = "terran-m03") -> dict:
    """terran02 (Backwater Station): Force 2 commands no Infested Command Center (130)."""
    return destroy_play(out_name, "terran2", "original", 19, types={130}, buildings_only=False)


def terran_m01(out_name: str = "terran-m01") -> dict:
    """tutorial (Boot Camp): the victory trigger wants the current player (slot 1) to command 3 Supply Depots
    (109) and a Refinery (110), to have 100 gas and no running transmission (switch 6 cleared). "show me the
    money" brings the gas; the Command Center trains two SCVs, which build two more Supply Depots, and an SCV
    builds the Refinery on the geyser."""
    s, pl, rec, kit = begin(out_name, "terran0", "original")
    cc = pl.mine([106])[0]
    near = (cc["x"], cc["y"])
    kit.train([106], ord("S"), limit=2)
    pl.until(lambda: len(kit.complete([T_SCV])) >= 3, max_frames=2000)
    for _ in range(2):
        kit.build(109, near, r0=128, r1=448)
    kit.refinery(near)
    kit.wait_result(max_frames=12000, every=96)
    return finish(rec, pl)


def terran_m04(out_name: str = "terran-m04") -> dict:
    """terran03 (Desperate Alliance): survive 30 minutes. The victory trigger fires when the 1800-second countdown
    reaches 0 (switch 0 is set by the opening trigger; the other victory trigger needs switch 1, which no trigger
    sets). "power overwhelming" keeps every unit and building alive while the time runs out."""
    s, pl, rec, kit = begin(out_name, "terran3", "original")
    kit.wait_result(max_frames=40000, every=480)
    return finish(rec, pl)


def terran_m05(out_name: str = "terran-m05") -> dict:
    """terran04 (The Jacobs Installation): the victory trigger fires on switch 4, which the trigger that brings a
    man of the player to "Data Disks" (2400,1984)-(2496,2048) sets. The squad (Raynor, Marines, Firebats) walks
    through the installation, its doors opening as it comes, to the Sector 1 teleporter, the only way to the
    console's part of the installation, and from its destination to the console."""
    s, pl, rec, kit = begin(out_name, "terran4", "original")
    squad = {0, 20, 32}
    # the game's region groups: the start and the Sector 1 pad (976,192) are in one, the console and the pad's
    # destination (3008,224)-(3168,384) in another; the walk from there goes south-west, away from the Sector 2 pad
    kit.travel(squad, [(976, 192, 3088, 304), (2448, 2016)], leg_frames=10000)
    kit.wait_result(max_frames=pl.frame() + 2000, every=48)
    return finish(rec, pl)


def march(kit: Kit, x: int, y: int, done: Callable[[], bool], max_frames: int = 4000) -> bool:
    """The idle army moves (without fighting) to (x, y) until `done()`."""
    pl = kit.pl
    end = pl.frame() + max_frames
    with StopAfterResult(kit.rec):
        while kit.rec.result is None and pl.frame() < end:
            if done():
                return True
            idle = [u for u in kit.army() if u["order"] in IDLE_ORDERS and dist(u, x, y) > 96]
            if idle:
                kit.send(idle, x, y, attack=False)
            pl.frames(96)
    return False


def rescue(kit: Kit, unit_type: int, max_frames: int = 4000) -> bool:
    """The army moves (without fighting) onto a rescuable unit of the type until it joins the local player."""
    them = kit.pl.units(types=[unit_type])
    ok = bool(them) and march(kit, them[0]["x"], them[0]["y"], lambda: bool(kit.pl.mine([unit_type])), max_frames)
    kit.rec.event("rescue", type=unit_type, ok=ok)
    return ok


def starport_tick(kit: Kit, cap: int) -> Callable[[], None]:
    """A conquer tick: each Starport with an empty queue trains a Battlecruiser, or three Wraiths when it cannot,
    while the flyers are fewer than `cap`."""
    def tick() -> None:
        kit.money()
        if len(kit.army(flyers=True)) >= cap:
            return
        for sp in kit.complete([114]):
            if not kit.queue_empty(sp):
                continue
            if select_building(kit.pl, kit.pl.unit(sp["ptr"])):
                kit.pl.hotkey(ord("B"))
                if kit.queue_empty(kit.pl.unit(sp["ptr"])):
                    for _ in range(3):
                        kit.pl.hotkey(ord("W"))
                kit.rec.event("starport_train", starport=unit_brief(kit.pl.unit(sp["ptr"])),
                              button_set=kit.pl.m.r16(BUTTON_SET_SHOWN))
            else:
                kit.rec.event("starport_not_selected", starport=unit_brief(sp))
    return tick


def terran_m06(out_name: str = "terran-m06") -> dict:
    """terran05 (Revolution): the victory trigger fires when Force 2 (the Confederates, slot 6) commands no
    building. Their base is out of reach on the ground (the game's region groups), so it falls to the air: the
    squad rescues Kerrigan (rescuable slot 2) and the Antigan base (rescuable slot 3, joined by the rebels' SCVs
    once Kerrigan stands at its Command Center), an SCV tries a Science Facility for Battlecruisers (the mission
    withholds it), the Starport gets a Control Tower and trains Wraiths, and they conquer the Confederate
    buildings."""
    s, pl, rec, kit = begin(out_name, "terran5", "original")
    rescue(kit, 16)
    march(kit, 2672, 552, lambda: bool(kit.complete([106])) and bool(kit.complete([T_SCV])), max_frames=6000)
    cc = kit.complete([106])[0]
    near = (cc["x"], cc["y"])
    kit.build_and_wait(116, near, r0=160, r1=640)
    for sf in kit.complete([116])[:1]:
        kit.addon(sf, ord("P"), 118)
    for sp in kit.complete([114])[:1]:
        kit.addon(sp, ord("C"), 115)
    kit.conquer(kit.players(FORCE_1 + 1), tick=starport_tick(kit, 24), flyers=True)
    return finish(rec, pl)


def terran_m07(out_name: str = "terran-m07") -> dict:
    """terran06 (Norad II): the victory trigger wants Raynor's Vulture (19) and 2 Dropships (11) of the current
    player at "Crash Site" (2176,2176)-(2400,2304). An SCV builds a Starport ("modify the phase variance" lifts its
    requirements; otherwise a Command Center, Barracks and Factory come first), builds its Control Tower and trains 2
    Dropships, and they and Raynor move to the crash site beside the Norad II."""
    s, pl, rec, kit = begin(out_name, "terran6", "original")
    scv = pl.mine([T_SCV])[0]
    near = (scv["x"] + 160, scv["y"] - 160)  # east of "Antigan town", whose Command Center wakes the AI
    if not kit.build_and_wait(114, near, r1=640):
        for t in (106, 111, 113, 114):
            if not kit.complete([t]):
                kit.build_and_wait(t, near, r1=640)
    kit.addon(kit.complete([114])[0], ord("C"), 115)
    kit.train([114], ord("D"), limit=2)
    pl.until(lambda: len(kit.complete([11])) >= 2, max_frames=3000)
    kit.bring_all({19: 1, 11: 2}, (2176, 2176, 2400, 2304), point=(2208, 2240))
    return finish(rec, pl)


POWERUP = 0xC0  # CUnit: a worker's carried powerup (BWAPI layout)


def terran_m08(out_name: str = "terran-m08") -> dict:
    """terran08 (The Trump Card): the victory trigger wants the Psi Emitter (217) of player 1 at "Beacon"
    (3616,1568)-(3776,1696) in the Confederate base. The opening trigger hands the emitter to the SCV at
    "SCV / Emitter"; that SCV walks it to the beacon."""
    s, pl, rec, kit = begin(out_name, "terran7", "original")
    pl.frames(24)
    scv = min(pl.mine([T_SCV]), key=lambda u: dist(u, 1056, 960))
    rec.event("emitter_carrier", scv=unit_brief(scv), powerup=hex(pl.m.r32(scv["ptr"] + POWERUP)))
    rect = (3616, 1568, 3776, 1696)
    with StopAfterResult(rec):
        while rec.result is None and pl.frame() < 20000:
            u = pl.unit(scv["ptr"])
            if u["order"] in IDLE_ORDERS and not (rect[0] <= u["x"] <= rect[2] and rect[1] <= u["y"] <= rect[3]):
                kit.send([u], 3696, 1632, attack=False)
                rec.event("carry", at=[u["x"], u["y"]], powerup=hex(pl.m.r32(u["ptr"] + POWERUP)))
            pl.frames(96)
    return finish(rec, pl)


ARMY_CAP = 72  # units: production stops at this army size


def production(kit: Kit, barracks_rally: tuple[int, int], cap: int = ARMY_CAP) -> Callable[[], None]:
    """A conquer tick: Barracks train Marines (5 per empty queue) towards the rally point while the army is below
    `cap`; "show me the money" again when the minerals run low."""
    rallied: set[int] = set()

    def tick() -> None:
        kit.money()
        bs = kit.complete([111])
        new = [b for b in bs if b["index"] not in rallied]
        if new:
            kit.rally(new, *barracks_rally)
            rallied.update(b["index"] for b in new)
        if len(kit.army()) < cap:
            kit.train([111], ord("M"), limit=5)
    return tick


def terran_m09(out_name: str = "terran-m09") -> dict:
    """terran09 (The Big Push): the victory trigger fires when no non-allied player commands a building. The base's
    plateau has no ground way to the Confederate bases (the game's region groups: its Marines stay home), so the
    war goes by air: the Starport and a second one get Control Towers (the Starport trains nothing without it
    here) and train Wraiths (the mission withholds the Physics Lab, so no Battlecruisers), and they and Duke's
    Norad II attack the buildings of slots 4 and 5 one by one."""
    s, pl, rec, kit = begin(out_name, "terran8", "original")
    cc = pl.mine([106])[0]
    first = kit.complete([114])[0]
    kit.addon(first, ord("C"), 115)
    if kit.build_and_wait(114, (cc["x"] - 160, cc["y"] - 160), r0=96, r1=640):
        kit.addon([u for u in kit.complete([114]) if u["index"] != first["index"]][0], ord("C"), 115)
    kit.strike(kit.players(NON_ALLIED_VICTORY), buildings_only=True, tick=starport_tick(kit, 48), flyers=True)
    return finish(rec, pl)


def terran_m10(out_name: str = "terran-m10") -> dict:
    """terran11 (New Gettysburg): the victory trigger fires when Force 2 (the Protoss, slot 1) commands no building;
    a Zerg building (Force 3) lost is a defeat. Two more Barracks train Marines, rallied east of the base, away
    from the Zerg; the army moves (without fighting) to the nearest Protoss building and attacks that building
    itself, so it does not spend itself on the Protoss AI's stream of units. A unit that chases Zerg raiders
    towards their base (within 800 px of a Zerg building) is called back to the rally point."""
    s, pl, rec, kit = begin(out_name, "terran9", "original")
    b = pl.mine([111])[0]
    for _ in range(2):
        kit.build(111, (b["x"] + 128, b["y"]), r1=512)
    rally = (2100, 1000)
    produce = production(kit, rally)
    zerg = kit.players(FORCE_1 + 2)

    def tick() -> None:
        kit.withdraw(zerg, 800, *rally)
        produce()
    kit.strike(kit.players(FORCE_1 + 1), buildings_only=True, tick=tick)
    return finish(rec, pl)


def terran_m11(out_name: str = "terran-m11") -> dict:
    """terran12 (The Hammer Falls): the victory trigger fires when no player commands an Ion Cannon (127). The
    cannon stands on a plateau no ground unit of the player reaches (the game's region groups), so it falls to the
    air: SCVs build two Starports, they train Battlecruisers and Wraiths, and the flyers go to the cannon and
    attack it. "the gathering" is left out here: the energy cheat feeds the computer's Ghosts too, whose endless
    Lockdowns held every flyer near the cannon; with their own energy they lock down only a few."""
    s, pl, rec, kit = begin(out_name, "terran10", "original",
                            cheats=tuple(c for c in PLAY_CHEATS if c != "the gathering"))
    cc = pl.mine([106])[0]
    for _ in range(2):
        kit.build_and_wait(114, (cc["x"], cc["y"]), r0=160, r1=640)
    kit.strike(kit.players(ALL_PLAYERS), {127}, tick=starport_tick(kit, 24), flyers=True)
    return finish(rec, pl)


T_CHRYSALIS = 216  # the Chrysalis powerup of Zerg 2


def zerg_m13(out_name: str = "zerg-m13") -> dict:
    """zerg02 (Egression): the victory trigger wants the Chrysalis (216) of the current player at "Warp Point"
    (2368,2400)-(2464,2464). A Drone picks the Chrysalis up beside the base and carries it across the Protoss
    land ("power overwhelming" keeps it alive) to the beacon."""
    s, pl, rec, kit = begin(out_name, "zerg2", "original")
    item = pl.units(types=[T_CHRYSALIS])[0]
    drone = min(pl.mine([T_DRONE]), key=lambda u: dist(u, item["x"], item["y"]))
    kit.carry(drone, T_CHRYSALIS, (2368, 2400, 2464, 2464))
    return finish(rec, pl)


T_SPIRE, T_KERRIGAN, T_TASSADAR, T_NEXUS, T_TEMPLE, T_CRYSTAL = 141, 51, 79, 154, 174, 219
BROOD_CAP = 36  # units: larvae morph no more army beyond this size


def brood(kit: Kit, hatcheries: int = 1, spire: bool = True, near: tuple[int, int] | None = None) -> None:
    """The base of a Zerg army: Drones from larvae until there is one for each building, `hatcheries` more
    Hatcheries (more larvae) and a Spire near the first Hatchery (or `near`)."""
    pl = kit.pl
    if near is None:
        h = kit.complete(HATCHERIES)[0]
        near = (h["x"], h["y"])
    need = hatcheries + int(spire)
    end = pl.frame() + 2000
    while len(pl.mine([T_DRONE])) < need and pl.frame() < end:
        kit.morph(ord("D"))
        pl.frames(96)
    for _ in range(hatcheries):
        kit.build(HATCHERIES[0], near, r0=256, r1=704, worker=T_DRONE)
    if spire:
        kit.build_and_wait(T_SPIRE, near, max_frames=4000, r0=96, r1=384, step=32, worker=T_DRONE)
    kit.rec.event("brood", buildings=sorted(u["type"] for u in pl.mine() if kit.is_building(u["type"])))


def brood_tick(kit: Kit, key: int, cap: int = BROOD_CAP, flyers: bool | None = None) -> Callable[[], None]:
    """A conquer tick: the hatcheries morph their larvae into the unit of the hotkey while the army (its flyers or
    ground units with `flyers`) is smaller than `cap`; "show me the money" again when the minerals run low."""
    def tick() -> None:
        kit.money()
        if len(kit.army(flyers)) < cap:
            kit.morph(key)
    return tick


def zerg_m14(out_name: str = "zerg-m14") -> dict:
    """zerg03 (The New Dominion): the victory trigger fires on switch 7, which the foes of the current player (the
    Terrans, slot 6) set when they command at most 6 units or no building. A Drone builds a Spire, the larvae
    morph Mutalisks, and the army conquers the Terran buildings (lifted ones too)."""
    s, pl, rec, kit = begin(out_name, "zerg3", "original")
    brood(kit, hatcheries=0)
    kit.conquer(kit.players(FOES), tick=brood_tick(kit, ord("M")))
    return finish(rec, pl)


def zerg_m15(out_name: str = "zerg-m15") -> dict:
    """zerg04 (Agents of the Swarm): the victory trigger fires when Force 3 (Raynor, slot 2) commands no Command
    Center; it has no condition on the Chrysalis's countdown. The CC stands on another island (the game's region
    groups), so it falls to the air: a Drone builds a Spire, the larvae morph Mutalisks, and they attack the CC."""
    s, pl, rec, kit = begin(out_name, "zerg4", "original")
    brood(kit, hatcheries=0)
    kit.strike(kit.players(FORCE_1 + 2), {106}, tick=brood_tick(kit, ord("M"), flyers=True), flyers=True)
    return finish(rec, pl)


def zerg_m16(out_name: str = "zerg-m16") -> dict:
    """zerg05 (The Amerigo): the victory trigger wants Kerrigan (51) at "We have the Disc wave"
    (256,2080)-(352,2176). That room is cut off from the ship's other decks on the ground (the game's region
    groups); the "Teleport1" pad (640,3616)-(736,3680) moves the current player's units to "Teleport2" in it.
    The way to the pad leads through "LockedDoor3", which opens once a unit of Force 1 steps on "UnlockDoor3"
    (2144,2336)-(2272,2432). Kerrigan walks there, then to the pad, the other doors opening as she comes, and
    from its destination attack-moves to the disc: the Floor Missile Traps in the way to it block a walk."""
    s, pl, rec, kit = begin(out_name, "zerg5", "original")
    kit.travel({T_KERRIGAN}, [(2208, 2384), (688, 3648, 784, 1584)], leg_frames=12000)
    kit.travel({T_KERRIGAN}, [(304, 2128)], leg_frames=12000, attack=True)
    kit.wait_result(max_frames=pl.frame() + 3000, every=48)
    return finish(rec, pl)


CAMP_WATCH = ((3600, 3200), (3300, 3750))  # zerg06: points over the Protoss camps
CHALLENGE = (1904, 2032)  # the middle of "kill tassadar" (1792,1920)-(2016,2144) in "Challenge area"


def zerg_m17(out_name: str = "zerg-m17") -> dict:
    """zerg06 (The Dark Templar): switch 0 is set once Force 2 (slots 1, 2) brings no unit into "Protoss Camp (1)"
    (3328,2848)-(4064,3520) and "Protoss Camp (2)" (2720,3520)-(4096,3968); then Kerrigan (51) in "Challenge area"
    with at most one unit of the player in "kill tassadar" brings Tassadar (79) of slot 2, and the victory
    trigger fires once no player commands him. The challenge is a plateau no ground unit reaches (the game's region
    groups). A Drone builds a Spire, the Hatchery becomes a Lair and researches Ventral Sacs, the larvae morph
    Mutalisks, the army conquers every unit in the camps (two Overlords over them detect the Observers), then an
    Overlord carries Kerrigan alone to the challenge, leaves, and she attacks Tassadar."""
    s, pl, rec, kit = begin(out_name, "zerg6", "original")
    brood(kit, hatcheries=0)
    kit.ventral_sacs()
    camps = [(3328, 2848, 4064, 3520), (2720, 3520, 4096, 3968)]
    # Overlords over the camps detect the Observers, which count among the units in them
    for ovl, (x, y) in zip(pl.mine([T_OVERLORD]), CAMP_WATCH):
        kit.send([ovl], x, y, attack=False)
    protoss = kit.players(FORCE_1 + 1)
    kit.conquer(protoss, buildings_only=False, rects=camps, tick=brood_tick(kit, ord("M")),
                done=lambda: not kit.targets(protoss, rects=camps))
    rec.event("camps_cleared")
    kerrigan = pl.mine([T_KERRIGAN])[0]
    if kit.airlift(kerrigan, *CHALLENGE):
        o = min(pl.mine([T_OVERLORD]), key=lambda u: dist(u, *CHALLENGE))
        kit.send([o], CHALLENGE[0] - 640, CHALLENGE[1], attack=False)
    kit.strike(kit.players(ALL_PLAYERS), {T_TASSADAR}, army=lambda units: [u for u in units
                                                                         if u["index"] == kerrigan["index"]])
    return finish(rec, pl)


S_TOWN = (1344, 3040, 2176, 3520)  # zerg07's "S Town"


def zerg_m18(out_name: str = "zerg-m18") -> dict:
    """zerg07 (The Culling): the victory trigger fires when Force 2 (the Garm Brood, slot 4) commands no building.
    The player starts without a Hatchery, below "S Town" (1344,3040)-(2176,3520), the brood's base nearest to it
    (every mineral field of the map lies at one of the brood's bases). The army conquers the buildings of S Town,
    a Drone builds a Hatchery in its place, then a Spire, the larvae morph Mutalisks, and the flyers conquer the
    brood's other buildings (some stand on plateaus no ground unit reaches)."""
    s, pl, rec, kit = begin(out_name, "zerg7", "original")
    garm = kit.players(FORCE_1 + 1)
    town = [S_TOWN]
    kit.conquer(garm, rects=town, done=lambda: not kit.targets(garm, buildings_only=True, rects=town))
    rec.event("town_taken")
    site = (S_TOWN[0] + S_TOWN[2]) // 2, (S_TOWN[1] + S_TOWN[3]) // 2
    kit.build_and_wait(HATCHERIES[0], site, max_frames=4000, r0=0, r1=448, worker=T_DRONE)
    brood(kit, hatcheries=0)
    kit.conquer(kit.players(FORCE_1 + 1), tick=brood_tick(kit, ord("M"), flyers=True), flyers=True)
    return finish(rec, pl)


def zerg_m19(out_name: str = "zerg-m19") -> dict:
    """zerg08 (Eye for an Eye): the victory trigger fires once slots 1 and 2 command no Nexus (154). While slot
    1's Nexus stands, its Dark Templars run for the map's corners (a defeat when one gets there), the first at 90
    game seconds; so the army attacks slot 1's Nexus first, then slot 2's."""
    s, pl, rec, kit = begin(out_name, "zerg8", "original")
    for p in (1, 2):
        kit.strike([p], {T_NEXUS}, done=lambda: not kit.targets([p], {T_NEXUS}))
        rec.event("nexus_destroyed", player=p)
    kit.wait_result(max_frames=pl.frame() + 3000, every=48)
    return finish(rec, pl)


def crystal_carrier(kit: Kit) -> dict | None:
    """The Drone that carries a powerup, else the Drone nearest to the Khaydarin Crystal on the ground (the trigger
    creates the crystal under a new Drone, which may pick it up at once), else None."""
    pl = kit.pl
    held = [u for u in pl.mine([T_DRONE]) if pl.m.r32(u["ptr"] + POWERUP)
            and pl.unit(pl.m.r32(u["ptr"] + POWERUP))["type"] == T_CRYSTAL]
    if held:
        return held[0]
    crystal = pl.units(types=[T_CRYSTAL])
    if not crystal or not pl.mine([T_DRONE]):
        return None
    return min(pl.mine([T_DRONE]), key=lambda u: dist(u, crystal[0]["x"], crystal[0]["y"]))


FORMATION = (1824, 2816, 2080, 2976)  # "khaydarin formation"
RETURN = (448, 416, 608, 544)  # "return location"


def zerg_m20(out_name: str = "zerg-m20") -> dict:
    """zerg09 (The Invasion of Aiur): a Drone brought to "khaydarin formation" (1824,2816)-(2080,2976) starts a
    600-second harvest; then the Khaydarin Crystal (219) appears beside a new Drone, and the victory trigger wants
    the crystal of the current player at "return location" (448,416)-(608,544) by the base. A Drone walks to the
    formation, the base holds ("power overwhelming"), and the new Drone carries the crystal home."""
    s, pl, rec, kit = begin(out_name, "zerg9", "original")
    fx, fy = (FORMATION[0] + FORMATION[2]) // 2, (FORMATION[1] + FORMATION[3]) // 2
    d = min(pl.mine([T_DRONE]), key=lambda u: dist(u, fx, fy))
    ok = march_unit(kit, d, fx, fy, lambda: pl.unit(d["ptr"])["type"] != T_DRONE
                    or pl.unit(d["ptr"])["owner"] != pl.local, max_frames=6000)
    rec.event("formation_reached", ok=ok)
    with StopAfterResult(rec):
        while crystal_carrier(kit) is None and rec.result is None and pl.frame() < 40000:
            pl.frames(480)
    drone = crystal_carrier(kit)
    rec.event("crystal_carrier", drone=unit_brief(drone), crystal=[unit_brief(c) for c in pl.units(types=[T_CRYSTAL])])
    kit.carry(drone, T_CRYSTAL, RETURN, max_frames=pl.frame() + 12000)
    return finish(rec, pl)


def march_unit(kit: Kit, u: dict, x: int, y: int, done: Callable[[], bool], max_frames: int = 4000) -> bool:
    """One unit moves (without fighting) to (x, y) until `done()`; sent again whenever it stands idle."""
    pl = kit.pl
    end = pl.frame() + max_frames
    with StopAfterResult(kit.rec):
        while kit.rec.result is None and pl.frame() < end:
            if done():
                return True
            v = pl.unit(u["ptr"])
            if v["order"] in IDLE_ORDERS:
                kit.send([v], x, y, attack=False)
                kit.rec.event("march", at=[v["x"], v["y"]], to=[x, y])
            pl.frames(48)
    return False


FOUNDATION = (1824, 1984, 2144, 2144)  # "Temple foundation"


def zerg_m21(out_name: str = "zerg-m21") -> dict:
    """zerg10 (Full Circle): the Temple (174) of slot 0 killed by Force 1 brings a Drone and the Khaydarin Crystal
    (219) to "Crystal arrival zone" by the base; the victory trigger wants the crystal of Force 1 at "Temple
    foundation" (1824,1984)-(2144,2144). A Drone builds a Spire, the larvae morph Mutalisks, the army attacks the
    Temple itself, and the Drone carries the crystal to its foundation."""
    s, pl, rec, kit = begin(out_name, "zerg10", "original")
    brood(kit, hatcheries=0)
    kit.strike(kit.players(ALL_PLAYERS), {T_TEMPLE}, tick=brood_tick(kit, ord("M")),
               done=lambda: not kit.targets(kit.players(ALL_PLAYERS), {T_TEMPLE}))
    pl.until(lambda: crystal_carrier(kit) is not None, max_frames=3000)
    drone = crystal_carrier(kit)
    rec.event("crystal_carrier", drone=unit_brief(drone), crystal=[unit_brief(c) for c in pl.units(types=[T_CRYSTAL])])
    kit.carry(drone, T_CRYSTAL, FOUNDATION, max_frames=pl.frame() + 20000)
    return finish(rec, pl)


# ==================================================================== Protoss (Episode III)
T_CEREBRATE, T_OVERMIND, T_STASIS_CELL, T_ZERATUL = 151, 147, 168, 75


def protoss_m23(out_name: str = "protoss-m23") -> dict:
    """protoss02 (Into the Flames): the victory trigger fires once Force 2 (the Zerg, slot 6) commands no
    Cerebrate (151); Fenix, who must survive, comes only with the countdown. The army walks to the Cerebrate
    and attacks it ("power overwhelming" keeps the army alive)."""
    s, pl, rec, kit = begin(out_name, "protoss2", "original")
    kit.strike(kit.players(FORCE_1 + 1), {T_CEREBRATE})
    return finish(rec, pl)


def protoss_m28(out_name: str = "protoss-m28") -> dict:
    """protoss07 (Homeland): switch 0, which brings the victory, is set once Force 1 (the Shrine Defenders,
    slot 4) or Force 2 (slot 0) commands no Nexus (154); Force 1 has a single one. The army (the heroes Fenix,
    Zeratul and Tassadar too, kept alive by "power overwhelming") walks to that Nexus and attacks it."""
    s, pl, rec, kit = begin(out_name, "protoss7", "original")
    kit.strike(kit.players(FORCE_1), {T_NEXUS})
    return finish(rec, pl)


def protoss_m29(out_name: str = "protoss-m29") -> dict:
    """protoss08 (The Trial of Tassadar): the victory trigger fires once no player commands the Stasis Cell (168)
    of slot 0; Fenix and Raynor must survive ("power overwhelming"). The army walks to the cell and attacks it."""
    s, pl, rec, kit = begin(out_name, "protoss8", "original")
    kit.strike(kit.players(ALL_PLAYERS), {T_STASIS_CELL})
    return finish(rec, pl)


def protoss_m30(out_name: str = "protoss-m30") -> dict:
    """protoss09 (Shadow Hunters): each Cerebrate (151) killed while Zeratul (75) stands in its location sets a
    switch (a Cerebrate killed without him there comes back); the victory trigger wants both switches. Zeratul
    alone walks to the Baelrog Cerebrate (Force 3, slot 6), then to the Leviathan one (Force 4, slot 5), and
    attacks each himself ("power overwhelming" keeps him and Fenix alive)."""
    s, pl, rec, kit = begin(out_name, "protoss9", "original")
    zeratul = pl.mine([T_ZERATUL])[0]
    for force in (3, 4):
        foes = kit.players(FORCE_1 + force - 1)
        kit.strike(foes, {T_CEREBRATE}, army=lambda units: [u for u in units if u["index"] == zeratul["index"]],
                   done=lambda: not kit.targets(foes, {T_CEREBRATE}))
        rec.event("cerebrate_killed", force=force)
    kit.wait_result(max_frames=pl.frame() + 3000, every=48)
    return finish(rec, pl)


def protoss_m31(out_name: str = "protoss-m31") -> dict:
    """protoss10 (Eye of the Storm): the victory trigger fires once slot 3 commands no Overmind (147); Tassadar's
    Gantrithor, Raynor and Zeratul must survive ("power overwhelming"). Both armies, the Terran base's and the
    Protoss one, walk to the Overmind and attack it."""
    s, pl, rec, kit = begin(out_name, "protoss10", "original")
    kit.strike(kit.players(ALL_PLAYERS), {T_OVERMIND})
    return finish(rec, pl)


T_ZEALOT, T_PYLON, T_GATEWAY, T_RAYNOR_VULTURE, T_SHUTTLE = 65, 156, 160, 19, 69
GATE_CAP = 48  # units: Gateways train no more beyond this army size


def gateways(kit: Kit, count: int, near: tuple[int, int], building: int = T_GATEWAY) -> None:
    """A Probe warps in a Pylon near `near` and, once it stands, `count` Gateways (or other buildings of the
    type) in its field."""
    pl = kit.pl
    at = kit.build(T_PYLON, near, r0=128, r1=512, worker=T_PROBE)
    if at is None:
        return
    pl.until(lambda: any(dist(u, *at) <= 64 for u in kit.complete([T_PYLON])), max_frames=2000)
    for _ in range(count):
        kit.build(building, at, r0=96, r1=288, step=32, worker=T_PROBE)
    kit.rec.event("gateways", buildings=sorted(u["type"] for u in pl.mine() if kit.is_building(u["type"])))


def gate_tick(kit: Kit, key: int = ord("Z"), cap: int = GATE_CAP, producer: int = T_GATEWAY,
              flyers: bool | None = None) -> Callable[[], None]:
    """A conquer tick: Gateways (or other producers of the type) train the unit of the hotkey (5 per empty queue)
    while the army (its flyers or ground units with `flyers`) is smaller than `cap`; "show me the money" again
    when the minerals run low."""
    def tick() -> None:
        kit.money()
        if len(kit.army(flyers)) < cap:
            kit.train([producer], key, limit=5)
    return tick


def protoss_m24(out_name: str = "protoss-m24") -> dict:
    """protoss03 (Higher Ground): the victory trigger fires once the foes of the current player (the Zerg of
    slots 0 and 6) command no building. Probes warp in a Pylon and three Gateways, which train Zealots, and the
    army conquers the Zerg buildings."""
    s, pl, rec, kit = begin(out_name, "protoss3", "original")
    nexus = pl.mine([T_NEXUS])[0]
    gateways(kit, 3, (nexus["x"] - 256, nexus["y"]))
    kit.conquer(kit.players(FOES), tick=gate_tick(kit))
    return finish(rec, pl)


COP = (3040, 3872, 3200, 4000)  # protoss04's "COP" beacon


def protoss_m25(out_name: str = "protoss-m25") -> dict:
    """protoss04 (The Hunt for Tassadar): Tassadar (79) and Raynor (19) of the rescuable slot 2 join the player
    when the army reaches them; the victory trigger then wants both at "COP" (3040,3872)-(3200,4000), the beacon
    by the start. The army walks to them, and they walk to the beacon ("power overwhelming" keeps them alive);
    Tassadar attacks the Zerg crowding him (Psi Assault) unless he holds his position there. "the gathering" is
    left out: it would feed the Zerg Queens' Spawn Broodlings, which kill a unit whatever its hit points."""
    s, pl, rec, kit = begin(out_name, "protoss4", "original",
                            cheats=tuple(c for c in PLAY_CHEATS if c != "the gathering"))
    rescue(kit, T_TASSADAR, max_frames=12000)
    pl.until(lambda: bool(pl.mine([T_RAYNOR_VULTURE])), max_frames=2000)
    kit.assemble({T_TASSADAR: 1, T_RAYNOR_VULTURE: 1}, COP, near=8192)
    return finish(rec, pl)


ENTRANCE = (3392, 3360, 3616, 3552)  # protoss05's "installation entrance"


def protoss_m26(out_name: str = "protoss-m26") -> dict:
    """protoss05 (Choosing Sides): the victory trigger wants Tassadar (79) and two Zealots (65) of the current
    player at "installation entrance" (3392,3360)-(3616,3552), inside the Zerg base, which no ground unit of the
    player reaches (the game's region groups). Tassadar and three Zealots board the Shuttle, which flies them there
    and unloads them, and they step into the location ("power overwhelming" keeps them alive). "the gathering" is
    left out here: the energy cheat feeds the Zerg Queens too, whose Spawn Broodlings kill a unit whatever its
    hit points."""
    s, pl, rec, kit = begin(out_name, "protoss5", "original",
                            cheats=tuple(c for c in PLAY_CHEATS if c != "the gathering"))
    shuttle = pl.mine([T_SHUTTLE])[0]
    zealots = sorted(pl.mine([T_ZEALOT]), key=lambda u: dist(u, shuttle["x"], shuttle["y"]))[:3]
    x, y = (ENTRANCE[0] + ENTRANCE[2]) // 2, (ENTRANCE[1] + ENTRANCE[3]) // 2
    kit.ferry(pl.mine([T_TASSADAR]) + zealots, T_SHUTTLE, x, y)
    kit.assemble({T_TASSADAR: 1, T_ZEALOT: 2}, ENTRANCE, near=800, max_frames=pl.frame() + 6000)
    return finish(rec, pl)


# protoss06: the way to Zeratul through the trigger locations that open the doors on it: "open sesame!" (door10),
# "door10", "door11", "Free me!" (door15, the last door before Zeratul) and Zeratul himself
ZERATUL_ROUTE = ((560, 2112), (416, 3312), (720, 3680), (3728, 3360), (3073, 3820))


def protoss_m27(out_name: str = "protoss-m27") -> dict:
    """protoss06 (Into the Darkness): the victory trigger fires once the current player commands Zeratul (75), who
    stands rescuable (slot 2) in the installation's south-east behind "door15". Tassadar and the two Zealots walk
    there through "open sesame!" (512,2080)-(608,2144), which opens "door10", the door locations, whose doors
    open as they come, and "Free me!" (3680,3328)-(3776,3392), which opens "door15" ("power overwhelming" keeps
    them alive)."""
    s, pl, rec, kit = begin(out_name, "protoss6", "original")
    kit.travel({T_TASSADAR, T_ZEALOT}, ZERATUL_ROUTE, leg_frames=8000)
    kit.wait_result(max_frames=pl.frame() + 2000, every=48)
    return finish(rec, pl)


# ==================================================================== Protoss (Episode IV, Brood War)
# "the gathering" left out: it feeds the computer's casters too (Queens' Spawn Broodling, Ghosts' Lockdown,
# Arbiters' Stasis Field)
NO_GATHERING = tuple(c for c in PLAY_CHEATS if c != "the gathering")
T_DARK_TEMPLAR, T_SCOUT, T_ARTANIS, T_URAJ, T_POWER_GENERATOR = 61, 70, 88, 128, 200
T_ROBOTICS, T_STARGATE = 155, 167
WARP_GATE = (3232, 2368, 3744, 2944)  # expprotoss01's "WarpGate"


def xprotoss_m32(out_name: str = "xprotoss-m32") -> dict:
    """expprotoss01 (Escape From Aiur): Zeratul (75) of the current player in "WarpGate"
    (3232,2368)-(3744,2944) sets switch 43; the Shuttles it brings land Raynor (19) at "Shuttle01Dest", which sets
    switch 45, and the victory trigger fires on it. Zeratul walks from the start through the Zerg land to the gate
    ("power overwhelming" keeps him alive), short of its "Beacon", which would remove him."""
    s, pl, rec, kit = begin(out_name, "xprotoss1", "expansion")
    kit.travel({T_ZERATUL}, [(3296, 2752)], leg_frames=12000)
    kit.wait_result(max_frames=pl.frame() + 8000, every=96)
    return finish(rec, pl)


def xprotoss_m33(out_name: str = "xprotoss-m33") -> dict:
    """expprotoss02 (The Dunes of Shakuras): the victory trigger fires once slot 4 (the Zerg) commands no
    building. The Probes warp in a Pylon and two Gateways (the dunes by the start leave room for no more), which
    train Zealots, and the army conquers the Zerg buildings."""
    s, pl, rec, kit = begin(out_name, "xprotoss2", "expansion", cheats=NO_GATHERING)
    probe = pl.mine([T_PROBE])[0]
    gateways(kit, 2, (probe["x"], probe["y"] + 160))
    kit.conquer([4], tick=gate_tick(kit))
    return finish(rec, pl)


# expprotoss03: each Cerebrate's "Cerebrate" and "AttackCerebrate" locations
CEREBRATES = ((3, (384, 2208, 480, 2272), (320, 2144, 544, 2336)), (4, (800, 2304, 896, 2368), (736, 2240, 960, 2432)))


def xprotoss_m34(out_name: str = "xprotoss-m34") -> dict:
    """expprotoss03 (Legacy of the Xel'Naga): a Cerebrate (151) killed with a Dark Templar (61) of slot 1 in its
    "AttackCerebrate" location sets a switch (one killed without comes back); with both switches Kerrigan comes,
    walks to "StartSpeech" and the victory trigger fires. The Zerg land has no ground way from the base (the game's
    region groups): a Probe warps in a Robotics Facility, which trains a Shuttle; it flies the two Dark Templars
    to the first Cerebrate, they attack it, then walk to the second one and attack it ("power overwhelming"
    keeps them alive), and leave the way Kerrigan comes."""
    s, pl, rec, kit = begin(out_name, "xprotoss3", "expansion", cheats=NO_GATHERING)
    nexus = pl.mine([T_NEXUS])[0]
    kit.build_and_wait(T_ROBOTICS, (nexus["x"] + 160, nexus["y"] + 192), r0=64, r1=320, step=32, worker=T_PROBE)
    kit.train([T_ROBOTICS], ord("S"))
    pl.until(lambda: bool(kit.complete([T_SHUTTLE])), max_frames=3000)
    templars = pl.mine([T_DARK_TEMPLAR])
    first = CEREBRATES[0][2]
    kit.ferry(templars, T_SHUTTLE, first[0] + 48, first[1] + 32)
    picked = {u["index"] for u in templars}
    for player, cerebrate, attack in CEREBRATES:
        kit.strike([player], {T_CEREBRATE}, army=lambda units: [u for u in units if u["index"] in picked],
                   done=lambda: not kit.targets([player], {T_CEREBRATE}, rects=[cerebrate]))
        rec.event("cerebrate_killed", player=player, templars=[unit_brief(u) for u in pl.mine([T_DARK_TEMPLAR])])
    kit.send(pl.mine([T_DARK_TEMPLAR]), 1600, 2600, attack=False)
    kit.wait_result(max_frames=pl.frame() + 8000, every=96)
    return finish(rec, pl)


def xprotoss_m35(out_name: str = "xprotoss-m35") -> dict:
    """expprotoss04 (The Quest for Uraj): two buildings of slot 5 lost bring Kerrigan (51) with Probes; the victory
    trigger wants Kerrigan of the current player and the Uraj crystal (128) in "KerriganCenter", a 96-pixel
    location that follows her. The army conquers slot 5's base until she comes, then she walks to the crystal in
    the Terran base ("power overwhelming" keeps her alive)."""
    s, pl, rec, kit = begin(out_name, "xprotoss4", "expansion", cheats=NO_GATHERING)
    kit.conquer([5], done=lambda: bool(pl.units(types=[T_KERRIGAN])))
    pl.until(lambda: bool(pl.mine([T_KERRIGAN])), max_frames=3000)
    rec.event("kerrigan", units=[unit_brief(u) for u in pl.units(types=[T_KERRIGAN])])
    uraj = pl.units(types=[T_URAJ])[0]
    march_unit(kit, pl.mine([T_KERRIGAN])[0], uraj["x"], uraj["y"], lambda: kit.rec.result is not None,
               max_frames=20000)
    return finish(rec, pl)


def xprotoss_m36(out_name: str = "xprotoss-m36") -> dict:
    """expprotoss05 (The Battle of Braxis): each Power Generator (200) of slot 7 destroyed sets a switch; the
    victory trigger wants all five. They stand on plateaus no ground unit of the player reaches (the game's region
    groups), so Artanis and the Scouts attack them one by one ("power overwhelming" keeps them alive)."""
    s, pl, rec, kit = begin(out_name, "xprotoss5", "expansion", cheats=NO_GATHERING)
    kit.strike([7], {T_POWER_GENERATOR}, flyers=True)
    return finish(rec, pl)


T_KHALIS = 129
CRYSTAL_LOC = (3680, 3424, 3776, 3488)  # expprotoss06's "CrystalLoc .g"


def xprotoss_m37(out_name: str = "xprotoss-m37") -> dict:
    """expprotoss06 (Return to Char): the victory trigger fires on switch 1, which a Probe (64) of the current
    player at "CrystalLoc .g" (3680,3424)-(3776,3488) sets. A Probe walks across the Zerg land ("power
    overwhelming" keeps it alive) and picks up the Khalis crystal (129) lying there."""
    s, pl, rec, kit = begin(out_name, "xprotoss6", "expansion")
    probe = min(pl.mine([T_PROBE]), key=lambda u: dist(u, 3728, 3456))
    kit.carry(probe, T_KHALIS, CRYSTAL_LOC, max_frames=20000)
    return finish(rec, pl)


STAGE_NORTH = (704, 0, 1888, 608)  # expprotoss07's "StageNorth"


def xprotoss_m38(out_name: str = "xprotoss-m38") -> dict:
    """expprotoss07 (The Insurgent): Aldaris (79, slot 7) killed in "StageNorth" (704,0)-(1888,608) starts the
    ending: the player's units go to slot 6, Kerrigan's Lurkers kill the Aldaris the trigger creates there, she
    leaves and the victory trigger fires. That Aldaris stands on a plateau no ground unit reaches (the game's region
    groups): Probes warp in a Pylon and three Stargates, which train Scouts, and they attack him ("power
    overwhelming" keeps them alive). Once the player's units are gone to slot 6, "power overwhelming" is typed
    again to turn it off: it takes the damage of every computer player's attack, the Lurkers' too."""
    s, pl, rec, kit = begin(out_name, "xprotoss7", "expansion", cheats=NO_GATHERING)
    nexus = pl.mine([T_NEXUS])[0]
    gateways(kit, 3, (nexus["x"] - 320, nexus["y"] - 192), building=T_STARGATE)
    kit.strike([7], {T_TASSADAR}, flyers=True, rects=[STAGE_NORTH],
               tick=gate_tick(kit, ord("S"), cap=24, producer=T_STARGATE, flyers=True),
               done=lambda: not pl.mine([T_NEXUS]))
    rec.event("units_handed_over", frame=pl.frame())
    kit.cheats(("power overwhelming",))
    kit.wait_result(max_frames=pl.frame() + 8000, every=96)
    return finish(rec, pl)


TEMPLE_ENTRANCE = (3776, 2368, 3872, 2432)  # expprotoss08's "Temple Entrance"


def xprotoss_m39(out_name: str = "xprotoss-m39") -> dict:
    """expprotoss08 (Countdown): Artanis (88) and Zeratul (75) each brought to "Temple Entrance"
    (3776,2368)-(3872,2432) start a 900-second countdown, the Temple (175) joins the player, and the victory
    trigger fires when the countdown ends with the Temple standing ("power overwhelming" keeps it, Artanis and
    Zeratul alive). Artanis' Scout flies there, Zeratul walks to the foot of the Temple, then to the entrance
    with a click in the view (a minimap click there leaves him beside the Temple)."""
    s, pl, rec, kit = begin(out_name, "xprotoss8", "expansion", cheats=NO_GATHERING)
    x, y = (TEMPLE_ENTRANCE[0] + TEMPLE_ENTRANCE[2]) // 2, TEMPLE_ENTRANCE[3] - 12
    artanis, zeratul = pl.mine([T_ARTANIS])[0], pl.mine([T_ZERATUL])[0]
    ok = march_unit(kit, artanis, x, y, lambda: not pl.mine([T_ARTANIS]), max_frames=8000)
    rec.event("hero_at_temple", hero=T_ARTANIS, ok=ok)
    march_unit(kit, zeratul, x - 224, y + 32, lambda: dist(pl.unit(zeratul["ptr"]), x - 224, y + 32) <= 96,
               max_frames=15000)
    ok = kit.approach(zeratul, x, y, lambda: not pl.mine([T_ZERATUL]), max_frames=4000)
    rec.event("hero_at_temple", hero=T_ZERATUL, ok=ok)
    kit.wait_result(max_frames=pl.frame() + 30000, every=480)
    return finish(rec, pl)


# ==================================================================== Terran (Episode V, Brood War)
T_COMMAND_CENTER, T_BARRACKS, T_STARPORT = 106, 111, 114


def air_strike(kit: Kit, players: list[int], types: Iterable[int], starports: int = 2, cap: int = 24,
               **kw: Any) -> None:
    """SCVs build `starports` more Starports by the first Command Center, which train Battlecruisers or Wraiths
    (`starport_tick`), and the flyers attack the targets one by one (`strike`)."""
    cc = kit.pl.mine([T_COMMAND_CENTER])[0]
    for _ in range(starports):
        kit.build_and_wait(T_STARPORT, (cc["x"], cc["y"]), r0=160, r1=640)
    kit.strike(players, types, tick=starport_tick(kit, cap), flyers=True, **kw)


FIRST_STRIKE_ROUTE = ((1916, 2620), (1724, 2044), (1188, 1556), (1100, 1132), (484, 980))  # expterran01
FIRST_STRIKE_ARMY = 24


def xterran_m40(out_name: str = "xterran-m40") -> dict:
    """expterran01 (First Strike): the victory trigger fires once slot 0 commands no Command Center (106). The
    mission withholds the Starport, so the Barracks trains Marines until the army has `FIRST_STRIKE_ARMY` units
    (the few starting ones cannot outpace the SCVs' repairs), and the army walks to slot 0's base by waypoints
    east around the plateau (sent straight there, it stops at the foot of a ramp below the base) and attacks the
    Command Center ("power overwhelming" keeps it alive)."""
    s, pl, rec, kit = begin(out_name, "xterran1", "expansion", cheats=NO_GATHERING)
    end = pl.frame() + 4000
    while len(kit.army()) < FIRST_STRIKE_ARMY and pl.frame() < end:
        kit.money()
        kit.train([T_BARRACKS], ord("M"), limit=5)
        pl.frames(96)
    rec.event("army", units=len(kit.army()))
    kit.travel({u["type"] for u in kit.army()}, FIRST_STRIKE_ROUTE, leg_frames=3000)
    kit.strike([0], {T_COMMAND_CENTER})
    return finish(rec, pl)


def emperors_fall(out_name: str, warp: str, inject: tuple[int, ...] | None = None) -> dict:
    """expterran05a/05b (Emperor's Fall): Mengsk's Command Center (106, slot 7) destroyed starts the ending:
    the units on the stage are removed, Raynor's Arbiters recall Mengsk and the victory trigger fires. The
    Command Center stands in the far corner of the map: SCVs build two Starports, which train Battlecruisers or
    Wraiths, and the flyers attack it."""
    s, pl, rec, kit = begin(out_name, warp, "expansion", inject=inject, cheats=NO_GATHERING)
    air_strike(kit, [7], {T_COMMAND_CENTER})
    return finish(rec, pl)


def xterran_m44(out_name: str = "xterran-m44") -> dict:
    """expterran05a (Emperor's Fall, Ground Zero): see `emperors_fall`."""
    return emperors_fall(out_name, "xterran5")


def xterran_m45(out_name: str = "xterran-m45") -> dict:
    """expterran05b (Emperor's Fall, Birds of War), the row expterran04 names when its Nuclear Silos fall: see
    `emperors_fall`. The warp cheat does not reach it; the mission injection starts it."""
    return emperors_fall(out_name, "xterran5", inject=(45, 1, 1))


def xterran_m46(out_name: str = "xterran-m46") -> dict:
    """expterran06 (Emperor's Flight): Raynor's Command Center (106, slot 7) destroyed brings his (invincible)
    Dropship, which flies to "Warp Gate"; there the victory trigger fires. The opening hands the player a base
    whose buildings arrive lifted: they land, SCVs build one more Starport, the Starports train Battlecruisers or
    Wraiths, and the flyers attack the Command Center beyond the Protoss base."""
    s, pl, rec, kit = begin(out_name, "xterran6", "expansion", cheats=NO_GATHERING)
    pl.until(lambda: bool(pl.mine([T_COMMAND_CENTER])), max_frames=6000)
    pl.frames(48)
    buildings = [u for u in pl.mine() if kit.is_building(u["type"])]
    for b in sorted(buildings, key=lambda u: u["type"] != T_COMMAND_CENTER):
        kit.land(b, (b["x"], b["y"]))
    air_strike(kit, [7], {T_COMMAND_CENTER}, starports=1)
    return finish(rec, pl)


# expterran07: ways through the installation, region centres of the game's region graph (the converter's RGNS) on
# the shortest ground path between the gated doors; a door on the way opens when a man stands in its location
TO_COMPUTER03 = ((464, 240), (560, 368), (848, 496), (1040, 432), (1232, 240), (1488, 240), (1680, 176), (1872, 240),
                 (2064, 336), (2288, 368), (2416, 528), (2640, 592), (2800, 720), (2992, 912), (2896, 1168),
                 (2640, 1168), (2484, 1284))
TO_COMPUTER04 = ((2640, 1168), (2480, 976), (2288, 944), (2096, 848), (1872, 752), (1840, 1008), (1520, 912),
                 (1392, 1072), (1232, 1168), (944, 1296), (784, 1520), (592, 1616), (592, 1936), (912, 1968),
                 (1040, 1776), (1328, 1776), (1456, 1872), (1584, 1968), (1776, 2128), (1648, 2288), (1488, 2352),
                 (1392, 2576), (1232, 2672), (1136, 2448), (880, 2448), (720, 2512), (624, 2640), (432, 2704),
                 (432, 2928), (688, 3088), (528, 3216), (624, 3408), (912, 3472))
TO_DOOR68 = ((1040, 3216), (1264, 3248), (1424, 3088), (1680, 3088), (1808, 2896), (2064, 2768), (2320, 2608),
             (2576, 2672), (2704, 2768), (2832, 2480), (2704, 2192), (2864, 2032), (3120, 2128), (3312, 2224),
             (3632, 2128), (3760, 1904), (3984, 1936), (4080, 1712), (3856, 1584), (3696, 1328), (3940, 1170))
TO_VICTORY = ((4080, 1040), (4208, 912), (4528, 656), (4720, 560), (4880, 464), (5104, 464), (5264, 240), (5424, 368),
              (5616, 432), (5840, 592), (5680, 752), (5648, 944), (5808, 1040), (5936, 1136), (5776, 1328),
              (5552, 1424), (5392, 1232), (5136, 1328), (4880, 1488), (4912, 1744), (5136, 1744), (5296, 1840),
              (5456, 1936), (5488, 2192), (5328, 2288), (5072, 2416), (5104, 2672), (5232, 2768), (5424, 2928),
              (5488, 3216), (5264, 3152), (5008, 3312), (4880, 3472), (4656, 3408), (4496, 3344), (4368, 3248),
              (4208, 3184), (4048, 3248), (3856, 3344), (3664, 3280), (3632, 2992), (3824, 2864), (3888, 3008))
DEAD_CIVILIAN_COMP03 = (2080, 1152, 2784, 1600)  # expterran07's "DeadCivilianComp03"
COMPUTER04 = (896, 3340)  # in expterran07's "Computer04" (864,3296)-(992,3392)
T_MARINE = 0


def xterran_m47(out_name: str = "xterran-m47") -> dict:
    """expterran07 (Patriot's Blood): the victory trigger wants a man of the player at "Victory"
    (3840,2976)-(3936,3040), behind four doors that triggers keep shut: "Door09" until the Civilians the
    Computer03 terminal calls are dead, "Door18" until Duran stands at Computer04, "Door14" until Stukov has died
    there, "Door68" until an Ultralisk, called by a Marine at the door, stands in it. "power overwhelming" is
    typed only once Duran has come: the opening needs the allies' Marines to kill slot 1's (it zeroes the
    damage of every player that is not human). Duran walks to the Marines of slot 6, which join the player; the
    Marines walk to Computer03 and kill the Civilians it calls (Duran stays behind: an attack click that lands on
    him turns them on him, and his own player's damage is not zeroed), then the Marines stop, Duran follows, and
    all walk through Door09 to Computer04, where Duran calls Stukov; once the countdown runs the Marines walk
    through Door18 and Door14 to Door68, wait for it, and walk on around the installation to the terminal. The
    ways are region-centre waypoints (`TO_COMPUTER03` ..)."""
    s, pl, rec, kit = begin(out_name, "xterran7", "expansion",
                            cheats=tuple(c for c in NO_GATHERING if c != "power overwhelming"))
    pl.until(lambda: bool(pl.mine([T_DURAN])), max_frames=6000)
    kit.cheats(("power overwhelming",))
    duran = pl.mine([T_DURAN])[0]
    allies = pl.units(owner=6, types=[T_MARINE])
    if allies:
        x, y = allies[0]["x"], allies[0]["y"]
        ok = march_unit(kit, duran, x, y, lambda: not pl.units(owner=6, types=[T_MARINE]), max_frames=1500)
        rec.event("marines_rescued", ok=ok, marines=len(pl.mine([T_MARINE])))
    kit.travel({T_MARINE}, TO_COMPUTER03, leg_frames=4000)
    rec.event("computer03", frame=pl.frame())
    pl.frames(48)
    civilians = [DEAD_CIVILIAN_COMP03]
    kit.strike([1], {T_CIVILIAN}, rects=civilians, max_frames=pl.frame() + 6000,
               army=lambda units: [u for u in units if u["type"] == T_MARINE],
               done=lambda: not kit.targets([1], {T_CIVILIAN}, rects=civilians))
    rec.event("civilians_dead", frame=pl.frame())
    kit.halt(kit.army())
    pl.frames(96)
    # Duran, kept away from the Marines' fight (an attack click that lands on him turns them on him), follows
    kit.travel({T_DURAN}, TO_COMPUTER03, leg_frames=4000)
    kit.travel({T_MARINE, T_DURAN}, TO_COMPUTER04, leg_frames=4000)
    ok = kit.approach(duran, *COMPUTER04, lambda: gone(kit, duran) or dist(pl.unit(duran["ptr"]), *COMPUTER04) <= 48,
                      max_frames=3000)
    rec.event("stukov", ok=ok)
    pl.until(lambda: not pl.mine([T_DURAN]), max_frames=3000)
    pl.frames(480)
    rec.event("countdown", frame=pl.frame())
    # the countdown (900 game seconds) leaves no time to lose: the next waypoint as soon as one Marine nears this one
    kit.travel({T_MARINE}, TO_DOOR68, leg_frames=4000, every=24, radius=192)
    kit.travel({T_MARINE}, TO_VICTORY, leg_frames=6000, every=24, radius=192)
    # the last leg ends near the terminal: the nearest Marine steps into "Victory" with a click in the view
    x, y = TO_VICTORY[-1]
    marine = min(pl.mine([T_MARINE]), key=lambda u: dist(u, x, y))
    kit.approach(marine, x, y, lambda: rec.result is not None, max_frames=3000)
    kit.wait_result(max_frames=pl.frame() + 2000, every=48)
    return finish(rec, pl)


T_DURAN, T_HIVE = 99, 133
PSI_DISRUPTER_BEACON = (1312, 224, 1408, 288)  # expterran03's "Psi Disrupter Beacon"


def xterran_m42(out_name: str = "xterran-m42") -> dict:
    """expterran03 (Ruins of Tarsonis): Duran (99) of Force 1 at "Psi Disrupter Beacon" (1312,224)-(1408,288)
    once no other player commands a Hive (133) brings the Ghosts and the victory trigger. Duran lands by the
    base of slot 1, which then joins the player, and he joins the player too (rescuable). SCVs build two
    Starports, which train Battlecruisers or Wraiths, and the flyers attack the four Hives of Force 2 (one stands
    where no ground unit reaches), then Duran walks across the Zerg land to the beacon ("power overwhelming"
    keeps him alive)."""
    s, pl, rec, kit = begin(out_name, "xterran3", "expansion", cheats=NO_GATHERING)
    pl.until(lambda: bool(pl.mine([T_DURAN])) and bool(kit.complete([T_COMMAND_CENTER])), max_frames=6000)
    rec.event("base", units=[unit_brief(u) for u in pl.mine([T_DURAN, T_COMMAND_CENTER])])
    zerg = kit.players(FORCE_1 + 1)
    air_strike(kit, zerg, {T_HIVE}, done=lambda: not kit.targets(zerg, {T_HIVE}))
    rec.event("hives_destroyed", frame=pl.frame())
    duran = pl.mine([T_DURAN])[0]
    x, y = (PSI_DISRUPTER_BEACON[0] + PSI_DISRUPTER_BEACON[2]) // 2, PSI_DISRUPTER_BEACON[3] - 16
    ok = kit.approach(duran, x, y, lambda: in_rect(pl.unit(duran["ptr"]), PSI_DISRUPTER_BEACON), max_frames=12000)
    rec.event("duran_at_beacon", ok=ok)
    kit.wait_result(max_frames=pl.frame() + 6000, every=96)
    return finish(rec, pl)


T_SILO = 108


def xterran_m43(out_name: str = "xterran-m43") -> dict:
    """expterran04 (Assault on Korhal): the opening trigger takes slot 1's Command Centers away and moves their
    six Nuclear Silos (108, neutral once without a Command Center) into "S1"-"S6" in slot 1's base; once no
    Silo stands in any of them the victory trigger fires and names expterran05b ("Terran05b.scx") as the next
    scenario (the four Physics Labs of slot 0 destroyed instead would name expterran05a). SCVs build two
    Starports, which train Battlecruisers or Wraiths, and the flyers attack the Silos one by one."""
    s, pl, rec, kit = begin(out_name, "xterran4", "expansion", cheats=NO_GATHERING)
    pl.frames(96)  # the opening trigger moves the Silos after 2 seconds
    air_strike(kit, [NEUTRAL], {T_SILO})
    return finish(rec, pl)


T_PHYSICS_LAB = 118


def xterran_m43b(out_name: str = "xterran-m43b") -> dict:
    """expterran04 (Assault on Korhal), the other way: the four Physics Labs (118) of slot 0 destroyed leave the
    default next scenario trigger 1 set, expterran05a ("Terran05a.scx"), while the six Nuclear Silos (`xterran_m43`)
    would name expterran05b. The Silos stand (the flyers strike only the Labs, and the Silos are neutral). SCVs
    build two Starports, which train Battlecruisers or Wraiths, and the flyers attack the Labs one by one."""
    s, pl, rec, kit = begin(out_name, "xterran4", "expansion", cheats=NO_GATHERING)
    pl.frames(96)  # the opening trigger moves the Silos after 2 seconds
    air_strike(kit, [0], {T_PHYSICS_LAB})
    rec.event("silos_left", silos=len(kit.targets([NEUTRAL], {T_SILO})))
    return finish_next(rec, pl)


T_CIVILIAN, T_BEACON, T_BATTLECRUISER, T_MEDIC = 15, 195, 12, 34


def gone(kit: Kit, u: dict) -> bool:
    """The unit died, or left the local player (a trigger removed it or gave it away)."""
    v = kit.pl.unit(u["ptr"])
    return v["type"] != u["type"] or v["owner"] != kit.pl.local or not v["hp"]


ESCORT_OFFSETS = ((0, 0), (-40, 0), (40, 0), (0, -24), (0, 24))  # points of a beacon tried in turn
ESCORT_TRY = 2500  # frames: a walker gets this long to reach one point
ESCORT_CLEAR = 480  # frames: the army gets this long to clear a beacon the walker did not reach


def escort(kit: Kit, walker_type: int, beacons: Callable[[], list[tuple[int, int]]], max_frames: int,
           done: Callable[[], bool]) -> None:
    """Until `done()`: a unit of the type walks onto the nearest beacon (an (x, y) of `beacons()`) with a click
    in the view (`approach`), until a trigger takes it or the beacon is gone; one walker at a time. A walker that
    does not get there in `ESCORT_TRY` frames tries points around the beacon, after the army attack-moved there
    (enemies may stand on it)."""
    pl = kit.pl
    with StopAfterResult(kit.rec):
        while kit.rec.result is None and pl.frame() < max_frames and not done():
            walkers, points = pl.mine([walker_type]), beacons()
            if not walkers or not points:
                pl.frames(96)
                continue
            w = walkers[0]
            x, y = min(points, key=lambda p: dist(w, *p))
            kit.rec.event("escort", walker=unit_brief(w), to=[x, y], walkers=len(walkers), beacons=len(points))
            for dx, dy in ESCORT_OFFSETS:
                ok = kit.approach(w, x + dx, y + dy, lambda: gone(kit, w) or (x, y) not in beacons(),
                                  max_frames=min(ESCORT_TRY, max(0, max_frames - pl.frame())))
                kit.rec.event("escorted", ok=ok, walker=unit_brief(pl.unit(w["ptr"])), to=[x + dx, y + dy])
                if ok:
                    break
                if (dx, dy) == (0, 0) and kit.army():
                    # enemies may stand on the beacon: the army attack-moves there
                    kit.send(kit.army(), x, y)
                    pl.frames(ESCORT_CLEAR)


def xterran_m41(out_name: str = "xterran-m41") -> dict:
    """expterran02 (The Dylarian Shipyards): four landings (A-D) each give the player Civilians (15), 3, 4, 5 and
    6, and Beacons (195) of the player at as many Battlecruisers; a Civilian of Force 1 on a Beacon is taken and
    its Battlecruiser goes to the player's ally (slot 2); every Beacon used ends the landing (the player's men go
    to the ally) and starts the next. After the last one the Battlecruisers join the player, a minute later slot
    0's strike force of Battlecruisers comes, and the victory trigger fires once Force 2 (slot 0) commands no
    Battlecruiser. Each Civilian in turn walks to the nearest Beacon ("power overwhelming" keeps it alive), then
    the Battlecruisers attack slot 0's."""
    s, pl, rec, kit = begin(out_name, "xterran2", "expansion", cheats=NO_GATHERING)
    escort(kit, T_CIVILIAN, lambda: [(b["x"], b["y"]) for b in pl.mine([T_BEACON])], 60000,
           lambda: bool(pl.mine([T_BATTLECRUISER])))
    rec.event("battlecruisers", units=[unit_brief(u) for u in pl.mine([T_BATTLECRUISER])])
    kit.strike([0], {T_BATTLECRUISER}, flyers=True, max_frames=pl.frame() + 20000)
    return finish(rec, pl)


# expterran08's "Medic 1".."Medic 4" around the Overmind, each holding a unit 194 of slot 7 that a Medic there hands
# to the player
T_DISRUPTOR, T_DROPSHIP = 194, 11
MEDIC_BEACONS = ((3840, 1888, 3936, 1952), (3840, 1664, 3936, 1728), (4128, 1664, 4224, 1728),
                 (4128, 1888, 4224, 1952))


def xterran_m48(out_name: str = "xterran-m48") -> dict:
    """expterran08 (To Chain the Beast): a Medic (34) of Force 1 in each of "Medic 1".."Medic 4" around the
    Overmind is taken and hands the unit 194 standing there to the player; with four of them the ending runs
    (the Cerebrates die, Kerrigan and Duran meet) and the victory trigger fires. Medics walking across the Zerg
    land die to the Queens' Spawn Broodling, which "power overwhelming" does not stop, so they fly: an SCV builds
    a Starport, which trains Dropships, the Barracks trains two more Medics ("modify the phase variance" lifts
    the Academy and the Control Tower), and each Medic in turn boards a Dropship, which unloads it in a free
    location (`ferry`)."""
    s, pl, rec, kit = begin(out_name, "xterran8", "expansion", cheats=NO_GATHERING)
    cc = pl.mine([T_COMMAND_CENTER])[0]
    kit.build_and_wait(T_STARPORT, (cc["x"], cc["y"]), r0=160, r1=640)
    kit.train([T_STARPORT], ord("D"), limit=len(MEDIC_BEACONS))
    kit.train([T_BARRACKS], ord("C"), limit=2)
    pl.until(lambda: len(kit.complete([T_MEDIC])) >= len(MEDIC_BEACONS)
             and len(kit.complete([T_DROPSHIP])) >= len(MEDIC_BEACONS), max_frames=4000)
    rec.event("medics", units=[unit_brief(u) for u in pl.mine([T_MEDIC, T_DROPSHIP])])

    def free() -> list[tuple[int, int]]:
        taken = pl.mine([T_DISRUPTOR])
        return [((l + r) // 2, (t + b) // 2) for l, t, r, b in MEDIC_BEACONS
                if not any(in_rect(u, (l, t, r, b)) for u in taken)]
    with StopAfterResult(rec):
        while rec.result is None and free() and pl.mine([T_MEDIC]) and pl.frame() < 30000:
            x, y = free()[0]
            medic = min(pl.mine([T_MEDIC]), key=lambda u: dist(u, cc["x"], cc["y"]))
            ok = kit.ferry([medic], T_DROPSHIP, x, y)
            pl.until(lambda: gone(kit, medic) or (x, y) not in free(), max_frames=480)
            rec.event("medic_delivered", ok=ok, to=[x, y], free=free())
    kit.wait_result(max_frames=pl.frame() + 8000, every=96)
    return finish(rec, pl)


PLAYS = {f.__name__: f for f in (terran_m01, terran_m03, terran_m04, terran_m05, terran_m06, terran_m07, terran_m08,
                                 terran_m09, terran_m10, terran_m11, zerg_m13, zerg_m14, zerg_m15, zerg_m16, zerg_m17,
                                 zerg_m18, zerg_m19, zerg_m20, zerg_m21, protoss_m23, protoss_m24, protoss_m25,
                                 protoss_m26, protoss_m27, protoss_m28, protoss_m29, protoss_m30, protoss_m31,
                                 xprotoss_m32, xprotoss_m33, xprotoss_m34, xprotoss_m35, xprotoss_m36, xprotoss_m37,
                                 xprotoss_m38, xprotoss_m39, xterran_m40, xterran_m41, xterran_m42, xterran_m43,
                                 xterran_m43b, xterran_m44, xterran_m45, xterran_m46, xterran_m47, xterran_m48)}
