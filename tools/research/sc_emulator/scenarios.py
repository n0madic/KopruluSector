"""Scripted play scenarios (H03): real objectives reached with synthetic input only.

Each scenario starts a mission through the normal warp path (or the pending-mission call
for mission 63), plays it only with window messages (inputs.py / play.py) and records
what the game did: selections, orders, queues, resources, placement feedback, trigger
results. The per-frame digest trace is written as in mission.py.
"""
from __future__ import annotations

import json
import logging
import struct
import time
from pathlib import Path

from . import game, process
from .play import Player
from .session import Session, write_json

log = logging.getLogger(__name__)
OUT = (Path.home() / "koprulu-private/emu/h03")


class Recorder:
    def __init__(self, s: Session, pl: Player, out: Path):
        self.s, self.pl, self.m, self.out = s, pl, s.m, out
        out.mkdir(parents=True, exist_ok=True)
        self.trace = open(out / "trace.jsonl", "w")
        self.events: list[dict] = []
        self.last = -1
        self.result = None
        s.hook(game.FRAME_END_PC, self._frame)
        s.hook(0x4BCFF0, lambda: self.event("victory_dialog"))
        s.hook(0x4BCF80, lambda: self.event("defeat_dialog"))
        self.t0 = time.time()

    def _frame(self):
        f = self.m.r32(game.FRAME_COUNTER)
        if f == self.last:
            return
        self.last = f
        d = game.digest(self.m)
        self.trace.write(json.dumps(d, separators=(",", ":")) + "\n")
        loc = self.m.r32(0x508D50)
        if self.result is None and loc < 8 and d["res"][loc]:
            self.result = {"frame": f, "value": d["res"][loc]}
            self.event("result", value=d["res"][loc])

    def event(self, kind, **kw):
        e = {"frame": self.m.r32(game.FRAME_COUNTER), "t": self.m.clock_us // 1000, "kind": kind, **kw}
        self.events.append(e)
        log.info("%s", e)
        return e

    def snap(self, name):
        self.pl.snap(self.out / f"{name}.png")

    def finish(self, extra: dict):
        from . import coverage
        self.trace.close()
        atlas = coverage.load_atlas()
        write_json(self.out / "coverage.json", {"executed_functions": [hex(a) for a in self.s.executed_functions(atlas)],
                                                "summary": self.s.coverage_summary(atlas)})
        res = {"result": self.result, "events": self.events, "input_log": self.pl.inp.log,
               "wall_seconds": round(time.time() - self.t0, 1), "frames": self.last,
               "exceptions": self.m.services.get("exceptions", []), "unimplemented_apis": sorted(self.m.unimplemented),
               **extra}
        write_json(self.out / "result.json", res)
        return res


# The hooks the `play` command installs into a scripted play's session before it starts (state dumps and logs).
SETUP = None


def start(warp: str, cd: str, inject=None, sound_device=False) -> tuple[Session, Player]:
    """Scripted-play runs use no DirectSound device unless ``sound_device`` is set."""
    cmd = '"C:\\StarCraft\\StarCraft.exe"' + ("" if inject else f" ophelia {warp}")
    s = Session(process.Config(command_line=cmd, cd=cd, sound_device=sound_device))
    if SETUP:
        SETUP(s)
    pl = Player(s)
    m = s.m
    if inject:
        done = []

        def inj():
            if not done:
                done.append(1)
                m.inject_call(game.SET_PENDING_MISSION, (inject[2],), {"ecx": inject[0], "edx": inject[1]})
        s.hook(game.GLUE_LOOP, inj)
    s.start()
    return s, pl


def enter_game(s, pl, min_frame=24):
    m = s.m
    s.run_until(lambda: m.r32(game.GLUE_STATE) in (7, 8, 9) and m.r32(game.READY_ROOM_DIALOG) != 0, 900)
    pl.wait(2000)
    pl.inp.key(ord("S"))
    s.run_until(lambda: m.r16(game.GAME_MODE) == 3 and m.r32(game.FRAME_COUNTER) >= min_frame, 900)


def in_rect(u, rect):
    l, t, r, b = rect
    return l <= u["x"] <= r and t <= u["y"] <= b


def bring_hero(name: str, warp: str, cd: str, hero_type: int, dest: tuple, inject=None, wait_for_hero=3000,
               max_frames=20000, attack_move=False, cheats=()) -> dict:
    """Select the hero by clicking it and order it (right-click on the minimap) to the
    trigger location; reissue the order when it stops short. Ends at the first trigger
    result (victory or defeat) or after ``max_frames``."""
    out = OUT / name
    s, pl = start(warp, cd, inject)
    rec = Recorder(s, pl, out)
    enter_game(s, pl)
    m = s.m
    pl.until(lambda: pl.mine([hero_type]), max_frames=wait_for_hero)
    if cheats:
        type_cheats(pl, rec, list(cheats))
    heroes = pl.mine([hero_type])
    if not heroes:
        return rec.finish({"status": "hero not found"})
    h = heroes[0]
    rec.event("hero", unit=h["index"], x=h["x"], y=h["y"])
    cx, cy = (dest[0] + dest[2]) // 2, (dest[1] + dest[3]) // 2
    issued = 0
    start_frame = pl.frame()
    while rec.result is None and pl.frame() - start_frame < max_frames:
        hs = [u for u in pl.mine([hero_type])]
        if not hs:
            rec.event("hero_lost")
            pl.frames(400)
            break
        h = hs[0]
        sel = [u["index"] for u in pl.selection()]
        if sel != [h["index"]]:
            pl.center(h["x"], h["y"])
            pl.select_unit(h)
            rec.event("select", selection=[u["index"] for u in pl.selection()])
        if h["order"] in (2, 3, 0x17) or issued == 0:  # idle orders: Stop/Guard/Nothing
            if issued:
                rec.event("reissue", x=h["x"], y=h["y"], order=h["order"])
            if attack_move:
                pl.hotkey(ord("A"))
                pl.minimap_click(cx, cy)
            else:
                pl.minimap_click(cx, cy, right=True)
            issued += 1
            u = pl.unit(h["ptr"])
            rec.event("order", order=u["order"], target=u["target"], at=[u["x"], u["y"]])
            if issued == 1:
                rec.snap("ordered")
        pl.frames(48)
        if in_rect(h, dest):
            rec.event("hero_in_destination", x=h["x"], y=h["y"])
    pl.frames(48)
    rec.snap("end")
    return rec.finish({"status": "result" if rec.result else "no result", "hero_type": hero_type, "dest": dest,
                       "orders_issued": issued, "helper_cheats": list(cheats)})


# --------------------------------------------------------------------------- shared recipes
PLACE_CELLS = 0x675188          # per-cell validity of placement preview 0 (8 rows x 6), P27
PLACE_TILE = 0x6751EC           # u16 tile x, y under the placement cursor, P27
PLACE_ACTIVE = 0x6751F4         # placement active, P27
RALLY = 0xF8                    # CUnit rally point x, y (u16) +0xf8/+0xfa and unit +0xfc (P12)

T_MARINE, T_SCV, T_CC, T_DEPOT, T_BARRACKS = 0, 7, 106, 109, 111
T_MINERALS = (176, 177, 178)


def unit_brief(u):
    return {k: u[k] for k in ("index", "type", "owner", "x", "y", "order", "hp", "queue")}


def placement_probe(pl, rec, x, y, label, snap=True):
    """Move the placement cursor over map point (x, y) and record the validator's output."""
    pl.ensure_visible(x, y)
    sx, sy = pl.screen_point(x, y)
    pl.inp.move(sx, sy)
    pl.wait(300)
    cells = pl.m.read(PLACE_CELLS, 96)  # previews 0 (addon parent) and 1 (placed building)
    tile = (pl.m.r16(PLACE_TILE), pl.m.r16(PLACE_TILE + 2))
    e = rec.event("placement_probe", label=label, map=[x, y], screen=[sx, sy], tile=list(tile),
                  active=pl.m.r8(PLACE_ACTIVE), cells=cells.hex())
    if snap:
        rec.snap(f"placement_{label}")
    return e, (sx, sy)


BUTTON_SETS = 0x50AE98  # 250 x {u32 count, button array, linked set} (P35)
BUTTON_SET_SHOWN = 0x6AAAF0


def button_slot_for(pl, set_id, action_value):
    """Command-card position of the button whose action value is ``action_value`` in a set."""
    m = pl.m
    count, arr = struct.unpack("<II", m.read(BUTTON_SETS + 12 * set_id, 8))
    for i in range(count):
        pos, icon, cond, act, cval, aval = struct.unpack("<HHIIHH", m.read(arr + 20 * i, 16))
        if aval == action_value:
            return pos
    return None


def place_building(pl, rec, worker, menu_key, build_key, bad, good, label, build_slot=None, menu_slot=None,
                   building_type=None):
    """Select ``worker``, open its build menu (hotkey), pick the building (hotkey, or a
    command-card click when ``build_slot`` is given), probe an invalid and a valid spot,
    then click the valid one."""
    pl.center(worker["x"], worker["y"])
    w = pl.unit(worker["ptr"])
    pl.select_unit(w)
    if menu_slot:
        pl.button(menu_slot)
    else:
        pl.hotkey(menu_key)
    pl.wait(200)
    if building_type is not None:
        build_slot = button_slot_for(pl, pl.m.r16(0x6AAAF0), building_type)
        rec.event("button_lookup", button_set=pl.m.r16(0x6AAAF0), building=building_type, slot=build_slot)
    if build_slot:
        pl.button(build_slot)
    else:
        pl.hotkey(build_key)
    rec.event("placement_started", label=label, active=pl.m.r8(PLACE_ACTIVE),
              button_set=pl.m.r16(0x6AAAF0))
    placement_probe(pl, rec, bad[0], bad[1], f"{label}_invalid")
    _, (sx, sy) = placement_probe(pl, rec, good[0], good[1], f"{label}_valid")
    m0 = pl.resources()
    pl.inp.click(sx, sy)
    pl.wait(300)
    w = pl.unit(worker["ptr"])
    rec.event("placement_clicked", label=label, worker_order=w["order"], worker_target=w["target"],
              resources_before=list(m0), resources_after=list(pl.resources()))


def place_first_valid(pl, rec, worker, menu_key, building_type, points, label):
    """Select ``worker`` (or the first of up to four candidate workers that a click selects alone),
    open its build menu, pick ``building_type`` from the command card, then
    left-click candidate points until the game accepts one (placement mode ends, 0x6751f4 = 0);
    a rejected click leaves placement mode on. Returns the accepted point, or None."""
    workers = worker if isinstance(worker, list) else [worker]
    for worker in workers[:4]:
        w = pl.unit(worker["ptr"])
        pl.center(w["x"], w["y"])
        pl.select_unit(pl.unit(worker["ptr"]))
        if [u["index"] for u in pl.selection()] == [w["index"]]:
            break
    else:
        rec.event("placement_worker_not_selected", label=label, selection=[u["type"] for u in pl.selection()])
        return None
    pl.hotkey(menu_key)
    pl.wait(200)
    slot = button_slot_for(pl, pl.m.r16(BUTTON_SET_SHOWN), building_type)
    if not slot or not pl.button_visible(slot):
        rec.event("placement_unavailable", label=label, building=building_type, slot=slot,
                  button_set=pl.m.r16(BUTTON_SET_SHOWN), selection=[u["type"] for u in pl.selection()])
        pl.hotkey(0x1B)
        return None
    pl.button(slot)
    if not pl.m.r8(PLACE_ACTIVE):
        rec.event("placement_not_started", label=label, building=building_type)
        pl.hotkey(0x1B)
        return None
    m0 = pl.resources()
    for k, (x, y) in enumerate(points):
        pl.ensure_visible(x, y)
        sx, sy = pl.screen_point(x, y)
        pl.inp.move(sx, sy)
        pl.wait(100)
        pl.inp.click(sx, sy)
        pl.wait(150)
        if not pl.m.r8(PLACE_ACTIVE):
            rec.event("placement_clicked", label=label, point=[x, y], rejected_before=k,
                      worker_order=pl.unit(worker["ptr"])["order"], resources_before=list(m0),
                      resources_after=list(pl.resources()))
            return x, y
    rec.event("placement_no_valid_spot", label=label, tried=len(points))
    pl.hotkey(0x1B)
    pl.hotkey(0x1B)
    return None


def spiral(cx, cy, r0, r1, step=32):
    """Candidate points on square rings around (cx, cy), nearest first."""
    pts = []
    for r in range(r0, r1 + 1, step):
        for dx in range(-r, r + 1, step):
            pts += [(cx + dx, cy - r), (cx + dx, cy + r)]
        for dy in range(-r + step, r, step):
            pts += [(cx - r, cy + dy), (cx + r, cy + dy)]
    return pts


def wait_resources(pl, minerals, gas=0, max_frames=8000):
    return pl.until(lambda: pl.resources()[0] >= minerals and pl.resources()[1] >= gas, max_frames=max_frames,
                    step_ms=500)


def gather(pl, rec, workers, field, ctrl=True):
    """Select the workers: all of the first one's type on screen with Ctrl+click (``ctrl``), or
    the first one alone with a click; add any others with Shift+click; then right-click a
    resource field."""
    first = pl.unit(workers[0]["ptr"])
    pl.center(first["x"], first["y"])
    first = pl.unit(workers[0]["ptr"])
    if ctrl:
        pl.key_down(0x11)
    pl.click_map(first["x"], first["y"])
    if ctrl:
        pl.key_up(0x11)
    chosen = {u["index"] for u in pl.selection()}
    for w in workers[1:]:
        if w["index"] in chosen:
            continue
        pl.center(w["x"], w["y"])
        u = pl.unit(w["ptr"])
        pl.select_unit(u, shift=True)
    wanted = {w["index"] for w in workers}
    sel = [u for u in pl.selection() if u["index"] in wanted]
    if len(sel) != len(pl.selection()):
        rec.event("gather_selection_mismatch", selection=[u["index"] for u in pl.selection()])
    pl.center(field["x"], field["y"])
    pl.click_map(field["x"], field["y"], right=True)
    pl.frames(2)
    rec.event("gather", selection=[u["index"] for u in pl.selection()], field=field["index"],
              orders=[pl.unit(w["ptr"])["order"] for w in workers])


def select_building(pl, b, offsets=((0, 0), (0, -24), (-24, -8), (24, -8), (0, 24))):
    """Click a building until it alone is selected; units standing in front of it can take the
    click, so a few points of its footprint are tried."""
    if [u["index"] for u in pl.selection()] == [b["index"]]:
        return True
    pl.center(b["x"], b["y"])
    for dx, dy in offsets:
        pl.click_map(b["x"] + dx, b["y"] + dy)
        if [u["index"] for u in pl.selection()] == [b["index"]]:
            return True
    return False


def train(pl, rec, building, slot=None, key=None, label=""):
    b = pl.unit(building["ptr"])
    if not select_building(pl, b):
        return rec.event("train_selection_failed", label=label, building=unit_brief(b),
                         selection=[u["type"] for u in pl.selection()])
    before = pl.resources()
    if slot:
        pl.button(slot)
    else:
        pl.hotkey(key)
    pl.wait(200)
    b = pl.unit(building["ptr"])
    return rec.event("train", label=label, via=f"button {slot}" if slot else f"hotkey {chr(key)}",
                     queue=b["queue"], order=b["order"], resources_before=list(before),
                     resources_after=list(pl.resources()))


def wasteland(max_frames=20000, out_name="terran-wasteland") -> dict:
    """Terran real victory in Wasteland (10 Marines, a Barracks, Raynor) with hotkeys, control
    groups, attack-move, attack-unit, gathering, training, placement, rally and PrintScreen."""
    out = OUT / out_name
    s, pl = start("terran1", "original")
    rec = Recorder(s, pl, out)
    enter_game(s, pl, min_frame=24)
    m = s.m
    sel = lambda: [u["index"] for u in pl.selection()]
    # 1. drag-select, control group 1
    rec.snap("start")
    pl.inp.drag(20, 60, 240, 195)
    pl.wait(300)
    rec.event("drag_select", selection=sel())
    pl.hotkey(ord("1"), ctrl=True)
    rec.event("group_assign", key="Ctrl+1", group_size=len(sel()))
    marines = pl.mine([T_MARINE])
    scvs = pl.mine([T_SCV])
    # 2. shift-add selection
    pl.select_unit(marines[0])
    a = sel()
    pl.select_unit(marines[1], shift=True)
    rec.event("shift_add", before=a, after=sel())
    # 3. recall and double-tap centring
    pl.center(1000, 1500)
    v0 = pl.view()
    pl.hotkey(ord("1"))
    rec.event("group_recall", key="1", selection=sel(), view=list(pl.view()))
    pl.inp.key(ord("1"))
    pl.wait(120)
    pl.inp.key(ord("1"))
    pl.wait(400)
    rec.event("group_double_tap", view_before=list(v0), view_after=list(pl.view()))
    # 4. location hotkeys: Shift+F2 stores the view, F2 restores it
    vs = pl.view()
    pl.hotkey(0x71, shift=True)
    pl.center(1500, 300)
    vm = pl.view()
    pl.hotkey(0x71)
    pl.wait(300)
    rec.event("location_hotkey", stored=list(vs), moved=list(vm), restored=list(pl.view()))
    # 5. Ctrl+click: all marines on screen
    pl.hotkey(ord("1"))
    pl.hotkey(ord("1"))
    pl.wait(300)
    mk = pl.unit(pl.mine([T_MARINE])[0]["ptr"])
    pl.key_down(0x11)
    pl.click_map(mk["x"], mk["y"])
    pl.key_up(0x11)
    rec.event("ctrl_click", selection=sel(), types=[u["type"] for u in pl.selection()])
    # 6. attack-move the group towards Raynor through the minimap
    raynor = [u for u in pl.units(types=[19])][0]
    pl.hotkey(ord("1"))
    pl.hotkey(ord("A"))
    pl.minimap_click(raynor["x"], raynor["y"])
    pl.frames(4)
    rec.event("attack_move", target=[raynor["x"], raynor["y"]],
              orders=[[u["index"], u["order"], u["target"]] for u in pl.selection()])
    rec.snap("attack_move")
    attacked = False
    got_raynor = False
    t_start = pl.frame()
    while pl.frame() - t_start < 4000:
        pl.frames(24)
        mine_r = pl.mine([19])
        if mine_r and not got_raynor:
            got_raynor = True
            rec.event("raynor_joined", unit=mine_r[0]["index"])
            break
        if not attacked:
            group = pl.mine([T_MARINE])
            if not group:
                break
            gx = sum(u["x"] for u in group) // len(group)
            gy = sum(u["y"] for u in group) // len(group)
            enemies = [u for u in pl.units(owner=4) if abs(u["x"] - gx) < 220 and abs(u["y"] - gy) < 150]
            if enemies:
                e = enemies[0]
                pl.hotkey(ord("1"))
                pl.click_map(e["x"], e["y"], right=True)
                pl.frames(2)
                rec.event("attack_unit", enemy=unit_brief(e),
                          orders=[[u["index"], u["order"], u["target_unit"] == e["ptr"]] for u in pl.selection()])
                rec.snap("attack_unit")
                attacked = True
                pl.hotkey(ord("A"))
                pl.minimap_click(raynor["x"], raynor["y"])
    if not got_raynor:
        return rec.finish({"status": "Raynor not reached"})
    # 7. let the joining transmission (camera moves) finish, stop the group (hotkey S), then
    #    send the workers to the minerals by the Command Center
    pl.frames(400)
    rec.snap("after_raynor")
    pl.hotkey(ord("1"))
    pl.hotkey(ord("S"))
    pl.frames(8)
    rec.event("hotkey_stop", orders=[[u["index"], u["order"]] for u in pl.selection()])
    cc = pl.mine([T_CC])[0]
    fields = sorted(pl.units(owner=11, types=list(T_MINERALS)), key=lambda f: abs(f["x"] - cc["x"]) + abs(f["y"] - cc["y"]))
    scvs = pl.mine([T_SCV])
    gather(pl, rec, scvs, fields[0])
    pl.hotkey(ord("2"), ctrl=True)
    # 7b. bring the fighting units (group 1, plus Raynor via Shift+click) to the base: attack-move
    raynor_u = pl.mine([19])
    mk = pl.unit(pl.mine([T_MARINE])[0]["ptr"])
    pl.center(mk["x"], mk["y"])
    mk = pl.unit(mk["ptr"])
    pl.key_down(0x11)
    pl.click_map(mk["x"], mk["y"])
    pl.key_up(0x11)
    if raynor_u:
        pl.center(raynor_u[0]["x"], raynor_u[0]["y"])
        pl.select_unit(pl.unit(raynor_u[0]["ptr"]), shift=True)
    pl.hotkey(ord("1"), ctrl=True)
    guard = (cc["x"] + 64, cc["y"] - 260)
    pl.hotkey(ord("A"))
    pl.minimap_click(*guard)
    rec.event("defend_move", group=[(u["index"], u["type"]) for u in pl.selection()], target=list(guard))
    # 8. Command Center: rally on minerals, train SCVs by button and hotkey
    pl.center(cc["x"], cc["y"])
    pl.select_unit(cc)
    pl.click_map(fields[1]["x"], fields[1]["y"], right=True)
    c = m.read(cc["ptr"] + RALLY, 8)
    rec.event("rally", building=cc["index"], rally=list(struct.unpack("<HH", c[:4])), rally_unit=struct.unpack("<I", c[4:])[0])
    wait_resources(pl, 50)
    train(pl, rec, cc, slot=1, label="SCV")
    wait_resources(pl, 50)
    train(pl, rec, cc, key=ord("S"), label="SCV")
    # 9. PrintScreen
    files_before = set(m.services["fs"].overlay)
    pl.inp.key(0x2C)
    pl.wait(500)
    shots = [k for k in m.services["fs"].overlay if k not in files_before]
    for k in shots:
        (out / "printscreen.png").write_bytes(bytes(m.services["fs"].overlay[k]))
    rec.event("printscreen", files=[m.services["fs"].overlay_names[k] for k in shots],
              bytes=[len(m.services["fs"].overlay[k]) for k in shots])
    # 10. Barracks: invalid spot (on the Command Center), valid spot south-east of it
    ok = False
    for attempt in range(4):
        wait_resources(pl, 150)
        builder = pl.mine([T_SCV])[0]
        place_building(pl, rec, builder, ord("B"), ord("B"), (cc["x"], cc["y"]), (cc["x"] + 64, cc["y"] - 192),
                       "barracks" if attempt == 0 else f"barracks_retry{attempt}")
        ok = pl.until(lambda: any(u["status"] & 1 for u in pl.mine([T_BARRACKS])) or
                      (not pl.mine([T_BARRACKS]) and pl.unit(builder["ptr"])["order"] not in (30, 33)
                       and pl.frame() % 1 == 0 and False), max_frames=2500)
        if ok:
            break
        rec.event("barracks_attempt_failed", attempt=attempt, barracks=[unit_brief(u) for u in pl.mine([T_BARRACKS])],
                  builder=unit_brief(pl.unit(builder["ptr"])))
        if pl.mine([T_BARRACKS]):
            ok = pl.until(lambda: any(u["status"] & 1 for u in pl.mine([T_BARRACKS])), max_frames=2500)
            if ok:
                break
    rec.event("barracks_complete" if ok else "barracks_not_complete",
              barracks=[unit_brief(u) for u in pl.mine([T_BARRACKS])])
    if not ok:
        return rec.finish({"status": "barracks not built"})
    bk = pl.mine([T_BARRACKS])[0]
    pl.center(bk["x"], bk["y"])
    pl.select_unit(bk)
    pl.click_map(bk["x"] - 120, bk["y"] + 40, right=True)
    c = m.read(bk["ptr"] + RALLY, 8)
    rec.event("rally", building=bk["index"], rally=list(struct.unpack("<HH", c[:4])))
    # 11. Marines until victory (button once, then the hotkey)
    first = True
    while rec.result is None and pl.frame() < max_frames:
        if len(pl.mine([T_MARINE])) + sum(1 for q in pl.unit(bk["ptr"])["queue"] if q == T_MARINE) >= 10:
            pl.frames(48)
            continue
        if not wait_resources(pl, 50, max_frames=3000):
            break
        train(pl, rec, bk, slot=1 if first else None, key=None if first else ord("M"), label="Marine")
        first = False
    pl.wait(2000)
    rec.snap("end")
    return rec.finish({"status": "victory" if rec.result and rec.result["value"] == 3 else "no victory",
                       "marines": len(pl.mine([T_MARINE]))})


T_VESSEL, T_FACTORY, T_EBAY, T_REFINERY, T_MSHOP = 9, 113, 122, 110, 120
U_ENERGY = 0xA2


def terran_sandbox(max_frames=12000, out_name="terran-commandcard") -> dict:
    """Mission T8 (Big Push base): Stop/Hold/Patrol buttons, Defensive Matrix (targeted spell),
    gas gathering, an Engineering Bay upgrade, and the Machine Shop add-on."""
    out = OUT / out_name
    s, pl = start("terran7", "original")
    rec = Recorder(s, pl, out)
    enter_game(s, pl, min_frame=24)
    m = s.m
    sel = lambda: [u["index"] for u in pl.selection()]
    scvs = pl.mine([T_SCV])
    refinery = pl.mine([T_REFINERY])
    fields = pl.units(owner=11, types=list(T_MINERALS))
    if refinery:
        gather(pl, rec, scvs[:3], refinery[0], ctrl=False)
    cc = pl.mine([T_CC])
    if cc and fields:
        near = sorted(fields, key=lambda f: abs(f["x"] - cc[0]["x"]) + abs(f["y"] - cc[0]["y"]))
        gather(pl, rec, scvs[3:], near[0], ctrl=False)
    # Stop / Hold / Patrol on one Marine (mouse on the command card; Patrol by hotkey + click)
    mk = pl.mine([T_MARINE])[0]
    pl.center(mk["x"], mk["y"])
    pl.select_unit(mk)
    for slot, label in ((2, "Stop"), (5, "Hold Position")):
        pl.button(slot)
        pl.frames(2)
        rec.event("button", slot=slot, label=label, unit=unit_brief(pl.unit(mk["ptr"])))
    pl.hotkey(ord("P"))
    pl.click_map(mk["x"] + 120, mk["y"])
    pl.frames(2)
    rec.event("patrol", unit=unit_brief(pl.unit(mk["ptr"])), target=pl.unit(mk["ptr"])["target"])
    pl.hotkey(ord("S"))
    rec.event("hotkey_stop", unit=unit_brief(pl.unit(mk["ptr"])))
    # Defensive Matrix from the Science Vessel on the Marine
    vs = pl.mine([T_VESSEL])
    if vs:
        v = vs[0]
        pl.center(v["x"], v["y"])
        pl.select_unit(v)
        e0 = m.r16(v["ptr"] + U_ENERGY) >> 8
        mk = pl.unit(mk["ptr"])
        pl.center(mk["x"], mk["y"])
        mk = pl.unit(mk["ptr"])
        pl.hotkey(ord("D"))
        rec.event("targeting_mode", selection=[u["index"] for u in pl.selection()])
        sx, sy = pl.screen_point(mk["x"], mk["y"])
        pl.inp.click(sx, sy)
        pl.frames(8)
        rec.event("spell_ordered", vessel_order=pl.unit(v["ptr"])["order"], target=pl.unit(v["ptr"])["target_unit"] == mk["ptr"])
        pl.until(lambda: (m.r16(v["ptr"] + U_ENERGY) >> 8) < e0 or pl.unit(v["ptr"])["order"] not in (141,),
                 max_frames=900, step_ms=300)
        rec.event("spell_defensive_matrix", vessel=v["index"], energy_before=e0,
                  energy_after=m.r16(v["ptr"] + U_ENERGY) >> 8, vessel_order=pl.unit(v["ptr"])["order"],
                  target=unit_brief(pl.unit(mk["ptr"])), target_status=hex(pl.unit(mk["ptr"])["status"]))
        rec.snap("defensive_matrix")
    # minimap: left click centres the view, right click moves the selection
    v0 = pl.view()
    pl.minimap_click(2000, 1500)
    rec.event("minimap_left", view_before=list(v0), view_after=list(pl.view()))
    pl.select_unit(pl.unit(mk["ptr"])) if pl.on_view(mk["x"], mk["y"]) else None
    pl.center(mk["x"], mk["y"])
    pl.select_unit(pl.unit(mk["ptr"]))
    pl.minimap_click(mk["x"] + 200, mk["y"], right=True)
    pl.frames(2)
    rec.event("minimap_right", unit=unit_brief(pl.unit(mk["ptr"])), target=pl.unit(mk["ptr"])["target"])
    # attack a specific enemy unit with the Marine (right-click on it)
    # Units hidden by fog cannot be targeted (a click there becomes an attack-move), so the
    # explicit attack targets a visible stationary unit: the player's own Supply Depot.
    enemies = pl.mine([T_DEPOT])
    mk = pl.unit(mk["ptr"])
    if enemies:
        e = min(enemies, key=lambda u: abs(u["x"] - mk["x"]) + abs(u["y"] - mk["y"]))
        pl.center(mk["x"], mk["y"])
        pl.select_unit(pl.unit(mk["ptr"]))
        pl.center(e["x"], e["y"])
        e = pl.unit(e["ptr"])
        pl.hotkey(ord("A"))
        e = pl.unit(e["ptr"])
        sx, sy = pl.screen_point(e["x"], e["y"])
        pl.inp.click(sx, sy)
        pl.frames(4)
        u = pl.unit(mk["ptr"])
        rec.event("attack_unit", via="A + click on the unit", enemy=unit_brief(e), marine_order=u["order"],
                  targets_enemy=u["target_unit"] == e["ptr"])
        pl.hotkey(ord("S"))
    # Engineering Bay upgrade (Infantry Weapons, button 1) and Machine Shop (hotkey C)
    eb = pl.mine([T_EBAY])
    if eb:
        wait_resources(pl, 100, 100, max_frames=6000)
        e = train(pl, rec, eb[0], slot=1, label="EngineeringBay button 1")
        b = pl.unit(eb[0]["ptr"])
        rec.event("upgrade_state", order=b["order"], building=unit_brief(b))
    fa = pl.mine([T_FACTORY])
    if fa:
        wait_resources(pl, 50, 50, max_frames=6000)
        f = pl.unit(fa[0]["ptr"])
        pl.center(f["x"], f["y"])
        pl.select_unit(f)
        pl.hotkey(ord("C"))
        rec.event("addon_key", placement_active=m.r8(PLACE_ACTIVE), factory=unit_brief(pl.unit(f["ptr"])))
        if m.r8(PLACE_ACTIVE):
            placement_probe(pl, rec, f["x"] - 200, f["y"], "machineshop_invalid")
            _, (sx, sy) = placement_probe(pl, rec, f["x"] + 80, f["y"] + 16, "machineshop_valid")
            pl.inp.click(sx, sy)
            pl.wait(300)
        rec.event("addon_started", factory=unit_brief(pl.unit(f["ptr"])), resources=list(pl.resources()),
                  shops=[unit_brief(u) for u in pl.mine([T_MSHOP])])
        ok = pl.until(lambda: any(u["status"] & 1 for u in pl.mine([T_MSHOP])), max_frames=3000)
        rec.event("addon_complete" if ok else "addon_not_complete",
                  shops=[unit_brief(u) for u in pl.mine([T_MSHOP])])
        if ok:
            shop = pl.mine([T_MSHOP])[0]
            wait_resources(pl, 100, 100, max_frames=3000)
            train(pl, rec, shop, slot=1, label="MachineShop button 1 (research)")
            b = pl.unit(shop["ptr"])
            rec.event("research_state", order=b["order"], building=unit_brief(b))
            pl.until(lambda: pl.unit(shop["ptr"])["order"] != 76, max_frames=4000)
            wait_resources(pl, 100, 100, max_frames=3000)
            train(pl, rec, shop, slot=2, label="MachineShop button 2 (research)")
            b = pl.unit(shop["ptr"])
            rec.event("research_state", order=b["order"], building=unit_brief(b))
    rec.snap("end")
    return rec.finish({"status": "done"})


def worker_build(warp, cd, out_name, worker_type, menu_key, build_key, building_type, base_type, inject=None,
                 max_frames=6000, build_slot=None) -> dict:
    """A Probe warp-in or a Drone morph next to the main base building: the placement cursor is
    probed over the base itself (invalid) and over candidate spots around it; the first spot
    the game accepts is clicked; then the run continues until the building completes."""
    out = OUT / out_name
    s, pl = start(warp, cd, inject)
    rec = Recorder(s, pl, out)
    enter_game(s, pl, min_frame=24)
    base = pl.mine([base_type])[0]
    before = {u["index"] for u in pl.mine([building_type])}
    new = lambda: [u for u in pl.mine([building_type]) if u["index"] not in before]
    candidates = [(160, 0), (-160, 0), (0, 160), (0, -160), (192, 128), (-192, 128), (192, -128), (-192, -128)]
    placed = None
    for dx, dy in candidates:
        w = sorted(pl.mine([worker_type]), key=lambda u: abs(u["x"] - base["x"]) + abs(u["y"] - base["y"]))[0]
        place_building(pl, rec, w, menu_key, build_key, (base["x"], base["y"]), (base["x"] + dx, base["y"] + dy),
                       f"{out_name}_{dx}_{dy}", build_slot=build_slot)
        pl.frames(96)
        if new() or pl.unit(w["ptr"])["order"] in (30, 25, 31):
            pl.until(lambda: bool(new()), max_frames=1500)
            if new():
                placed = (dx, dy)
                break
        pl.hotkey(0x1B)
    rec.event("building_appeared", spot=placed, buildings=[unit_brief(u) for u in new()])
    ok = bool(placed) and pl.until(lambda: any(u["status"] & 1 for u in new()), max_frames=max_frames)
    done = [unit_brief(u) for u in new() if u["status"] & 1]
    rec.event("building_complete" if ok else "building_not_complete", completed=done)
    rec.snap("end")
    return rec.finish({"status": "complete" if ok else "incomplete"})


def escort(name: str, warp: str, cd: str, hero_type: int, dest: tuple, box=200, max_frames=15000, inject=None) -> dict:
    """Drag-select the hero and the units around it, assign them to group 1, and attack-move the
    group (A + minimap click) to the trigger location, re-issuing whenever the hero is idle."""
    out = OUT / name
    s, pl = start(warp, cd, inject)
    rec = Recorder(s, pl, out)
    enter_game(s, pl)
    pl.until(lambda: pl.mine([hero_type]), max_frames=3000)
    h = pl.mine([hero_type])[0]
    pl.center(h["x"], h["y"])
    h = pl.unit(h["ptr"])
    sx, sy = pl.screen_point(h["x"], h["y"])
    pl.inp.drag(max(sx - box, 2), max(sy - box // 2, 2), min(sx + box, 637), min(sy + box // 2, 330))
    pl.wait(300)
    pl.hotkey(ord("1"), ctrl=True)
    rec.event("group", selection=[(u["index"], u["type"]) for u in pl.selection()])
    cx, cy = (dest[0] + dest[2]) // 2, (dest[1] + dest[3]) // 2
    start_frame = pl.frame()
    issued = 0
    while rec.result is None and pl.frame() - start_frame < max_frames:
        hs = pl.mine([hero_type])
        if not hs:
            rec.event("hero_lost")
            pl.frames(400)
            break
        h = hs[0]
        if issued == 0 or h["order"] in (2, 3, 0x17):
            pl.hotkey(ord("1"))
            pl.hotkey(ord("A"))
            pl.minimap_click(cx, cy)
            issued += 1
            pl.frames(2)
            rec.event("attack_move", n=issued, hero=[h["x"], h["y"]], group=len(pl.selection()),
                      orders=sorted({u["order"] for u in pl.selection()}))
        pl.frames(48)
    pl.frames(48)
    rec.snap("end")
    return rec.finish({"status": "result" if rec.result else "no result", "orders_issued": issued})


# --------------------------------------------------------------------------- H03b: helper cheats
HELPER_CHEATS = {"power overwhelming", "show me the money", "operation cwal", "food for thought", "the gathering",
                 "medieval man", "modify the phase variance", "black sheep wall", "war aint what it used to be"}


def type_cheats(pl, rec, cheats):
    """Type helper cheats into the in-game message line (Enter, text, Enter). Only cheats that
    do not end the mission are allowed."""
    for c in cheats:
        assert c in HELPER_CHEATS, c
        before = pl.m.r32(0x5432FC)
        pl.inp.key(0x0D)
        pl.wait(200)
        pl.inp.text(c)
        pl.wait(200)
        pl.inp.key(0x0D)
        pl.wait(400)
        rec.event("helper_cheat", text=c, cheat_flags_before=hex(before), cheat_flags_after=hex(pl.m.r32(0x5432FC)))


def protoss_find_zeratul(max_frames=12000, out_name="protoss-find-zeratul-po") -> dict:
    """P6: Tassadar (invincible through "power overwhelming") moves to Zeratul; the map's victory
    trigger requires the current player to command Zeratul (unit 75)."""
    out = OUT / out_name
    s, pl = start("protoss6", "original")
    rec = Recorder(s, pl, out)
    enter_game(s, pl)
    type_cheats(pl, rec, ["power overwhelming"])
    t = pl.mine([79])[0]
    pl.center(t["x"], t["y"])
    t = pl.unit(t["ptr"])
    sx, sy = pl.screen_point(t["x"], t["y"])
    pl.inp.drag(max(sx - 120, 2), max(sy - 80, 2), min(sx + 120, 637), min(sy + 80, 330))
    pl.wait(300)
    pl.hotkey(ord("1"), ctrl=True)
    rec.event("group", selection=[(u["index"], u["type"]) for u in pl.selection()])
    z = [u for u in pl.units(types=[75])][0]
    dest = (z["x"], z["y"])
    start_frame = pl.frame()
    n = 0
    while rec.result is None and pl.frame() - start_frame < max_frames:
        if pl.mine([75]) and n >= 0:
            rec.event("zeratul_joined", unit=pl.mine([75])[0]["index"]) if not any(
                e["kind"] == "zeratul_joined" for e in rec.events) else None
        t = pl.mine([79])
        if not t:
            rec.event("hero_lost")
            break
        if n == 0 or t[0]["order"] in (2, 3, 0x17):
            pl.hotkey(ord("1"))
            pl.minimap_click(*dest, right=True)
            n += 1
            pl.frames(2)
            rec.event("move", n=n, tassadar=[t[0]["x"], t[0]["y"]], order=pl.unit(t[0]["ptr"])["order"])
        pl.frames(48)
    pl.wait(2000)
    rec.snap("end")
    return rec.finish({"status": "victory" if rec.result and rec.result["value"] == 3 else "no victory",
                       "helper_cheats": ["power overwhelming"], "victory_trigger": {
                           "action": "Victory", "players": "all players (18)",
                           "conditions": ["Command: current player commands at least 1 Zeratul (unit 75)"]}})


def zerg_among_the_ruins(max_frames=30000, out_name="zerg-among-the-ruins") -> dict:
    """Z1: Spawning Pool, Hydralisk Den (Drones), then the Zerglings, invincible through
    "power overwhelming", attack-move to each remaining building of player 6 (slot 5) until the
    map's victory trigger fires (player 6 commands no buildings, the current player has a Spawning
    Pool and a Hydralisk Den). "show me the money" pays for the buildings, "operation cwal"
    speeds up construction."""
    out = OUT / out_name
    s, pl = start("zerg1", "original")
    rec = Recorder(s, pl, out)
    enter_game(s, pl)
    cheats = ["power overwhelming", "show me the money", "operation cwal"]
    type_cheats(pl, rec, cheats)
    hatch = pl.mine([131])[0]
    # Spawning Pool west of the Hatchery (valid in H03), Hydralisk Den north-west
    drones = pl.mine([41])
    place_building(pl, rec, drones[0], ord("B"), ord("S"), (hatch["x"], hatch["y"]), (hatch["x"] - 160, hatch["y"]),
                   "spawning_pool")
    pl.until(lambda: any(u["status"] & 1 for u in pl.mine([142])), max_frames=3000)
    rec.event("pool_complete", pools=[unit_brief(u) for u in pl.mine([142])])
    for dx, dy in ((-160, -128), (0, -160), (160, -96), (-96, 160)):
        d = pl.mine([41])
        if not d:
            break
        place_building(pl, rec, d[0], ord("B"), 0, (hatch["x"], hatch["y"]), (hatch["x"] + dx, hatch["y"] + dy),
                       f"hydralisk_den_{dx}_{dy}", building_type=135)
        pl.frames(96)
        if pl.mine([135]):
            break
        pl.hotkey(0x1B)
    pl.until(lambda: any(u["status"] & 1 for u in pl.mine([135])), max_frames=3000)
    rec.event("den_complete", dens=[unit_brief(u) for u in pl.mine([135])])
    # army: all Zerglings (Ctrl+click), group 1
    zl = pl.mine([37])
    z0 = pl.unit(zl[0]["ptr"])
    pl.center(z0["x"], z0["y"])
    z0 = pl.unit(z0["ptr"])
    pl.key_down(0x11)
    pl.click_map(z0["x"], z0["y"])
    pl.key_up(0x11)
    for z in zl:
        if z["index"] not in {u["index"] for u in pl.selection()}:
            u = pl.unit(z["ptr"])
            pl.center(u["x"], u["y"])
            pl.select_unit(pl.unit(z["ptr"]), shift=True)
    pl.hotkey(ord("1"), ctrl=True)
    rec.event("army", selection=[(u["index"], u["type"]) for u in pl.selection()])
    enemy = 5
    start_frame = pl.frame()
    target = None
    while rec.result is None and pl.frame() - start_frame < max_frames:
        bl = [u for u in pl.units(owner=enemy) if 106 <= u["type"] < 200]
        if not bl:
            pl.frames(48)
            continue
        army = pl.mine([37])
        gx = sum(u["x"] for u in army) // max(len(army), 1)
        gy = sum(u["y"] for u in army) // max(len(army), 1)
        nearest = min(bl, key=lambda u: abs(u["x"] - gx) + abs(u["y"] - gy))
        if target is None or target["index"] != nearest["index"] or all(u["order"] in (2, 3, 0x17) for u in army):
            target = nearest
            pl.hotkey(ord("1"))
            pl.hotkey(ord("A"))
            pl.minimap_click(target["x"], target["y"])
            pl.frames(2)
            rec.event("attack_move", target=unit_brief(target), remaining_buildings=len(bl),
                      orders=sorted({u["order"] for u in pl.selection()}))
        pl.frames(96)
    pl.wait(2000)
    rec.snap("end")
    return rec.finish({"status": "victory" if rec.result and rec.result["value"] == 3 else "no victory",
                       "helper_cheats": cheats, "victory_trigger": {
                           "action": "Victory", "players": "all players (18)",
                           "conditions": ["Command: player 6 (slot 5) commands at most 0 buildings",
                                          "Command: current player commands at least 1 Spawning Pool (142)",
                                          "Command: current player commands at least 1 Hydralisk Den (135)"]}})


# --------------------------------------------------------------------------- H04: remaining input paths
PAUSE_FLAG = 0x67517C
T_DROPSHIP = 11
S_IN_TRANSPORT, S_BURROWED = 0x40, 0x10


def _sel(pl):
    return [u["index"] for u in pl.selection()]


def terran_misc(out_name="terran-misc") -> dict:
    """New Gettysburg (T9 base): Tab, F10/Escape, F3/F4, Alt-click recent-group recall, training
    cancel (Cancel button and Escape), construction cancel (Escape), Dropship load by right-click
    and by the Load button, Unload All at a point."""
    out = OUT / out_name
    s, pl = start("terran9", "original")
    rec = Recorder(s, pl, out)
    enter_game(s, pl)
    m = s.m
    marines = pl.mine([T_MARINE])
    mk = pl.unit(marines[0]["ptr"])
    pl.center(mk["x"], mk["y"])
    mk = pl.unit(mk["ptr"])
    pl.key_down(0x11)
    pl.click_map(mk["x"], mk["y"])
    pl.key_up(0x11)
    group = _sel(pl)
    rec.event("ctrl_click_marines", selection=group)
    # Tab
    pl.hotkey(0x09)
    rec.event("tab", selection_before=group, selection_after=_sel(pl))
    # F10 menu and Escape
    pl.hotkey(0x79)
    pl.wait(500)
    rec.snap("f10")
    rec.event("f10", pause_flag=m.r32(PAUSE_FLAG), frame_before=pl.frame())
    f0 = pl.frame()
    pl.wait(1000)
    rec.event("f10_frames_while_open", advanced=pl.frame() - f0)
    pl.hotkey(0x1B)
    pl.wait(500)
    rec.event("escape_after_f10", pause_flag=m.r32(PAUSE_FLAG), frames_advance=pl.frame() - f0)
    rec.snap("after_escape")
    # F3 / F4 jump to saved screen positions
    for vk, name in ((0x72, "F3"), (0x73, "F4")):
        v0 = pl.view()
        pl.hotkey(vk)
        pl.wait(300)
        rec.event("function_key", key=name, view_before=list(v0), view_after=list(pl.view()))
    # Alt-click: the marines' selection became a recent group; select something else, Alt-click one
    cc = pl.mine([T_CC])[0]
    pl.center(cc["x"], cc["y"])
    pl.select_unit(cc)
    other = _sel(pl)
    mk = pl.unit(marines[0]["ptr"])
    pl.center(mk["x"], mk["y"])
    mk = pl.unit(mk["ptr"])
    pl.inp.wm.post(pl.inp.hwnd, 0x104, 0x12, 0x20000001)   # WM_SYSKEYDOWN Alt
    pl.click_map(mk["x"], mk["y"])
    pl.inp.wm.post(pl.inp.hwnd, 0x105, 0x12, 0xC0000001)   # WM_SYSKEYUP Alt
    pl.wait(200)
    rec.event("alt_click", group_before=group, selection_before=other, selection_after=_sel(pl))
    # Training cancel: two Marines, then the Cancel button, then Escape
    bk = pl.mine([T_BARRACKS])[0]
    wait_resources(pl, 100)
    train(pl, rec, bk, key=ord("M"), label="Marine")
    train(pl, rec, bk, key=ord("M"), label="Marine")
    b = pl.unit(bk["ptr"])
    r0 = pl.resources()
    pl.button(9)
    pl.frames(4)
    rec.event("cancel_button", queue_before=b["queue"], queue_after=pl.unit(bk["ptr"])["queue"],
              resources_before=list(r0), resources_after=list(pl.resources()))
    r0 = pl.resources()
    q0 = pl.unit(bk["ptr"])["queue"]
    pl.hotkey(0x1B)
    pl.frames(4)
    rec.event("cancel_escape", queue_before=q0, queue_after=pl.unit(bk["ptr"])["queue"],
              resources_before=list(r0), resources_after=list(pl.resources()))
    # Construction cancel: SCV builds a Supply Depot, the incomplete depot is selected, Escape
    wait_resources(pl, 100)
    before = {u["index"] for u in pl.mine([T_DEPOT])}
    scv = pl.mine([T_SCV])[0]
    for dx, dy in ((0, 160), (160, 0), (-160, 0), (0, -160), (192, 160), (-192, 160)):
        place_building(pl, rec, scv, ord("B"), ord("S"), (cc["x"], cc["y"]), (cc["x"] + dx, cc["y"] + dy),
                       f"depot_{dx}_{dy}")
        if pl.until(lambda: any(u["index"] not in before for u in pl.mine([T_DEPOT])), max_frames=700):
            break
        pl.hotkey(0x1B)
    new = [u for u in pl.mine([T_DEPOT]) if u["index"] not in before]
    if new:
        d = pl.unit(new[0]["ptr"])
        pl.frames(24)
        r0 = pl.resources()
        pl.center(d["x"], d["y"])
        pl.select_unit(pl.unit(d["ptr"]))
        st0 = pl.unit(d["ptr"])["status"]
        sel0 = _sel(pl)
        pl.hotkey(0x1B)
        pl.frames(8)
        rec.event("construction_escape", selection=sel0, still_exists=any(u["index"] == d["index"] for u in pl.mine([T_DEPOT])),
                  resources=list(pl.resources()), button_set=m.r16(0x6AAAF0))
        pl.select_unit(pl.unit(d["ptr"]))
        rec.event("construction_selected", selection=_sel(pl), button_set=m.r16(0x6AAAF0))
        pl.button(9)  # Cancel (button sets 231/233, position 9)
        pl.frames(8)
        rec.event("construction_cancel", via="Cancel button 9", depot=d["index"], status_before=hex(st0),
                  still_exists=any(u["index"] == d["index"] for u in pl.mine([T_DEPOT])),
                  resources_before=list(r0), resources_after=list(pl.resources()),
                  scv=unit_brief(pl.unit(scv["ptr"])))
    else:
        rec.event("construction_cancel_skipped", reason="no depot placed")
    # Dropship: load by right-click (marine on dropship) and by the Load button
    ds = pl.mine([T_DROPSHIP])
    if ds:
        d = pl.unit(ds[0]["ptr"])
        ms = [pl.unit(u["ptr"]) for u in pl.mine([T_MARINE])]
        ms.sort(key=lambda u: abs(u["x"] - d["x"]) + abs(u["y"] - d["y"]))
        a = ms[0]
        # the Load-button target must not lie under the hovering Dropship (the click would pick it)
        b2 = next((u for u in ms[1:] if abs(u["x"] - d["x"]) + abs(u["y"] - d["y"]) > 80), ms[1])
        pl.center(a["x"], a["y"])
        pl.select_unit(pl.unit(a["ptr"]))
        d = pl.unit(d["ptr"])
        pl.click_map(d["x"], d["y"], right=True)
        pl.frames(4)
        rec.event("load_rightclick", marine=unit_brief(pl.unit(a["ptr"])))
        pl.until(lambda: pl.unit(a["ptr"])["status"] & S_IN_TRANSPORT, max_frames=600)
        rec.event("loaded", marine=a["index"], status=hex(pl.unit(a["ptr"])["status"]))
        # fly the Dropship away from the Marines first, so the Load target is not under it
        d = pl.unit(d["ptr"])
        pl.center(d["x"], d["y"])
        pl.select_unit(pl.unit(d["ptr"]))
        pl.minimap_click(d["x"] + 320, d["y"] + 160, right=True)
        pl.until(lambda: pl.unit(d["ptr"])["order"] in (2, 3, 0x17), max_frames=400)
        d = pl.unit(d["ptr"])
        rest = [pl.unit(u["ptr"]) for u in ms[1:]]
        b2 = next((u for u in sorted(rest, key=lambda u: abs(u["x"] - d["x"]) + abs(u["y"] - d["y"]))
                   if abs(u["x"] - d["x"]) + abs(u["y"] - d["y"]) > 120), rest[-1])
        pl.center(d["x"], d["y"])
        pl.select_unit(pl.unit(d["ptr"]))
        pl.hotkey(ord("3"), ctrl=True)          # Dropship -> group 3
        bb = pl.unit(b2["ptr"])
        pl.center(bb["x"], bb["y"])              # view on the Marine before targeting starts
        pl.hotkey(ord("3"))                      # recall the Dropship (view unchanged)
        pl.button(8)  # Load (Dropship button set 11, position 8)
        rec.event("load_targeting", button_set=m.r16(0x6AAAF0), selection=_sel(pl))
        bb = pl.unit(b2["ptr"])
        sx, sy = pl.screen_point(bb["x"], bb["y"])
        pl.inp.click(sx, sy)
        pl.frames(4)
        rec.event("load_button", dropship_order=pl.unit(d["ptr"])["order"], marine=unit_brief(pl.unit(b2["ptr"])))
        ok = pl.until(lambda: pl.unit(b2["ptr"])["status"] & S_IN_TRANSPORT, max_frames=900)
        rec.event("loaded" if ok else "not_loaded", marine=b2["index"], status=hex(pl.unit(b2["ptr"])["status"]))
        # Unload All at a point: U + click
        d = pl.unit(d["ptr"])
        pl.center(d["x"], d["y"])
        pl.select_unit(pl.unit(d["ptr"]))
        tx, ty = d["x"] + 200, d["y"] + 60
        pl.hotkey(ord("U"))
        rec.event("unload_key", selection=_sel(pl), dropship_order=pl.unit(d["ptr"])["order"])
        sx, sy = pl.screen_point(tx, ty)
        pl.inp.click(sx, sy)
        pl.frames(4)
        rec.event("unload_ordered", dropship=unit_brief(pl.unit(d["ptr"])), target=pl.unit(d["ptr"])["target"])
        pl.until(lambda: not (pl.unit(a["ptr"])["status"] & S_IN_TRANSPORT) and
                 not (pl.unit(b2["ptr"])["status"] & S_IN_TRANSPORT), max_frames=900)
        rec.event("unloaded", marines=[unit_brief(pl.unit(a["ptr"])), unit_brief(pl.unit(b2["ptr"]))],
                  dropship=unit_brief(pl.unit(d["ptr"])))
    pl.wait(1000)
    rec.snap("end")
    return rec.finish({"status": "done"})


def zerg_burrow(out_name="zerg-burrow") -> dict:
    """Among the Ruins: research Burrow at the Hatchery (helper cheats "show me the money",
    "operation cwal"), then burrow and unburrow Zerglings with the hotkey U."""
    out = OUT / out_name
    s, pl = start("zerg1", "original")
    rec = Recorder(s, pl, out)
    enter_game(s, pl)
    type_cheats(pl, rec, ["show me the money", "operation cwal"])
    hatch = pl.mine([131])[0]
    pl.center(hatch["x"], hatch["y"])
    pl.select_unit(pl.unit(hatch["ptr"]))
    set_id = pl.m.r16(0x6AAAF0)
    slot = button_slot_for(pl, set_id, 11)  # tech 11 = Burrowing
    r0 = pl.resources()
    if slot:
        pl.button(slot)
    pl.frames(4)
    h = pl.unit(hatch["ptr"])
    rec.event("research_burrow", button_set=set_id, slot=slot, hatchery_order=h["order"],
              resources_before=list(r0), resources_after=list(pl.resources()))
    pl.until(lambda: pl.unit(hatch["ptr"])["order"] != h["order"], max_frames=3000)
    rec.event("research_done", hatchery_order=pl.unit(hatch["ptr"])["order"])
    zl = pl.mine([37])
    z = pl.unit(zl[0]["ptr"])
    pl.center(z["x"], z["y"])
    z = pl.unit(z["ptr"])
    pl.key_down(0x11)
    pl.click_map(z["x"], z["y"])
    pl.key_up(0x11)
    sel = pl.selection()
    pl.hotkey(ord("U"))
    pl.frames(48)
    rec.event("burrow", selection=[u["index"] for u in sel],
              statuses=[hex(pl.unit(u["ptr"])["status"]) for u in sel],
              burrowed=sum(1 for u in sel if pl.unit(u["ptr"])["status"] & S_BURROWED))
    rec.snap("burrowed")
    pl.hotkey(ord("U"))
    pl.frames(48)
    rec.event("unburrow", burrowed=sum(1 for u in sel if pl.unit(u["ptr"])["status"] & S_BURROWED),
              orders=sorted({pl.unit(u["ptr"])["order"] for u in sel}))
    pl.wait(1000)
    rec.snap("end")
    return rec.finish({"status": "done", "helper_cheats": ["show me the money", "operation cwal"]})


# --------------------------------------------------------------------------- H05
def transport_cycle(pl, rec, transport_type, passenger_types, label):
    """Load one passenger by right-clicking it onto the transport, a second by the Load button
    (position 8) and a click, then Unload All (position 9) at a point."""
    m = pl.m
    tr = pl.unit(pl.mine([transport_type])[0]["ptr"])
    ps = [pl.unit(u["ptr"]) for u in pl.mine(passenger_types)]
    ps.sort(key=lambda u: abs(u["x"] - tr["x"]) + abs(u["y"] - tr["y"]))
    a = ps[0]
    pl.center(a["x"], a["y"])
    pl.select_unit(pl.unit(a["ptr"]))
    tr = pl.unit(tr["ptr"])
    pl.click_map(tr["x"], tr["y"], right=True)
    pl.frames(4)
    rec.event(f"{label}_load_rightclick", passenger=unit_brief(pl.unit(a["ptr"])))
    ok = pl.until(lambda: pl.unit(a["ptr"])["status"] & S_IN_TRANSPORT, max_frames=900)
    rec.event(f"{label}_loaded" if ok else f"{label}_not_loaded", unit=a["index"], status=hex(pl.unit(a["ptr"])["status"]))
    tr = pl.unit(tr["ptr"])
    pl.center(tr["x"], tr["y"])
    pl.select_unit(pl.unit(tr["ptr"]))
    pl.hotkey(ord("3"), ctrl=True)
    rest = [pl.unit(u["ptr"]) for u in ps[1:]]
    b = next((u for u in sorted(rest, key=lambda u: abs(u["x"] - tr["x"]) + abs(u["y"] - tr["y"]))
              if abs(u["x"] - tr["x"]) + abs(u["y"] - tr["y"]) > 80), rest[0])
    pl.center(b["x"], b["y"])
    pl.hotkey(ord("3"))
    pl.button(8)
    rec.event(f"{label}_load_targeting", button_set=m.r16(0x6AAAF0), selection=_sel(pl))
    b = pl.unit(b["ptr"])
    sx, sy = pl.screen_point(b["x"], b["y"])
    pl.inp.click(sx, sy)
    pl.frames(4)
    rec.event(f"{label}_load_button", transport_order=pl.unit(tr["ptr"])["order"], passenger=unit_brief(b))
    ok = pl.until(lambda: pl.unit(b["ptr"])["status"] & S_IN_TRANSPORT, max_frames=1200)
    rec.event(f"{label}_loaded" if ok else f"{label}_not_loaded", unit=b["index"], status=hex(pl.unit(b["ptr"])["status"]))
    tr = pl.unit(tr["ptr"])
    pl.center(tr["x"], tr["y"])
    pl.select_unit(pl.unit(tr["ptr"]))
    pl.button(9)
    tx, ty = tr["x"] + 160, tr["y"] + 64
    sx, sy = pl.screen_point(tx, ty)
    pl.inp.click(sx, sy)
    pl.frames(4)
    rec.event(f"{label}_unload_ordered", transport=unit_brief(pl.unit(tr["ptr"])), target=pl.unit(tr["ptr"])["target"])
    pl.until(lambda: not (pl.unit(a["ptr"])["status"] & S_IN_TRANSPORT) and
             not (pl.unit(b["ptr"])["status"] & S_IN_TRANSPORT), max_frames=1200)
    rec.event(f"{label}_unloaded", passengers=[unit_brief(pl.unit(a["ptr"])), unit_brief(pl.unit(b["ptr"]))],
              transport=unit_brief(pl.unit(tr["ptr"])))


def protoss_shuttle(out_name="protoss-shuttle") -> dict:
    out = OUT / out_name
    s, pl = start("protoss5", "original")
    rec = Recorder(s, pl, out)
    enter_game(s, pl)
    transport_cycle(pl, rec, 69, [65, 66], "shuttle")
    pl.wait(1000)
    rec.snap("end")
    return rec.finish({"status": "done"})


def zerg_h05(out_name="zerg-h05", warp="zerg6") -> dict:
    """Z6 (default; Z1 does not allow the Lair): Select Larva (hotkey S, button 1), Larva->Zergling egg cancel, Drone->
    Evolution Chamber cancel (before "operation cwal", so the morph is still running), then Spawning
    Pool, Lair, Ventral Sacs, Overlord load/unload, F1 help."""
    out = OUT / out_name
    s, pl = start(warp, "original")
    rec = Recorder(s, pl, out)
    enter_game(s, pl)
    m = s.m
    type_cheats(pl, rec, ["show me the money"])
    hatch = pl.mine([131])[0]

    def select_hatchery():
        h = pl.unit(hatch["ptr"])
        pl.center(h["x"], h["y"])
        pl.click_map(h["x"], h["y"] - 30)  # above the Larvae that sit at its foot
        rec.event("hatchery_selected", selection=_sel(pl), button_set=m.r16(0x6AAAF0))
    select_hatchery()
    pl.hotkey(ord("S"))
    rec.event("select_larva_hotkey", selection=[(u["index"], u["type"]) for u in pl.selection()])
    select_hatchery()
    pl.button(1)
    pl.wait(300)
    rec.event("select_larva_button", selection=[(u["index"], u["type"]) for u in pl.selection()],
              button_set=m.r16(0x6AAAF0))
    # Larva -> Zergling egg (hotkey Z), then cancel (egg button 9)
    larva = pl.selection()[0]
    r0 = pl.resources()
    pl.hotkey(ord("Z"))
    pl.until(lambda: pl.unit(larva["ptr"])["type"] == 36, max_frames=120)
    e = pl.unit(larva["ptr"])
    rec.event("morph_egg", unit=e["index"], type=e["type"], order=e["order"], resources_before=list(r0),
              resources_after=list(pl.resources()))
    if e["type"] == 36:
        pl.select_unit(e)
        r1 = pl.resources()
        pl.button(9)
        pl.frames(16)
        rec.event("egg_cancel", unit=e["index"], type_after=pl.unit(e["ptr"])["type"], selection=_sel(pl),
                  resources_before=list(r1), resources_after=list(pl.resources()))
    # Drone -> Evolution Chamber, then cancel while the morph runs
    drone = pl.mine([41])[0]
    for dx, dy in ((0, -160), (-160, -128), (160, -96)):
        place_building(pl, rec, drone, ord("B"), 0, (hatch["x"], hatch["y"]), (hatch["x"] + dx, hatch["y"] + dy),
                       f"evo_{dx}_{dy}", building_type=139)
        if pl.until(lambda: pl.unit(drone["ptr"])["type"] == 139, max_frames=500):
            break
        pl.hotkey(0x1B)
    d = pl.unit(drone["ptr"])
    if d["type"] == 139:
        pl.frames(24)
        pl.center(d["x"], d["y"])
        pl.select_unit(pl.unit(d["ptr"]))
        r1 = pl.resources()
        rec.event("building_selected", selection=_sel(pl), button_set=m.r16(0x6AAAF0),
                  status=hex(pl.unit(d["ptr"])["status"]))
        pl.button(9)
        pl.frames(16)
        u = pl.unit(d["ptr"])
        rec.event("morph_building_cancel", unit=d["index"], type_after=u["type"], order=u["order"],
                  resources_before=list(r1), resources_after=list(pl.resources()))
    type_cheats(pl, rec, ["operation cwal"])
    # Spawning Pool, Lair, Ventral Sacs
    drones = pl.mine([41])
    place_building(pl, rec, drones[0], ord("B"), ord("S"), (hatch["x"], hatch["y"]), (hatch["x"] - 160, hatch["y"]),
                   "spawning_pool")
    pl.until(lambda: any(u["status"] & 1 for u in pl.mine([142])), max_frames=3000)
    rec.event("pool_state", pools=[(unit_brief(u), hex(u["status"])) for u in pl.mine([142])])
    select_hatchery()
    pl.wait(300)
    pl.button(7)
    pl.frames(8)
    rec.event("lair_morph", via="button 7", hatchery=unit_brief(pl.unit(hatch["ptr"])))
    if pl.unit(hatch["ptr"])["order"] == 23 and pl.unit(hatch["ptr"])["type"] == 131:
        pl.hotkey(ord("L"))
        pl.frames(8)
        rec.event("lair_morph", via="hotkey L", hatchery=unit_brief(pl.unit(hatch["ptr"])))
    pl.until(lambda: pl.unit(hatch["ptr"])["type"] == 132 and pl.unit(hatch["ptr"])["order"] == 23, max_frames=4000)
    rec.event("lair_done", hatchery=unit_brief(pl.unit(hatch["ptr"])))
    select_hatchery()
    r1 = pl.resources()
    pl.button(4)
    pl.frames(8)
    h = pl.unit(hatch["ptr"])
    rec.event("ventral_sacs", lair=unit_brief(h), resources_before=list(r1), resources_after=list(pl.resources()))
    pl.until(lambda: pl.unit(hatch["ptr"])["order"] != h["order"], max_frames=4000)
    rec.event("ventral_sacs_done", lair_order=pl.unit(hatch["ptr"])["order"])
    transport_cycle(pl, rec, 42, [38, 37, 41], "overlord")
    # F1 help
    pl.hotkey(0x70)
    pl.wait(500)
    rec.snap("f1_help")
    f0 = pl.frame()
    pl.wait(1000)
    rec.event("f1_help", pause_flag=m.r32(PAUSE_FLAG), frames_while_open=pl.frame() - f0,
              game_mode=m.r16(game.GAME_MODE))
    pl.hotkey(0x1B)
    pl.wait(800)
    rec.event("escape_after_f1", pause_flag=m.r32(PAUSE_FLAG), frames_after=pl.frame() - f0)
    rec.snap("end")
    return rec.finish({"status": "done", "helper_cheats": ["show me the money", "operation cwal"]})


# --------------------------------------------------------------------------- H06
FRAME_MS_PC = 0x444F71        # RunPendingFrames: `call 0x4be500` with ECX = ms per frame (P15)
MUSIC_VOL, SFX_VOL = 0x6D313C, 0x6D3140


def speed_keys(out_name="speed-keys", with_input=True, frames=900) -> dict:
    """Wasteland: game speed '+'/'-' (Local.dll ASCII accelerators 40056/40058), Shift+F2..F4 store
    and F2..F4 recall screen positions, Ctrl+F2..F4 (unbound), Ctrl+M / Ctrl+S music and sound
    toggles. With ``with_input`` False the same run gets no input (the digest baseline)."""
    out = OUT / out_name
    s, pl = start("terran1", "original")
    rec = Recorder(s, pl, out)
    m = s.m
    ms_log = []
    s.hook(FRAME_MS_PC, lambda: ms_log.append((m.r32(game.FRAME_COUNTER), m.reg(__import__("unicorn").x86_const.UC_X86_REG_ECX))))
    enter_game(s, pl)
    if with_input:
        pl.frames(100 - pl.frame())
        for ch, label in (("+", "plus"), ("+", "plus"), ("-", "minus")):
            sp0 = m.r32(game.SPEED_INDEX)
            pl.inp.text(ch)
            pl.frames(6)
            rec.event("speed_key", key=ch, speed_before=sp0, speed_after=m.r32(game.SPEED_INDEX),
                      ms_per_frame=ms_log[-1][1] if ms_log else None)
        pl.frames(300 - pl.frame())
        # screen positions: Shift+F2..F4 store, Ctrl+F2..F4 (no binding), F2..F4 recall
        spots = [(400, 400), (1200, 600), (600, 1500)]
        for i, (vk, (x, y)) in enumerate(zip((0x71, 0x72, 0x73), spots)):
            pl.center(x, y)
            pl.hotkey(vk, shift=True)
            rec.event("store_view", key=f"Shift+F{i + 2}", view=list(pl.view()),
                      saved=[m.r16(0x526550 + 4 * i), m.r16(0x526552 + 4 * i)])
        for i, vk in enumerate((0x71, 0x72, 0x73)):
            v0 = pl.view()
            pl.hotkey(vk, ctrl=True)
            rec.event("ctrl_f", key=f"Ctrl+F{i + 2}", view_before=list(v0), view_after=list(pl.view()),
                      saved=[m.r16(0x526550 + 4 * i), m.r16(0x526552 + 4 * i)])
        pl.center(1800, 1800)
        for i, vk in enumerate((0x71, 0x72, 0x73)):
            pl.hotkey(vk)
            pl.wait(200)
            rec.event("recall_view", key=f"F{i + 2}", view=list(pl.view()))
        for vk, label in ((ord("M"), "Ctrl+M"), (ord("M"), "Ctrl+M"), (ord("S"), "Ctrl+S"), (ord("S"), "Ctrl+S")):
            v0 = (m.r32(MUSIC_VOL), m.r32(SFX_VOL))
            pl.hotkey(vk, ctrl=True)
            rec.event("toggle", key=label, music_sfx_before=list(v0), music_sfx_after=[m.r32(MUSIC_VOL), m.r32(SFX_VOL)])
    pl.frames(frames - pl.frame())
    rec.snap("end")
    write_json(out / "frame_ms.json", ms_log)
    return rec.finish({"status": "done", "with_input": with_input})


def sfx_toggle(out_name="sfx-toggle") -> dict:
    """Ctrl+S twice with a (recording) DirectSound device: ToggleSfxMute 0x49ec40."""
    from unicorn import x86_const as X
    out = OUT / out_name
    s, pl = start("terran1", "original", sound_device=True)
    rec = Recorder(s, pl, out)
    m = s.m
    s.hook(0x49ECA0, lambda: rec.event("StopAllSounds_0x49eca0"))
    s.hook(0x4C3760, lambda: rec.event("ui_volume_set_0x4c3760", ecx=struct.unpack("<i", struct.pack("<I", m.reg(X.UC_X86_REG_ECX)))[0]))
    enter_game(s, pl)
    pl.frames(100)
    for i in range(2):
        v0 = (m.r32(0x6AA954), m.r32(SFX_VOL), m.r32(0x6AAA50))
        pl.hotkey(ord("S"), ctrl=True)
        rec.event("ctrl_s", n=i + 1, dsound_6aa954=hex(v0[0]), sfx_6d3140_before=v0[1], saved_6aaa50_before=v0[2],
                  sfx_6d3140_after=m.r32(SFX_VOL), saved_6aaa50_after=m.r32(0x6AAA50))
    return rec.finish({"status": "done", "sound_device": True})


# --------------------------------------------------------------------------- H07
def track_units(pl, rec, ptrs, every=200, frames=12000, label="track", stop=None):
    """Log positions and orders of the given units every ``every`` frames."""
    start_f = pl.frame()
    while pl.frame() - start_f < frames and rec.result is None:
        pl.frames(every)
        rec.event(label, units=[{k: pl.unit(p)[k] for k in ("index", "type", "x", "y", "order", "hp")} for p in ptrs])
        if stop and stop():
            break


def explore_detectors(warp="xzerg9b", inject=(63, 0, 1), out_name="dark-origin-map") -> dict:
    out = OUT / out_name
    s, pl = start(warp, "expansion", inject)
    rec = Recorder(s, pl, out)
    enter_game(s, pl)
    pl.until(lambda: pl.mine([75]), max_frames=3000)
    det = [unit_brief(u) for u in pl.units() if u["type"] in (124, 9, 84, 162, 107, 146)]
    rec.event("detectors", units=det)
    rec.event("all_enemy", units=[(u["type"], u["owner"], u["x"], u["y"]) for u in pl.units()
                                  if u["owner"] not in (pl.local, 11)])
    return rec.finish({"status": "done"})


T_LARVA, T_EGG, T_DRONE, T_ZERGLING, T_HYDRA, T_OVERLORD, T_HATCH, T_POOL, T_DEN = 35, 36, 41, 37, 38, 42, 131, 142, 135
T_EXTRACTOR, T_GEYSER = 149, 188
IDLE_ORDERS = (2, 3, 0x17)

HARVEST_ORDERS = (79, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 70, 25, 45, 30)
BUILD_ORDERS = tuple(range(24, 35))   # Drone/SCV/Probe placement and construction orders
GAS_ORDERS = (81, 82, 83, 84)


def idle_workers_to_minerals(pl, worker_type, fields, limit=4):
    """Every worker that is not harvesting or building is clicked and right-clicked onto one of
    the nearest mineral fields."""
    for k, d in enumerate([u for u in pl.mine([worker_type])
                           if u["order"] not in HARVEST_ORDERS + BUILD_ORDERS][:limit]):
        u = pl.unit(d["ptr"])
        pl.center(u["x"], u["y"])
        u = pl.unit(d["ptr"])
        pl.select_unit(u)
        if [x["index"] for x in pl.selection()] != [u["index"]] or not fields:
            continue
        f = fields[k % min(len(fields), 4)]
        pl.click_map(f["x"], f["y"], right=True)


def attack_wave(pl, rec, unit_types, target, exclude=(), boxes=6):
    """Up to ``boxes`` times: Ctrl+click an idle army unit, which selects every unit of its type on
    the screen (up to 12; other types such as workers and heroes stay unselected), then A + minimap
    click on the target. Returns the count sent."""
    sent = 0
    for _ in range(boxes):
        near = [u for u in pl.mine(list(unit_types)) if u["order"] in IDLE_ORDERS and u["type"] not in exclude]
        if not near:
            break
        c0 = pl.unit(near[0]["ptr"])
        pl.center(c0["x"], c0["y"])
        c0 = pl.unit(near[0]["ptr"])
        pl.key_down(0x11)
        pl.click_map(c0["x"], c0["y"])
        pl.key_up(0x11)
        sel = pl.selection()
        if not sel or any(u["type"] != c0["type"] for u in sel):
            rec.event("wave_selection_rejected", selection=[u["type"] for u in sel])
            break
        pl.hotkey(ord("A"))
        pl.minimap_click(target["x"], target["y"])
        sent += len(sel)
    return sent


class StopAfterResult:
    """Context for a scripted-play loop: once the mission result is set, the game leaves the map
    for the end dialogs and score screen, so a scripted action that needs the minimap or a unit
    may fail; such an error is recorded and ends the loop instead of the run."""

    def __init__(self, rec):
        self.rec = rec

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        if exc is None or self.rec.result is None:
            return False
        self.rec.event("script_stopped_after_result", error=f"{exc_type.__name__}: {exc}")
        return True


def rally_producers(pl, rec, state, types, stage):
    """Set the rally point of every completed producer of the given types to the staging point
    (select it, right-click the minimap), once per building."""
    for h in pl.mine(list(types)):
        if h["status"] & 1 and h["index"] not in state["rallied"]:
            if select_building(pl, pl.unit(h["ptr"])):
                pl.minimap_click(*stage, right=True)
                state["rallied"].add(h["index"])
                rec.event("rally", building=unit_brief(h), stage=list(stage))


def staged_waves(pl, rec, state, army_types, enemy, stage, wave_size, reinforce, ready=True,
                 strays_attack=False, exclude=()):
    """Attack waves from a staging point: the first when ``wave_size`` idle units stand there,
    then every ``reinforce``. Idle units elsewhere are moved back to the staging point every 480
    frames, or, with ``strays_attack``, attack-move to the nearest enemy building within 800 px."""
    idle = [u for u in pl.mine(list(army_types)) if u["order"] in IDLE_ORDERS]
    staged = [u for u in idle if abs(u["x"] - stage[0]) + abs(u["y"] - stage[1]) < 320]
    strays = [u for u in idle if u not in staged]
    targets = [u for u in pl.units(owner=enemy) if 106 <= u["type"] < 200]
    if strays and pl.frame() - state["last_gather"] > 480:
        state["last_gather"] = pl.frame()
        for k in range(0, min(len(strays), 24), 12):
            grp = strays[k:k + 12]
            gx, gy = sum(u["x"] for u in grp) // len(grp), sum(u["y"] for u in grp) // len(grp)
            near_t = [t for t in targets if abs(t["x"] - gx) + abs(t["y"] - gy) < 800] if strays_attack else []
            if select_group(pl, grp):
                if near_t:
                    t = min(near_t, key=lambda t: abs(t["x"] - gx) + abs(t["y"] - gy))
                    pl.hotkey(ord("A"))
                    pl.minimap_click(t["x"], t["y"])
                else:
                    pl.minimap_click(*stage, right=True)
    if ready and targets and len(staged) >= (wave_size if state["waves"] == 0 else reinforce):
        tgt = min(targets, key=lambda u: abs(u["x"] - stage[0]) + abs(u["y"] - stage[1]))
        sent = attack_wave(pl, rec, army_types, tgt, exclude=exclude)
        if sent:
            state["waves"] += 1
            rec.event("wave", n=state["waves"], sent=sent, target=unit_brief(tgt), enemy_buildings=len(targets),
                      army=len(pl.mine(list(army_types))))


def zerg_no_cheats(max_frames=40000, out_name="zerg-z1-nocheat", wave_size=24, reinforce=12, stage_frac=30) -> dict:
    """Z1 Among the Ruins without any cheat: Drones mine minerals and gas (Extractor), Larvae morph
    Drones/Overlords/Zerglings/Hydralisks, Spawning Pool, Hydralisk Den and a second Hatchery are
    built, and the army attack-moves in waves (drag-select 12 at a time, A + minimap click) at the
    nearest remaining building of player slot 5 until the map's victory trigger fires."""
    out = OUT / out_name
    s, pl = start("zerg1", "original")
    rec = Recorder(s, pl, out)
    enter_game(s, pl)
    hatch = pl.mine([T_HATCH])[0]
    hx, hy = hatch["x"], hatch["y"]
    fields = sorted(pl.units(owner=11, types=list(T_MINERALS)), key=lambda f: abs(f["x"] - hx) + abs(f["y"] - hy))
    geysers = sorted(pl.units(owner=11, types=[T_GEYSER]), key=lambda f: abs(f["x"] - hx) + abs(f["y"] - hy))
    state = {"pool": False, "den": False, "extractor": False, "gas_drones": False, "hatch2": False,
             "last_ovl": -10000, "waves": 0, "rallied": set(), "last_gather": 0}
    enemy_b = [u for u in pl.units(owner=5) if 106 <= u["type"] < 200]
    ex = sum(u["x"] for u in enemy_b) // max(len(enemy_b), 1)
    ey = sum(u["y"] for u in enemy_b) // max(len(enemy_b), 1)
    stage = (hx + (ex - hx) * stage_frac // 100, hy + (ey - hy) * stage_frac // 100)
    rec.event("staging", stage=list(stage), enemy_centre=[ex, ey])

    def larva():
        return [u for u in pl.mine([T_LARVA])]

    def complete(types):
        return [u for u in pl.mine(types) if u["status"] & 1]

    def supply2():
        used = 2 * len(pl.mine([T_DRONE])) + len(pl.mine([T_ZERGLING])) + 2 * len(pl.mine([T_HYDRA])) + \
            2 * len(pl.mine([T_EGG]))
        cap = 2 * len(complete([T_HATCH, 132, 133])) + 16 * len(complete([T_OVERLORD]))
        return used, min(cap, 400)

    def morph_one(key):
        lv = larva()
        if not lv:
            return False
        u = pl.unit(lv[0]["ptr"])
        pl.center(u["x"], u["y"])
        u = pl.unit(lv[0]["ptr"])
        pl.select_unit(u)
        if [x["type"] for x in pl.selection()] != [T_LARVA]:
            return False
        pl.hotkey(key)
        pl.frames(4)
        return pl.unit(u["ptr"])["type"] == T_EGG

    def idle_drones_to_minerals():
        idle_workers_to_minerals(pl, T_DRONE, fields)

    def build(btype, points, label):
        ds = [u for u in pl.mine([T_DRONE]) if u["order"] not in GAS_ORDERS]
        if not ds:
            return False
        d = ds[-1]
        n0 = len(pl.mine([btype]))
        if place_first_valid(pl, rec, d, ord("B"), btype, points, label) is None:
            return False
        return pl.until(lambda: bool(pl.mine([btype])) and len(pl.mine([btype])) > n0, max_frames=600)

    idle_drones_to_minerals()
    last_report = 0
    with StopAfterResult(rec):
        while rec.result is None and pl.frame() < max_frames:
            mins, gas = pl.resources()
            used, cap = supply2()
            drones = len(pl.mine([T_DRONE]))
            pool_done, den_done = bool(complete([T_POOL])), bool(complete([T_DEN]))
            state["pool"] = state["pool"] or bool(pl.mine([T_POOL]))
            state["extractor"] = bool(pl.mine([T_EXTRACTOR]))
            state["gas_drones"] = state["gas_drones"] and state["extractor"]
            state["den"] = state["den"] or bool(pl.mine([T_DEN]))
            state["hatch2"] = state["hatch2"] or len(pl.mine([T_HATCH, 132, 133])) > 1
            if cap - used < 6 and mins >= 100 and pl.frame() - state["last_ovl"] > 450 and cap < 400:
                if morph_one(ord("O")):
                    state["last_ovl"] = pl.frame()
                    rec.event("overlord_egg", supply=[used, cap])
            elif drones < 8 and not state["pool"] and mins >= 50 and cap - used >= 2:
                morph_one(ord("D"))
            elif not state["pool"] and mins >= 200:
                state["pool"] = build(T_POOL, spiral(hx, hy, 128, 288), "pool")
                rec.event("pool_started", ok=state["pool"])
            elif state["pool"] and not state["extractor"] and geysers and mins >= 50:
                gx_, gy_ = geysers[0]["x"], geysers[0]["y"]
                state["extractor"] = build(T_EXTRACTOR, [(gx_ + dx, gy_ + dy) for dy in range(-64, 33, 16)
                                                         for dx in range(-96, 33, 16)], "extractor")
                rec.event("extractor_started", ok=state["extractor"], geyser=unit_brief(geysers[0]))
            elif state["extractor"] and not state["gas_drones"] and complete([T_EXTRACTOR]):
                ext = complete([T_EXTRACTOR])[0]
                ds = [u for u in pl.mine([T_DRONE]) if u["order"] in HARVEST_ORDERS][:3]
                gather(pl, rec, ds, ext, ctrl=False)
                state["gas_drones"] = True
            elif pool_done and not state["den"] and mins >= 100 and gas >= 50:
                state["den"] = build(T_DEN, spiral(hx, hy, 128, 288), "den")
                rec.event("den_started", ok=state["den"])
            elif drones < 14 and mins >= 50 and cap - used >= 2:
                morph_one(ord("D"))
            elif state["den"] and not state["hatch2"] and mins >= 350:
                state["hatch2"] = build(T_HATCH, spiral(hx, hy, 256, 416), "hatchery2")
                rec.event("hatchery2_started", ok=state["hatch2"])
            elif den_done and mins >= 75 and gas >= 25 and cap - used >= 2:
                morph_one(ord("H"))
            # spend the remaining larvae (several per pass)
            for _ in range(3):
                mins, gas = pl.resources()
                used, cap = supply2()
                if not pool_done or not larva() or cap - used < 2 or mins < 50:
                    break
                morph_one(ord("H") if den_done and gas >= 25 and mins >= 75 else ord("Z"))
            rally_producers(pl, rec, state, [T_HATCH, 132, 133], stage)
            idle_drones_to_minerals()
            staged_waves(pl, rec, state, (T_ZERGLING, T_HYDRA), 5, stage, wave_size, reinforce, ready=den_done)
            if pl.frame() - last_report >= 1000:
                last_report = pl.frame()
                rec.event("status", minerals=mins, gas=gas, supply=[used, cap], drones=len(pl.mine([T_DRONE])),
                          zerglings=len(pl.mine([T_ZERGLING])), hydralisks=len(pl.mine([T_HYDRA])),
                          overlords=len(pl.mine([T_OVERLORD])), larvae=len(larva()),
                          enemy_buildings=len([u for u in pl.units(owner=5) if 106 <= u["type"] < 200]),
                          buildings=sorted(u["type"] for u in pl.mine() if 106 <= u["type"] < 200))
            pl.frames(48)
    pl.wait(2000)
    rec.snap("end")
    return rec.finish({"status": "victory" if rec.result and rec.result["value"] == 3 else "no victory",
                       "helper_cheats": [], "waves": state["waves"], "victory_trigger": {
                           "conditions": ["Command: player 6 (slot 5) commands at most 0 buildings",
                                          "Command: current player commands at least 1 Spawning Pool (142)",
                                          "Command: current player commands at least 1 Hydralisk Den (135)"]}})

def mission_survey(out_name="p6-survey", warp="protoss6") -> dict:
    """P6 (The Hunt for Tassadar) without cheats: list the player's and every other player's units
    at mission start, for route planning."""
    out = OUT / out_name
    s, pl = start(warp, "original")
    rec = Recorder(s, pl, out)
    enter_game(s, pl)
    pl.frames(48)
    rec.event("local", player=pl.local)
    rec.event("all_units", units=[(u["type"], u["owner"], u["x"], u["y"], u["order"]) for u in pl.units()
                                  if u["owner"] != 11])
    return rec.finish({"status": "done"})


T_TASSADAR, T_ZERATUL = 79, 75
# P6 trigger locations (CHK MRGN): "open sesame!" opens door10, "Free me!" opens door15 (Zeratul's cell)
P6_ROUTE = (
    ("marines 2 (rescue)", 520, 1450),
    ("open sesame!", 560, 2112),
    ("door10", 416, 3312),
    ("south-west group (rescue)", 290, 3870),
    ("door11", 720, 3680),
)


# waypoints taken from the game's own path in run p6-nocheat-b (marines 2 -> open sesame!)
P6_EAST_LOOP = (("east 1", 2044, 1314), ("east 2", 2759, 1440), ("ambush junction", 3470, 1800),
                ("loop south", 3025, 2231), ("loop west 1", 2302, 2047), ("loop west 2", 1568, 1788))
P6_ROUTE_FULL = P6_ROUTE[:1] + P6_EAST_LOOP + P6_ROUTE[1:] + (
    ("zerg nest", 2090, 3760), ("east of the nest", 2550, 3440), ("Free me!", 3728, 3360),
    ("door15", 2656, 3584), ("Zeratul", 3073, 3820))
P6_ROUTE_REINFORCED = (
    ("teleportation field", 2640, 512),
    ("Marines 1 (rescue)", 2064, 992),
    ("marines 2 (rescue)", 520, 1450),
) + P6_EAST_LOOP + P6_ROUTE[1:]


def select_group(pl, units):
    """Click the first unit, Shift+click the others (at most 12 in the selection)."""
    units = units[:12]
    if not units:
        return []
    u = pl.unit(units[0]["ptr"])
    pl.center(u["x"], u["y"])
    pl.select_unit(pl.unit(u["ptr"]))
    for v in units[1:]:
        if v["index"] in {x["index"] for x in pl.selection()}:
            continue
        v = pl.unit(v["ptr"])
        pl.ensure_visible(v["x"], v["y"])
        pl.select_unit(pl.unit(v["ptr"]), shift=True)
    return pl.selection()


U_ENERGY, U_SHIELDS = 0xA2, 0x60
STATUS_BURROWED = 0x10
STORM_COST, STORM_RADIUS = 75, 48


def energy(pl, u):
    return pl.m.r16(u["ptr"] + U_ENERGY) >> 8


def cast_storm(pl, rec, caster_ptr, enemies, friends, reach=320):
    """Psionic Storm (T, then a left click on the map) on the densest visible enemy cluster within
    ``reach`` of the caster, if no friendly unit stands inside the storm."""
    c = pl.unit(caster_ptr)
    if energy(pl, c) < STORM_COST:
        return False
    cand = [e for e in enemies if abs(e["x"] - c["x"]) + abs(e["y"] - c["y"]) < reach
            and not e["status"] & STATUS_BURROWED]
    best, score = None, 0
    for e in cand:
        n = sum(1 for o in cand if abs(o["x"] - e["x"]) < STORM_RADIUS and abs(o["y"] - e["y"]) < STORM_RADIUS)
        n += 3 * (e["type"] == 50)                      # Infested Terran
        if n > score and not any(abs(f["x"] - e["x"]) < STORM_RADIUS + 16 and abs(f["y"] - e["y"]) < STORM_RADIUS + 16
                                 for f in friends):
            best, score = e, n
    if best is None or score < 3:
        return False
    pl.center(c["x"], c["y"])
    pl.select_unit(pl.unit(caster_ptr))
    if [u["type"] for u in pl.selection()] != [c["type"]]:
        return False
    e0 = energy(pl, c)
    pl.hotkey(ord("T"))
    b = pl.unit(best["ptr"])
    pl.click_map(b["x"], b["y"])
    pl.frames(8)
    rec.event("storm", target=unit_brief(b), score=score, energy_before=e0, energy_after=energy(pl, pl.unit(caster_ptr)),
              order=pl.unit(caster_ptr)["order"])
    return True


# burrowed (player 6) and guarding (player 4) Zerg groups on the route, from the unit table at mission start
P6_STORM_SITES = ((965, 1240), (1480, 1290), (1058, 1920), (805, 2214), (879, 2390), (780, 3620),
                  (2090, 3760), (2550, 3440))


def protoss_p6_nocheat(max_frames=40000, out_name="protoss-p6-nocheat", route=P6_ROUTE, leg_frames=3000,
                       arrive=192, tassadar_mode="group", storm=True, storm_sites=(), leg_energy=0) -> dict:
    """P6 (Into the Darkness) without any cheat: Tassadar and the escort (Zealots, then every rescued
    unit) attack-move through the trigger locations of the route (A + minimap click), so that the
    bring-triggers open the doors and the rescue-passive Zeratul joins the player; Tassadar casts
    Psionic Storm (T + click) on visible enemy clusters and, once each, on the ground of the known
    ``storm_sites`` when he is within range; before each leg he waits (bounded) for ``leg_energy``. ``tassadar_mode``: "group" (he attack-moves
    with the escort), "follow" (he is moved to the escort's centre every 240 frames) or "stay"."""
    out = OUT / out_name
    s, pl = start("protoss6", "original")
    rec = Recorder(s, pl, out)
    enter_game(s, pl)
    tass = pl.mine([T_TASSADAR])[0]["ptr"]
    enemy_owners = (3, 6)

    def army():
        return [u for u in pl.mine() if (tassadar_mode == "group" or u["type"] != T_TASSADAR) and u["type"] < 106
                and u["hp"] > 0]

    def snapshot(label, **kw):
        t = pl.unit(tass)
        rec.event(label, army=[(u["type"], u["x"], u["y"], u["order"], u["hp"]) for u in army()],
                  tassadar=[t["x"], t["y"], t["hp"], pl.m.r32(tass + U_SHIELDS) >> 8, energy(pl, t)], **kw)

    def attack_move(units, x, y):
        for k in range(0, len(units), 12):
            if select_group(pl, units[k:k + 12]):
                pl.hotkey(ord("A"))
                pl.minimap_click(x, y)

    sites = list(storm_sites)

    def storm_site():
        t = pl.unit(tass)
        if energy(pl, t) < STORM_COST:
            return
        for sx_, sy_ in sites:
            if abs(t["x"] - sx_) + abs(t["y"] - sy_) < 300:
                sites.remove((sx_, sy_))
                pl.center(t["x"], t["y"])
                pl.select_unit(pl.unit(tass))
                if [u["type"] for u in pl.selection()] != [T_TASSADAR]:
                    return
                e0 = energy(pl, t)
                pl.hotkey(ord("T"))
                pl.click_map(sx_, sy_)
                pl.frames(24)
                rec.event("storm_site", site=[sx_, sy_], energy_before=e0, energy_after=energy(pl, pl.unit(tass)))
                return

    for label, x, y in route:
        if rec.result is not None or pl.frame() >= max_frames:
            break
        if leg_energy:
            pl.until(lambda: energy(pl, pl.unit(tass)) >= leg_energy or rec.result is not None, max_frames=2500)
        snapshot("leg_start", leg=label, dest=[x, y])
        attack_move(army(), x, y)
        f0, last = pl.frame(), pl.frame()
        while rec.result is None and pl.frame() - f0 < leg_frames and pl.frame() < max_frames:
            pl.frames(24)
            if pl.unit(tass)["type"] != T_TASSADAR or pl.unit(tass)["owner"] != pl.local:
                break
            a = army()
            if pl.mine([T_ZERATUL]) and not any(e["kind"] == "zeratul_joined" for e in rec.events):
                rec.event("zeratul_joined")
            if storm and cast_storm(pl, rec, tass, [u for o in enemy_owners for u in pl.units(owner=o)], a):
                attack_move([pl.unit(tass)], x, y) if pl.unit(tass)["order"] in IDLE_ORDERS else None
            elif sites:
                n_sites = len(sites)
                storm_site()
                if len(sites) != n_sites and pl.unit(tass)["order"] in IDLE_ORDERS:
                    attack_move([pl.unit(tass)], x, y)
            near = [u for u in a if abs(u["x"] - x) + abs(u["y"] - y) < arrive]
            if len(near) * 2 >= len(a) and all(u["order"] in IDLE_ORDERS for u in near):
                break
            if pl.frame() - last >= 240:
                last = pl.frame()
                stray = [u for u in a if u["order"] in IDLE_ORDERS and abs(u["x"] - x) + abs(u["y"] - y) >= arrive]
                if stray:
                    attack_move(stray, x, y)
                t = pl.unit(tass)
                if tassadar_mode == "follow" and a:
                    cx, cy = sum(u["x"] for u in a) // len(a), sum(u["y"] for u in a) // len(a)
                    if abs(t["x"] - cx) + abs(t["y"] - cy) > 128 and t["order"] != 142:
                        pl.center(t["x"], t["y"])
                        pl.select_unit(pl.unit(tass))
                        if [u["type"] for u in pl.selection()] == [T_TASSADAR]:
                            pl.minimap_click(cx, cy, right=True)
                snapshot("progress", leg=label)
        snapshot("leg_end", leg=label, frames=pl.frame() - f0)
        if pl.unit(tass)["type"] != T_TASSADAR or pl.unit(tass)["owner"] != pl.local or not army():
            rec.event("army_lost")
            break
    pl.wait(2000)
    rec.snap("end")
    return rec.finish({"status": "victory" if rec.result and rec.result["value"] == 3 else "no victory",
                       "helper_cheats": [], "victory_trigger": {
                           "players": "player 8 (the local player)", "action": "Victory",
                           "conditions": ["Command: current player commands at least 1 Zeratul (unit 75)"]}})


T_PROBE, T_ZEALOT, T_DRAGOON, T_FENIX_Z = 64, 65, 66, 77
T_NEXUS, T_PYLON, T_ASSIMILATOR, T_GATEWAY, T_CORE, T_CANNON, T_FORGE = 154, 156, 157, 160, 164, 162, 166
P1_FENIX, P1_NEXUS = (1271, 1853), (480, 2224)


def protoss_p1_nocheat(max_frames=40000, out_name="protoss-p1-nocheat", wave_size=20, reinforce=10,
                       max_gateways=5, max_probes=20, stage_frac=10, cannons=3, use_gas=False) -> dict:
    """P1 First Strike without any cheat. The starting Zealots and Dragoons attack-move to Fenix and
    to the rescuable base of player 2 (slot 1), which joins the player. Then the Nexus trains Probes;
    Probes mine, power the rescued Gateway with a Pylon, and build Pylons, Gateways, Photon Cannons
    and (if the mission offers one) a Cybernetics Core; Gateways keep two Zealots (Dragoons after a
    Core) queued and rally to a staging point. Idle army units attack-move onto Zerg raiders near
    the base, and the army (without Fenix, whose death loses the mission) attacks in waves from the
    staging point at the nearest building of player 1 (slot 0) until the map's victory trigger fires."""
    out = OUT / out_name
    s, pl = start("protoss1", "original")
    rec = Recorder(s, pl, out)
    enter_game(s, pl)
    enemy = 0
    army_types = (T_ZEALOT, T_DRAGOON)

    def complete(types):
        return [u for u in pl.mine(types) if u["status"] & 1]

    def supply2():
        used = 2 * len(pl.mine([T_PROBE])) + 4 * len(pl.mine([T_ZEALOT, T_DRAGOON, T_FENIX_Z]))
        for b in pl.mine([T_NEXUS, T_GATEWAY]):
            used += sum({T_PROBE: 2, T_ZEALOT: 4, T_DRAGOON: 4}.get(q, 0) for q in b["queue"])
        cap = 18 * len(complete([T_NEXUS])) + 16 * len(complete([T_PYLON]))
        return used, min(cap, 400)

    def queued(b):
        return sum(q != 228 for q in pl.unit(b["ptr"])["queue"])

    # 1. rescue Fenix and the base
    group = pl.mine(army_types)
    for dest in (P1_FENIX, P1_NEXUS):
        f0 = pl.frame()
        for k in range(0, len(group), 12):
            if select_group(pl, [u for u in pl.mine(army_types)][k:k + 12]):
                pl.hotkey(ord("A"))
                pl.minimap_click(*dest)
        pl.until(lambda: pl.mine([T_NEXUS]) if dest == P1_NEXUS else pl.mine([T_FENIX_Z]), max_frames=2500)
        rec.event("rescue_leg", dest=list(dest), frames=pl.frame() - f0,
                  mine=sorted(u["type"] for u in pl.mine()))
    nexus = pl.mine([T_NEXUS])
    if not nexus:
        rec.event("base_not_rescued")
        return rec.finish({"status": "no victory", "helper_cheats": []})
    nx, ny = nexus[0]["x"], nexus[0]["y"]
    fields = sorted(pl.units(owner=11, types=list(T_MINERALS)), key=lambda f: abs(f["x"] - nx) + abs(f["y"] - ny))
    # Fenix waits next to the Nexus, away from the waves
    fz = pl.mine([T_FENIX_Z])
    if fz:
        select_group(pl, fz)
        pl.minimap_click(nx, ny + 96, right=True)
    state = {"waves": 0, "gas": False, "last_pylon": -10000, "core_tries": 0, "train_fail_snap": set(),
             "gate_powered": False, "rallied": set(), "last_gather": 0, "last_defend": 0}
    enemy_b = [u for u in pl.units(owner=enemy) if 106 <= u["type"] < 200]
    ex = sum(u["x"] for u in enemy_b) // max(len(enemy_b), 1)
    ey = sum(u["y"] for u in enemy_b) // max(len(enemy_b), 1)
    stage = (nx + (ex - nx) * stage_frac // 100, ny + (ey - ny) * stage_frac // 100)
    rec.event("staging", stage=list(stage), enemy_centre=[ex, ey])
    last_report = 0
    with StopAfterResult(rec):
        while rec.result is None and pl.frame() < max_frames:
            mins, gas = pl.resources()
            used, cap = supply2()
            probes = pl.mine([T_PROBE])
            gates = complete([T_GATEWAY])
            nex = complete([T_NEXUS])
            builders = [u for u in probes if u["order"] not in GAS_ORDERS]
            if not state["gate_powered"] and mins >= 100 and builders and pl.mine([T_GATEWAY]):
                # the rescued Gateway stands outside every Pylon's field ("Unpowered"): power it first
                g0 = pl.mine([T_GATEWAY])[0]
                if place_first_valid(pl, rec, builders[::-1], ord("B"), T_PYLON, spiral(g0["x"], g0["y"], 96, 224),
                                     "pylon_for_gateway"):
                    state["gate_powered"] = True
                    state["last_pylon"] = pl.frame()
            elif cap - used < 12 and mins >= 100 and pl.frame() - state["last_pylon"] > 300 and cap < 400 and builders:
                if place_first_valid(pl, rec, builders[::-1], ord("B"), T_PYLON, spiral(nx, ny, 160, 384), "pylon"):
                    state["last_pylon"] = pl.frame()
            elif nex and len(probes) < max_probes and mins >= 50 and cap - used >= 2 and queued(nex[0]) < 2:
                train(pl, rec, nex[0], key=ord("P"), label="probe")
            elif use_gas and not state["gas"] and len(probes) >= 8 and complete([T_ASSIMILATOR]):
                gather(pl, rec, [u for u in probes if u["order"] in HARVEST_ORDERS][:3], complete([T_ASSIMILATOR])[0],
                       ctrl=False)
                state["gas"] = True
            elif len(pl.mine([T_CANNON])) < cannons and len(probes) >= 10 and mins >= 150 and builders \
                    and complete([T_FORGE]):
                # Photon Cannons by the Nexus against Mutalisk raids
                place_first_valid(pl, rec, builders[::-1], ord("B"), T_CANNON, spiral(nx, ny, 128, 320), "photon_cannon")
            elif not pl.mine([T_CORE]) and state["core_tries"] < 2 and gates and mins >= 200 and builders:
                # Dragoons need a Cybernetics Core (advanced build menu, V), if the mission allows one
                state["core_tries"] += 1
                place_first_valid(pl, rec, builders[::-1], ord("V"), T_CORE, spiral(nx, ny, 192, 448), "cybernetics_core")
            elif len(pl.mine([T_GATEWAY])) < max_gateways and mins >= 150 and len(probes) >= 10 and builders:
                place_first_valid(pl, rec, builders[::-1], ord("B"), T_GATEWAY, spiral(nx, ny, 192, 448), "gateway")
            used, cap = supply2()
            core = bool(complete([T_CORE]))
            for g in gates:
                if queued(g) >= 2 or cap - used < 4:     # keep two units queued per Gateway
                    continue
                mins, gas = pl.resources()
                if core and mins >= 125 and gas >= 50:
                    e = train(pl, rec, g, key=ord("D"), label="dragoon")
                elif mins >= 100:
                    e = train(pl, rec, g, key=ord("Z"), label="zealot")
                else:
                    continue
                used += 4
                if e.get("resources_after") == e.get("resources_before") and g["index"] not in state["train_fail_snap"]:
                    state["train_fail_snap"].add(g["index"])
                    rec.snap(f"train_failed_{g['index']}")
            idle_workers_to_minerals(pl, T_PROBE, fields)
            if pl.frame() - state["last_defend"] > 240:
                # Zerg raiders next to a building: idle army units attack-move onto them
                mine_b = [u for u in pl.mine() if u["type"] >= 106]
                threats = [e for e in pl.units(owner=enemy) if e["type"] < 106 and e["type"] != T_OVERLORD
                           and any(abs(e["x"] - b["x"]) + abs(e["y"] - b["y"]) < 400 for b in mine_b)]
                defenders = [u for u in pl.mine(list(army_types)) if u["order"] in IDLE_ORDERS]
                if threats and defenders:
                    state["last_defend"] = pl.frame()
                    t = threats[0]
                    for k in range(0, min(len(defenders), 24), 12):
                        if select_group(pl, defenders[k:k + 12]):
                            pl.hotkey(ord("A"))
                            pl.minimap_click(t["x"], t["y"])
                    rec.event("defend", threat=unit_brief(t), threats=len(threats), defenders=len(defenders))
            rally_producers(pl, rec, state, [T_GATEWAY], stage)
            staged_waves(pl, rec, state, army_types, enemy, stage, wave_size, reinforce, strays_attack=True,
                         exclude=(T_FENIX_Z,))
            if pl.frame() - last_report >= 1000:
                last_report = pl.frame()
                rec.event("status", minerals=mins, gas=gas, supply=[used, cap], probes=len(probes),
                          zealots=len(pl.mine([T_ZEALOT])), dragoons=len(pl.mine([T_DRAGOON])),
                          fenix=bool(pl.mine([T_FENIX_Z])), buildings=sorted(u["type"] for u in pl.mine() if u["type"] >= 106),
                          enemy_buildings=sorted(u["type"] for u in pl.units(owner=enemy) if 106 <= u["type"] < 200),
                          enemy_units=len([u for u in pl.units(owner=enemy) if u["type"] < 106]),
                          threats=[(u["type"], u["x"], u["y"]) for u in pl.units(owner=enemy)
                                   if u["type"] < 106 and abs(u["x"] - nx) + abs(u["y"] - ny) < 1000])
                if last_report // 1000 % 4 == 0:
                    rec.snap(f"status_{last_report}")
            pl.frames(48)
    pl.wait(2000)
    rec.snap("end")
    return rec.finish({"status": "victory" if rec.result and rec.result["value"] == 3 else "no victory",
                       "helper_cheats": [], "waves": state["waves"], "victory_trigger": {
                           "players": "force 1", "action": "Victory",
                           "conditions": ["Command: player 1 (slot 0) commands at most 0 buildings"]}})
