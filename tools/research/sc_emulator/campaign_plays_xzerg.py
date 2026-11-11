"""The scripted wins of the Brood War Zerg campaign (Episode VI) with its hidden rows, on the toolkit of
campaign_plays.py (Kit, begin, finish and the helpers); `python -m sc_emulator play NAME OUT` finds them in PLAYS.

Rows (the catalog's mapdata indices): m49 zerg01 .. m51 zerg03, m52 zerg04a and the hidden zerg04b-f (m53-m57,
started with the mission injection), m58 zerg05 .. m62 zerg09, the hidden Bonus (m63) and m64 zerg10. zerg03 and
zerg09 choose the next scenario; their branch plays (`xzerg_m51b`..`f`, `xzerg_m62b`) win them the other ways.
Each play records the name SetNextScenario left (the string at `NEXT_SCENARIO`) in its result.

Helpers beyond campaign_plays.py: `XKit` (a Kit whose army can be narrowed to part of the map, whose targets can
leave some out and which selects a group with one box, `select`; `strike` with `near=FAR` keeps every unit on the
targets), `xbegin`, `allied`, `countdown`, `po_on` / `toggle_po`, `swarm_tick` (a
recording `brood_tick`), `walk` (`Kit.approach` that also orders a stuck walker again), `hive_route`,
`through_nydus`, `emitter_rescue`, `attack_unit`, `infest` and `take_cc` (a Queen's Infestation of a Command
Center the army has worn down).
"""
from __future__ import annotations

from collections.abc import Callable, Iterable

from . import game
from .campaign_plays import (FORCE_1, HATCHERIES, NO_GATHERING, NON_ALLIED_VICTORY, ORDER_MOVE, PICK_UP_AGAIN,
                             POWERUP, STATUS_EVERY, T_DRONE, T_LARVA, T_OVERLORD, T_ZERATUL, Kit, begin, brood, dist,
                             finish, finish_next)
from .play import Player
from .scenario import ALLIANCES
from .scenarios import IDLE_ORDERS, Recorder, StopAfterResult, in_rect, select_group, unit_brief
from .session import Session

CHEAT_FLAGS = 0x5432FC
CHEAT_POWER_OVERWHELMING = 0x4  # bit of the cheat flags
NO_MONEY = tuple(c for c in NO_GATHERING if c != "show me the money")
NO_GATHERING_PO = tuple(c for c in NO_GATHERING if c != "power overwhelming")


class XKit(Kit):
    """A Kit whose `army` takes only the units `only(u)` accepts (when set), e.g. those of one part of the map,
    and whose `targets` leave out the units in `spare` (pointers), e.g. buildings that must stand until a
    countdown has run down."""

    def __init__(self, s: Session, pl: Player, rec: Recorder) -> None:
        super().__init__(s, pl, rec)
        self.only = None
        self.spare: set[int] = set()

    def army(self, flyers: bool | None = None) -> list[dict]:
        units = super().army(flyers)
        return [u for u in units if self.only(u)] if self.only else units

    def targets(self, players: list[int], types: Iterable[int] | None = None, buildings_only: bool = False,
                rects: list[tuple[int, int, int, int]] | None = None) -> list[dict]:
        return [u for u in super().targets(players, types, buildings_only, rects) if u["ptr"] not in self.spare]

    def select(self, grp: list[dict]) -> list[dict]:
        """Selects a group with one selection box dragged around its first unit (the player's units in it, at
        most 12; flyers stack), else unit by unit (`select_group`, a click per unit, which lets the game run on
        for a few frames each)."""
        if self.select_box(grp[0], r=SELECT_BOX):
            return self.pl.selection()
        return select_group(self.pl, grp)


def xbegin(name: str, warp: str, inject: tuple[int, ...] | None = None,
           cheats: Iterable[str] = NO_GATHERING) -> tuple[Session, Player, Recorder, XKit]:
    """`begin` with the expansion CD and an XKit."""
    s, pl, rec, kit = begin(name, warp, "expansion", inject=inject, cheats=cheats)
    x = XKit(s, pl, rec)
    return s, pl, rec, x


def allied(kit: Kit, p: int) -> bool:
    return bool(kit.m.read(ALLIANCES + 12 * kit.pl.local + p, 1)[0])


def countdown(pl: Player) -> int:
    return pl.m.r32(game.COUNTDOWN)


def po_on(pl: Player) -> bool:
    return bool(pl.m.r32(CHEAT_FLAGS) & CHEAT_POWER_OVERWHELMING)


def toggle_po(kit: Kit) -> None:
    """Types "power overwhelming" again: it also zeroes the damage of every computer player, which stalls a
    scripted kill by the computer (a cutscene) until it is turned off."""
    kit.cheats(("power overwhelming",))
    kit.rec.event("power_overwhelming", on=po_on(kit.pl))


SWARM_CAP = 48  # flyers: the larvae morph no more Mutalisks beyond this
# pixels: `Kit.strike` with this `near` orders every unit not attacking a target (idle, or fighting the computers'
# units anywhere) to attack the nearest target itself (A and a click on it) instead of first flying close to it
FAR = 1 << 20
SELECT_BOX = 80  # pixels: half the side of the selection box `XKit.select` drags


def swarm_tick(kit: Kit, key: int = ord("M"), cap: int = SWARM_CAP,
               flyers: bool | None = True) -> Callable[[], None]:
    """A conquer tick like `brood_tick` (the hatcheries morph their larvae into the unit of the hotkey while the
    army is smaller than `cap`; "show me the money" again when the minerals run low) that records what it
    morphed, and the larvae and the money every `STATUS_EVERY` frames."""
    last = [-STATUS_EVERY]

    def tick() -> None:
        kit.money()
        if len(kit.army(flyers)) < cap:
            n = kit.morph(key)
            if n:
                kit.rec.event("morph", unit=chr(key), larvae=n)
        if kit.pl.frame() - last[0] >= STATUS_EVERY:
            last[0] = kit.pl.frame()
            kit.rec.event("swarm", army=len(kit.army(flyers)), larvae=len(kit.pl.mine([T_LARVA])),
                          hatcheries=len(kit.complete(HATCHERIES)), resources=list(kit.pl.resources()))
    return tick


STUCK = 480  # frames: a walker that came no nearer in this time is ordered again


def walk(kit: Kit, u: dict, x: int, y: int, done: Callable[[], bool], max_frames: int = 12000) -> bool:
    """Like `Kit.approach` (one unit, selected with a box, moves with a right click on the exact point in the
    view), but the order is given again also when the unit came no nearer for `STUCK` frames (blocked or
    turned to another order), and its place and order are recorded every `STATUS_EVERY` frames."""
    pl = kit.pl
    end = pl.frame() + max_frames
    best, since, last = 1 << 30, pl.frame(), -STATUS_EVERY
    with StopAfterResult(kit.rec):
        while kit.rec.result is None and pl.frame() < end:
            if done():
                return True
            v = pl.unit(u["ptr"])
            d = dist(v, x, y)
            if d < best - 32:
                best, since = d, pl.frame()
            if pl.frame() - last >= STATUS_EVERY:
                last = pl.frame()
                kit.rec.event("walk_status", unit=unit_brief(v), to=[x, y])
            if (v["order"] in IDLE_ORDERS or pl.frame() - since >= STUCK) and kit.select_box(v):
                pl.click_map(x, y, right=True)
                since = pl.frame()
                kit.rec.event("walk", at=[v["x"], v["y"]], order=v["order"], to=[x, y])
            pl.frames(48)
    return False


# ==================================================================== m49 zerg01 (Disruption)
# (Hive0N .g location centre) in the order the rescues come
HIVES = ((528, 352), (2592, 400), (2240, 1808), (1344, 2448), (320, 1840), (352, 3888), (2208, 3856))
T_HIVE = 133
HIVE07_AREA = (1280, 3040, 3072, 4096)
HIVE07_FAILSAFE = (1088, 3392, 1536, 3840)  # "Hive07Failsafe .g"
T_NYDUS = 134


def hive_mine(kit: Kit, x: int, y: int) -> bool:
    return any(dist(h, x, y) <= 96 for h in kit.pl.mine([T_HIVE]))


def hive_route(kit: Kit, x: int, y: int, flyers: bool | None = None, max_frames: int = 6000) -> bool:
    """The idle army (its flyers or ground units with `flyers`) attack-moves onto (x, y) until the Hive there
    is the player's."""
    pl = kit.pl
    end = pl.frame() + max_frames
    last = pl.frame()
    with StopAfterResult(kit.rec):
        while kit.rec.result is None and pl.frame() < end:
            if pl.frame() - last >= STATUS_EVERY:
                last = pl.frame()
                kit.rec.event("status", army=[[u["type"], u["x"], u["y"], u["order"]] for u in kit.army(flyers)])
            if hive_mine(kit, x, y):
                kit.rec.event("hive_rescued", at=[x, y], frame=pl.frame())
                return True
            idle = [u for u in kit.army(flyers) if u["order"] in IDLE_ORDERS and dist(u, x, y) > 48]
            if idle:
                kit.send(idle, x, y)
                kit.rec.event("hive_route", to=[x, y], sent=len(idle))
            pl.frames(96)
    kit.rec.event("hive_not_rescued", at=[x, y])
    return False


def through_nydus(kit: Kit, rects: list[tuple[int, int, int, int]], want: int, max_frames: int = 6000) -> int:
    """The ground army outside the rectangles goes through the player's Nydus Canals (each group selected, a
    right click on the canal nearest to it) until `want` of its units stand inside one of them; returns how
    many do."""
    pl = kit.pl
    end = pl.frame() + max_frames
    inside = []
    with StopAfterResult(kit.rec):
        while kit.rec.result is None and pl.frame() < end:
            army = kit.army(flyers=False)
            inside = [u for u in army if any(in_rect(u, r) for r in rects)]
            if len(inside) >= want:
                break
            canals = kit.complete([T_NYDUS])
            outside = [u for u in army if u not in inside and u["order"] in IDLE_ORDERS]
            for grp in kit.groups(outside) if canals else ():
                c = min(canals, key=lambda c: dist(c, grp[0]["x"], grp[0]["y"]))
                if select_group(pl, grp):
                    pl.click_map(c["x"], c["y"], right=True)
                    kit.rec.event("nydus", group=len(grp), canal=unit_brief(c))
            pl.frames(96)
    kit.rec.event("through_nydus", inside=len(inside), frame=pl.frame())
    return len(inside)


def xzerg_m49(out_name: str = "xzerg-m49") -> dict:
    """expzerg01 (Disruption), next scenario none set (zerg02 follows): the seven Hives of slot 1 join the player
    when a man of the player comes to each one's "Hive0N .g" location (ground units only), each adding 1 to the
    custom score; a man in "Hive07Area" with the score at 7 brings the last assault (switch 50: Hydralisks of the
    player appear there), and the victory trigger fires once Force 2 (slots 4, 5) has no man left in
    "Hive07Area" (1280,3040)-(3072,4096). The Hunter Killers and Zerglings walk to the first six Hives ("power
    overwhelming" keeps them and the Hives alive). The seventh Hive stands where no ground unit of the start
    reaches (the game's region groups), and the mission lets the larvae morph nothing; the sixth Hive's rescue
    hands over its Nydus Canals, whose exits lie by the seventh. The army goes through them (the exits in
    "Hive07Failsafe .g" lift the invincibility of "Hive07Area"), attack-moves to the seventh Hive, and the units
    of the player in its area kill Force 2's men there."""
    s, pl, rec, kit = xbegin(out_name, "xzerg1")
    for x, y in HIVES[:6]:
        hive_route(kit, x, y, flyers=False, max_frames=8000)
    region = [HIVE07_FAILSAFE, HIVE07_AREA]
    through_nydus(kit, region, want=len(kit.army(flyers=False)) * 3 // 4)
    kit.only = lambda u: any(in_rect(u, r) for r in region)
    hive_route(kit, *HIVES[6], flyers=False, max_frames=8000)
    rec.event("hive07", frame=pl.frame(), units=len(pl.mine()))
    kit.conquer(kit.players(FORCE_1 + 1), buildings_only=False, rects=[HIVE07_AREA], max_frames=pl.frame() + 30000)
    return finish_next(rec, pl)


# ==================================================================== m50 zerg02 (Reign of Fire)
T_PSI_DISRUPTER, T_PSI_EMITTER, T_SCV = 190, 217, 7
RESCUABLE_ZERG = 5


def emitter_rescue(kit: Kit, scv: dict, max_frames: int = 20000) -> bool:
    """The SCV picks up the Psi Emitter (a right click on it) and carries it to the rescuable Zerg of slot 5,
    the nearest group first (the SCV alone selected with a click, a right click on the minimap), until slot 5
    has no man left (the "Emitter" location follows the emitter, and slot 5's men in it join the player)."""
    pl = kit.pl
    end = pl.frame() + max_frames
    last = -PICK_UP_AGAIN
    with StopAfterResult(kit.rec):
        while kit.rec.result is None and pl.frame() < end:
            u = pl.unit(scv["ptr"])
            if u["type"] != scv["type"] or u["owner"] != pl.local:
                kit.rec.event("scv_lost", unit=unit_brief(u))
                return False
            zerg = [z for z in pl.units(owner=RESCUABLE_ZERG) if not kit.is_building(z["type"])]
            if not zerg:
                return True
            if not pl.m.r32(u["ptr"] + POWERUP):
                items = pl.units(types=[T_PSI_EMITTER])
                if items and (u["order"] in IDLE_ORDERS or pl.frame() - last >= PICK_UP_AGAIN):
                    last = pl.frame()
                    pl.select_unit(u)
                    pl.click_map(items[0]["x"], items[0]["y"], right=True)
                    kit.rec.event("pick_up", at=[u["x"], u["y"]], item=[items[0]["x"], items[0]["y"]])
            elif u["order"] in IDLE_ORDERS:
                z = min(zerg, key=lambda z: dist(z, u["x"], u["y"]))
                # `Kit.send`: a selection box would take the Zerg that joined around the SCV along
                Kit.send(kit, [u], z["x"], z["y"], attack=False)
                kit.rec.event("emitter_to", at=[u["x"], u["y"]], zerg=unit_brief(z), left=len(zerg))
            pl.frames(48)
    return False


def xzerg_m50(out_name: str = "xzerg-m50") -> dict:
    """expzerg02 (Reign of Fire), next scenario none set: the victory trigger fires once Force 2 (slot 7)
    commands no Psi Disrupter (190). After the opening an SCV (7) of the player lands by the Psi Emitter (217);
    the rescuable Zerg of slot 5 that come into the "Emitter" location, which follows the emitter, join the
    player, and once slot 5 has no man left a Shuttle takes the SCV and the emitter away. The SCV carries the
    emitter to slot 5's groups; a Drone builds a Hatchery, then a Spire, the larvae morph Mutalisks, and they
    attack the Disrupter in the Terran base ("power overwhelming" keeps them alive)."""
    s, pl, rec, kit = xbegin(out_name, "xzerg2", cheats=NO_GATHERING_PO)
    pl.until(lambda: bool(pl.mine([T_SCV])), max_frames=20000)
    rec.event("scv", units=[unit_brief(u) for u in pl.mine()])
    toggle_po(kit)
    ok = emitter_rescue(kit, pl.mine([T_SCV])[0])
    rec.event("zerg_rescued", ok=ok, units=sorted(u["type"] for u in pl.mine()))
    pl.until(lambda: not pl.mine([T_SCV]), max_frames=6000)
    d = pl.mine([T_DRONE])[0]
    kit.build_and_wait(HATCHERIES[0], (d["x"], d["y"]), max_frames=4000, r0=64, r1=640, worker=T_DRONE)
    rec.event("base", units=[unit_brief(u) for u in pl.mine() if kit.is_building(u["type"])])
    brood(kit, hatcheries=1)
    kit.strike(kit.players(FORCE_1 + 1), {T_PSI_DISRUPTER}, tick=swarm_tick(kit), flyers=True,
               max_frames=60000)
    return finish_next(rec, pl)


# ==================================================================== m51 zerg03 (The Kel-Morian Combine)
T_QUEEN, T_CC, T_INFESTED_CC = 45, 106, 130
# the Command Centers of the Kel-Morian towns, nearest to the start first: Brown (slot 5), Orange (4), White
# (6), Red (0), Blue (1)
KEL_MORIAN_CCS = ((5, 800, 2320), (4, 1824, 1104), (6, 448, 560), (0, 3584, 3696), (1, 3424, 304))
QUOTA = 10000  # minerals: the victory triggers' "accumulate"
INFEST_HP = 640  # hit points: a Command Center below half of its 1500 is infested; the army stops below this


ATTACK_AGAIN = 48  # frames between the attack orders on a Command Center
QUEEN_WAIT = 160  # pixels: the Queen waits this far from the Command Center (towards the rally point)


def attack_unit(kit: Kit, grp: list[dict], t: dict) -> bool:
    """The group attacks the unit itself (A and a click on it); True when the first one aims at it."""
    pl = kit.pl
    if not select_group(pl, grp):
        return False
    t = pl.unit(t["ptr"])
    pl.center(t["x"], t["y"])  # before A: a minimap click after it would attack-move
    for dx, dy in ((0, 0), (-24, 16), (24, 16), (0, -24)):
        pl.hotkey(ord("A"))
        pl.click_map(t["x"] + dx, t["y"] + dy)
        if pl.unit(grp[0]["ptr"])["target_unit"] == t["ptr"]:
            return True
    return False


def cc_at(kit: Kit, x: int, y: int) -> dict | None:
    """The Command Center (or the Infested one) standing at (x, y), or None."""
    near = [u for u in kit.pl.units(types=[T_CC, T_INFESTED_CC]) if dist(u, x, y) <= 64]
    return near[0] if near else None


def infest(kit: Kit, queen: dict, cc: dict) -> None:
    """The Queen infests the Command Center (below half its hit points): selected with a box, a right click on
    it."""
    pl = kit.pl
    q = pl.unit(queen["ptr"])
    if not kit.select_box(q):
        kit.rec.event("queen_not_selected", queen=unit_brief(q))
        return
    c = pl.unit(cc["ptr"])
    pl.click_map(c["x"], c["y"], right=True)
    kit.rec.event("infest", queen=unit_brief(pl.unit(queen["ptr"])), cc=unit_brief(c))


def take_cc(kit: Kit, x: int, y: int, rally: tuple[int, int], max_frames: int = 12000) -> bool:
    """The army walks to the town's Command Center and attacks it until it is below `INFEST_HP`, then moves
    off to `rally`, and the Queen (waiting by the Command Center) infests it; again while it is not infested
    (the town's SCVs repair it)."""
    pl = kit.pl
    end = pl.frame() + max_frames
    # a Queen infesting a Command Center is inside it for a while
    pl.until(lambda: bool(pl.mine([T_QUEEN])), max_frames=1200)
    queen = pl.mine([T_QUEEN])[0]
    last_order = -ATTACK_AGAIN
    with StopAfterResult(kit.rec):
        while kit.rec.result is None and pl.frame() < end:
            cc = cc_at(kit, x, y)
            if cc is None:
                kit.rec.event("cc_gone", at=[x, y])
                return False
            if cc["type"] == T_INFESTED_CC:
                kit.rec.event("cc_infested", cc=unit_brief(cc), frame=pl.frame())
                return True
            army = kit.army()
            if cc["hp"] >= INFEST_HP:
                q = pl.unit(queen["ptr"])
                if dist(q, x, y) > 2 * QUEEN_WAIT and q["order"] in IDLE_ORDERS:
                    k = max(dist({"x": rally[0], "y": rally[1]}, x, y), 1)
                    kit.send([q], x + (rally[0] - x) * QUEEN_WAIT // k, y + (rally[1] - y) * QUEEN_WAIT // k,
                             attack=False)
                far = [u for u in army if dist(u, x, y) > 480 and u["order"] in IDLE_ORDERS]
                if far:
                    kit.send(far, x, y)  # attack-move into the town
                near = [u for u in army if dist(u, x, y) <= 480 and u["target_unit"] != cc["ptr"]]
                if near and pl.frame() - last_order >= ATTACK_AGAIN:
                    last_order = pl.frame()
                    for grp in kit.groups(near):
                        attack_unit(kit, grp, cc)
                    kit.rec.event("attack_cc", units=len(near), cc=unit_brief(pl.unit(cc["ptr"])))
                pl.frames(4)
                continue
            # below the mark: the army leaves the Command Center, the Queen infests it
            kit.send([u for u in army if dist(u, x, y) <= 900], *rally, attack=False)
            kit.rec.event("cc_weakened", cc=unit_brief(cc))
            infest(kit, queen, cc)
            pl.until(lambda: pl.unit(cc["ptr"])["type"] == T_INFESTED_CC or pl.unit(queen["ptr"])["order"]
                     in IDLE_ORDERS, max_frames=900)
    return False


def zerg03_branch(out_name: str, count: int) -> dict:
    """expzerg03 (The Kel-Morian Combine): the victory trigger wants 10,000 minerals of the current player
    ("accumulate" counts the minerals held) and names the next scenario after the number of Infested Command
    Centers (130) the player commands then: exactly 0 -> Zerg04a, 1 -> 04b, .. 5 -> 04f (6 or more: no victory).
    The player starts with 750 minerals and no Infested Command Center, and "show me the money" adds 10,000; so
    it is typed only once `count` Command Centers are infested. The army attacks the Kel-Morian towns' Command
    Centers one by one, nearest first, until each is below half its hit points, and the Queen infests it
    ("power overwhelming" keeps the army, the Queen and Fenix alive). With all five infested, Force 2 commands
    no Command Center and a trigger sets the minerals to 10,500 itself."""
    s, pl, rec, kit = xbegin(out_name, "xzerg3", cheats=NO_MONEY if count else NO_GATHERING)
    if count:
        start = pl.mine([T_QUEEN])[0]
        rally = (start["x"], start["y"])
        for owner, x, y in KEL_MORIAN_CCS[:count]:
            ok = take_cc(kit, x, y, rally)
            rec.event("town", owner=owner, ok=ok, infested=len(pl.mine([T_INFESTED_CC])))
            rally = (x, y)
        rec.event("infested", count=len(pl.mine([T_INFESTED_CC])), resources=list(pl.resources()))
        if kit.rec.result is None and pl.resources()[0] < QUOTA:
            kit.cheats(("show me the money",))
    kit.wait_result(max_frames=pl.frame() + 2000, every=48)
    return finish_next(rec, pl, {"infested": len(pl.mine([T_INFESTED_CC])) if rec.result is None else None})


def xzerg_m51(out_name: str = "xzerg-m51") -> dict:
    """expzerg03 -> Zerg04a: "show me the money" at once, with no Infested Command Center (`zerg03_branch`)."""
    return zerg03_branch(out_name, 0)


def xzerg_m51b(out_name: str = "xzerg-m51b") -> dict:
    """expzerg03 -> Zerg04b: one Infested Command Center (Brown's), then "show me the money" (`zerg03_branch`)."""
    return zerg03_branch(out_name, 1)


def xzerg_m51c(out_name: str = "xzerg-m51c") -> dict:
    """expzerg03 -> Zerg04c: two Infested Command Centers (Brown's, Orange's), then "show me the money"
    (`zerg03_branch`)."""
    return zerg03_branch(out_name, 2)


def xzerg_m51d(out_name: str = "xzerg-m51d") -> dict:
    """expzerg03 -> Zerg04d: three Infested Command Centers (Brown's, Orange's, White's), then "show me the money"
    (`zerg03_branch`)."""
    return zerg03_branch(out_name, 3)


def xzerg_m51e(out_name: str = "xzerg-m51e") -> dict:
    """expzerg03 -> Zerg04e: four Infested Command Centers (and Red's), then "show me the money"
    (`zerg03_branch`)."""
    return zerg03_branch(out_name, 4)


def xzerg_m51f(out_name: str = "xzerg-m51f") -> dict:
    """expzerg03 -> Zerg04f: all five Command Centers infested; Force 2 then commands none, and a trigger sets
    the minerals to 10,500 (`zerg03_branch`)."""
    return zerg03_branch(out_name, 5)


# ==================================================================== m52-m57 zerg04a-f (The Liberation of Korhal)
def liberation(out_name: str, inject: tuple[int, ...] | None = None) -> dict:
    """expzerg04a-f (The Liberation of Korhal), next scenario Zerg05: the six rows differ only in their retry
    scenario and their start (04b-f bring Infested Command Centers). The victory trigger fires once the non-allied
    players (slots 1, 4, 6) command no building. A Drone builds a Spire ("modify the phase variance" lifts the
    Lair), the larvae morph Mutalisks, and they conquer the buildings ("power overwhelming" keeps them alive)."""
    s, pl, rec, kit = xbegin(out_name, "xzerg4", inject=inject)
    brood(kit, hatcheries=0)
    kit.conquer(kit.players(NON_ALLIED_VICTORY), tick=swarm_tick(kit), flyers=True,
                max_frames=80000)
    return finish_next(rec, pl)


def xzerg_m52(out_name: str = "xzerg-m52") -> dict:
    """expzerg04a, next scenario Zerg05: see `liberation`."""
    return liberation(out_name)


def xzerg_m53(out_name: str = "xzerg-m53") -> dict:
    """expzerg04b (hidden; the mission injection starts it), next scenario Zerg05: see `liberation`."""
    return liberation(out_name, inject=(53, 0, 1))


def xzerg_m54(out_name: str = "xzerg-m54") -> dict:
    """expzerg04c (hidden), next scenario Zerg05: see `liberation`."""
    return liberation(out_name, inject=(54, 0, 1))


def xzerg_m55(out_name: str = "xzerg-m55") -> dict:
    """expzerg04d (hidden), next scenario Zerg05: see `liberation`."""
    return liberation(out_name, inject=(55, 0, 1))


def xzerg_m56(out_name: str = "xzerg-m56") -> dict:
    """expzerg04e (hidden), next scenario Zerg05: see `liberation`."""
    return liberation(out_name, inject=(56, 0, 1))


def xzerg_m57(out_name: str = "xzerg-m57") -> dict:
    """expzerg04f (hidden), next scenario Zerg05: see `liberation`."""
    return liberation(out_name, inject=(57, 0, 1))


# ==================================================================== m58 zerg05 (True Colors)
T_NORAD, T_FENIX = 29, 78


def xzerg_m58(out_name: str = "xzerg-m58") -> dict:
    """expzerg05 (True Colors), next scenario none set: the opening's Lurkers of slot 4 kill the allied men
    in "The Killing Ground", then slots 4 and 5 hand their units to the player (switch 50); after a 360-second
    countdown slots 0 and 1 turn on the player and Duke's Norad II (29) and Fenix (78) of slot 2 appear. The
    victory trigger wants the non-allied players without a building and slot 2 without Duke and Fenix.
    "power overwhelming" is typed only once the units are handed over (it zeroes the Lurkers' damage too). A
    Drone builds a Spire, the larvae morph Mutalisks, and once slots 0 and 1 are foes the army attacks their
    buildings one by one (`strike`: the computers' units are many), then Duke and Fenix."""
    s, pl, rec, kit = xbegin(out_name, "xzerg5", cheats=NO_GATHERING_PO)
    pl.until(lambda: bool(kit.complete(HATCHERIES)), max_frames=8000)
    toggle_po(kit)
    rec.event("base", units=[unit_brief(u) for u in pl.mine() if kit.is_building(u["type"])])
    brood(kit, hatcheries=1)
    morph = swarm_tick(kit)
    with StopAfterResult(rec):
        while allied(kit, 0) and pl.frame() < 20000:
            morph()
            pl.frames(240)
    rec.event("foes", frame=pl.frame())
    kit.strike(kit.players(NON_ALLIED_VICTORY), buildings_only=True, near=FAR, tick=morph, flyers=True,
               max_frames=80000, done=lambda: not kit.targets(kit.players(NON_ALLIED_VICTORY), buildings_only=True))
    kit.strike([2], {T_NORAD, T_FENIX}, tick=morph, flyers=True, max_frames=pl.frame() + 20000)
    return finish_next(rec, pl)


# ==================================================================== m59 zerg06 (Fury of the Swarm)
T_CIVILIAN = 15


def xzerg_m59(out_name: str = "xzerg-m59") -> dict:
    """expzerg06 (Fury of the Swarm), next scenario none set: the victory trigger fires once slot 6 commands no
    Civilian (15, the UED scientists in its base). The larvae morph Mutalisks (the base has a Spire) and they hunt
    the Civilians ("power overwhelming" keeps them alive)."""
    s, pl, rec, kit = xbegin(out_name, "xzerg6")
    kit.conquer([6], {T_CIVILIAN}, buildings_only=False, tick=swarm_tick(kit),
                flyers=True, max_frames=60000)
    return finish_next(rec, pl)


# ==================================================================== m60 zerg07 (Drawing of the Web)
T_DURAN_INFESTED = 104
# the "Beacon 1".."Beacon 5" locations, in the order Duran walks them
BEACONS = ((3424, 1408, 3520, 1472), (2944, 640, 3040, 704), (1184, 416, 1280, 480), (640, 1248, 736, 1312),
           (2016, 2464, 2112, 2528))


def xzerg_m60(out_name: str = "xzerg-m60") -> dict:
    """expzerg07 (Drawing of the Web), next scenario none set: Duran (104) of Force 1 in each of "Beacon 1".."5"
    sets a switch; with all five a 5-second countdown runs and the victory trigger fires. Duran walks from beacon to
    beacon across the Protoss land with clicks in the view ("power overwhelming" keeps him alive)."""
    s, pl, rec, kit = xbegin(out_name, "xzerg7")
    duran = pl.mine([T_DURAN_INFESTED])[0]
    for rect in BEACONS:
        x, y = (rect[0] + rect[2]) // 2, (rect[1] + rect[3]) // 2
        ok = walk(kit, duran, x, y, lambda: in_rect(pl.unit(duran["ptr"]), rect), max_frames=12000)
        rec.event("beacon", at=[x, y], ok=ok, frame=pl.frame())
    kit.wait_result(max_frames=pl.frame() + 3000, every=48)
    return finish_next(rec, pl)


# ==================================================================== m61 zerg08 (To Slay the Beast)
T_DARK_TEMPLAR, T_OVERMIND = 61, 148
OMIND_AREA = (5568, 448, 5856, 672)  # "OMindArea .g"


def xzerg_m61(out_name: str = "xzerg-m61") -> dict:
    """expzerg08 (To Slay the Beast), next scenario none set: the Overmind (148, slot 4) killed with a Dark
    Templar (61) of the player in "OMindArea .g" (5568,448)-(5856,672) starts the ending (killed without, it comes
    back): the player's units go to slot 5, Zeratul (slot 2) kills the remains, Kerrigan meets him and the victory
    trigger fires once Samir Duran (74, slot 0) is gone. The Overmind's plateau has no ground way from the base
    (the game's region groups): the Lair researches Ventral Sacs, an Overlord flies the two Dark Templars there
    and they attack the Overmind ("power overwhelming" keeps them alive). Once the units are handed over,
    "power overwhelming" is typed again to turn it off: it zeroes the damage of Zeratul's attack too."""
    s, pl, rec, kit = xbegin(out_name, "xzerg8")
    kit.ventral_sacs()
    dts = pl.mine([T_DARK_TEMPLAR])
    kit.ferry(dts, T_OVERLORD, 5600, 640, max_frames=20000)
    picked = {u["index"] for u in dts}
    kit.strike([4], {T_OVERMIND}, army=lambda units: [u for u in units if u["index"] in picked],
               done=lambda: not pl.mine([T_DARK_TEMPLAR]) or not kit.targets([4], {T_OVERMIND}),
               max_frames=pl.frame() + 12000)
    pl.until(lambda: not pl.mine([T_DARK_TEMPLAR]), max_frames=2000)
    rec.event("units_handed_over", frame=pl.frame(), units=len(pl.mine()))
    toggle_po(kit)
    kit.wait_result(max_frames=pl.frame() + 8000, every=96)
    return finish_next(rec, pl)


# ==================================================================== m62 zerg09 (The Reckoning)
BONUS_MARK = 299  # countdown seconds: switch 8 (the way to Zerg10) is set once the countdown is at most this
KEEP = 4  # buildings of slot 1 (the farthest from the player's base) spared until the countdown passes the mark


def reckoning(out_name: str, bonus: bool) -> dict:
    """expzerg09 (The Reckoning): the 1800-second countdown ends in a defeat unless slot 1 (the Protoss) commands
    no building first; then the player's units go to slot 5, Kerrigan's Ultralisks kill the Matriarch's Stasis
    Cell, and the victory trigger fires. Switch 8, set once the countdown is at most 299 seconds, decides the next
    scenario: Zerg10 with it, Bonus without (a win within about 25 minutes). The larvae morph Mutalisks (the
    base has a Greater Spire, two Drones add Hatcheries) and they conquer slot 1's buildings ("power overwhelming"
    keeps them alive); for Zerg10 they spare the `KEEP` buildings farthest from the base, wait at home until the
    countdown passes the mark, then destroy them. Once the units are handed over, "power overwhelming" is typed again to turn it off:
    it zeroes the damage of the Ultralisks too."""
    s, pl, rec, kit = xbegin(out_name, "xzerg9")
    rec.event("countdown", value=countdown(pl), frame=pl.frame())
    brood(kit, hatcheries=2, spire=False)
    morph = swarm_tick(kit)
    h = kit.complete(HATCHERIES)[0]
    home = (h["x"], h["y"])
    if not bonus:
        mark = BONUS_MARK - 5
        blds = kit.targets([1], buildings_only=True)
        kit.spare = {b["ptr"] for b in sorted(blds, key=lambda b: -dist(b, *home))[:KEEP]}
        rec.event("spared", buildings=[unit_brief(b) for b in blds if b["ptr"] in kit.spare])
        # each group attacks its target itself (an attack-move would take the spared buildings on the way)
        kit.strike([1], buildings_only=True, near=FAR, tick=morph, flyers=True, max_frames=80000,
                   done=lambda: not kit.targets([1], buildings_only=True) or countdown(pl) <= mark)
        hx, hy = home
        with StopAfterResult(rec):
            while rec.result is None and countdown(pl) > mark:
                morph()
                away = [u for u in kit.army() if dist(u, hx, hy) > 640 and u["order"] != ORDER_MOVE]
                if away:
                    kit.send(away, hx, hy, attack=False)
                pl.frames(96)
        kit.spare = set()
        rec.event("mark_passed", countdown=countdown(pl), frame=pl.frame())
    kit.conquer([1], tick=morph, flyers=True, max_frames=80000, done=lambda: not kit.targets([1], buildings_only=True))
    rec.event("protoss_base_destroyed", countdown=countdown(pl), frame=pl.frame())
    pl.until(lambda: not kit.army(), max_frames=2000)
    rec.event("units_handed_over", frame=pl.frame(), units=len(pl.mine()))
    toggle_po(kit)
    kit.wait_result(max_frames=pl.frame() + 8000, every=96)
    return finish_next(rec, pl)


def xzerg_m62(out_name: str = "xzerg-m62") -> dict:
    """expzerg09, next scenario Zerg10: the base falls after the countdown passed 299 seconds (`reckoning`)."""
    return reckoning(out_name, bonus=False)


def xzerg_m62b(out_name: str = "xzerg-m62b") -> dict:
    """expzerg09, next scenario Bonus: the base falls before the countdown reaches 299 seconds (`reckoning`)."""
    return reckoning(out_name, bonus=True)


# ==================================================================== m63 Bonus (The Dark Origin)
COP04 = (2592, 2816, 2688, 2880)


def xzerg_m63(out_name: str = "xzerg-m63") -> dict:
    """Bonus (The Dark Origin; hidden, the mission injection starts it), next scenario Zerg10: the victory trigger
    wants Zeratul (75) of the current player at "COP04" (2592,2816)-(2688,2880) in the Terran base. Zeratul walks
    there ("power overwhelming" keeps him alive)."""
    s, pl, rec, kit = xbegin(out_name, "xzerg1", inject=(63, 0, 1))
    pl.until(lambda: bool(pl.mine([T_ZERATUL])), max_frames=6000)
    zeratul = pl.mine([T_ZERATUL])[0]
    x, y = (COP04[0] + COP04[2]) // 2, (COP04[1] + COP04[3]) // 2
    walk(kit, zeratul, x, y, lambda: in_rect(pl.unit(zeratul["ptr"]), COP04), max_frames=20000)
    kit.wait_result(max_frames=pl.frame() + 3000, every=48)
    return finish_next(rec, pl)


# ==================================================================== m64 zerg10 (Omega)
OMEGA_TARGETS = {106, 111, 113, 114, 154, 155, 160, 167}  # CC, Barracks, Factory, Starport; Nexus, Robotics, ...


def xzerg_m64(out_name: str = "xzerg-m64") -> dict:
    """expzerg10 (Omega), next scenario none set (the campaign ends): a switch is set once slot 0 (and slot 6)
    commands no Command Center, Barracks, Factory or Starport, another once slot 1 commands no Nexus, Robotics
    Facility, Gateway or Stargate; the victory trigger wants all three. A Drone builds a Spire, the larvae morph
    Mutalisks, and they attack those buildings one by one ("power overwhelming" keeps them alive), each group
    ordered to attack the nearest itself, again whenever it turns to other units (`FAR`): attack-moving, they
    spent themselves on the computers' endless stream of units."""
    s, pl, rec, kit = xbegin(out_name, "xzerg10")
    brood(kit, hatcheries=1)
    kit.strike([0, 1, 6], OMEGA_TARGETS, near=FAR, tick=swarm_tick(kit), flyers=True, max_frames=100000)
    return finish_next(rec, pl)


PLAYS = {f.__name__: f for f in (xzerg_m49, xzerg_m50, xzerg_m51, xzerg_m51b, xzerg_m51c, xzerg_m51d, xzerg_m51e,
                                 xzerg_m51f, xzerg_m52, xzerg_m53, xzerg_m54, xzerg_m55, xzerg_m56, xzerg_m57,
                                 xzerg_m58, xzerg_m59, xzerg_m60, xzerg_m61, xzerg_m62, xzerg_m62b, xzerg_m63,
                                 xzerg_m64)}
