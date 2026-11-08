#!/usr/bin/env python3
"""M0E reference trace: Extractor completion and first gas deliveries in the full game.

The isolated Extractor probe (docs/research/native-extractor.md) stops its completion scene
before the Windows import GetLastError (0x4d7466) that CRT rand reaches during the final
creep stamping (BuildSelf45 0x43b6e0 -> 0x43c220), so it gives no completed return and no later
gas harvesting. This driver obtains that observation from the full-game emulator
(tools/research/sc_emulator, docs/research/full-game-emulation.md) on the same authentic input
record: Zerg02 (warp ``zerg2``) contains the probe's geyser, UNIT record 14 (type 188 at
(960, 256), amount 4851), and its local player is slot 3, the probe's declared owner.

Only window messages are posted (the game's own placement and right-click order code act on
them); guest memory is read, never written. Code hooks record the conversion, the completion
stamping call and its return, CRT rand's import call inside it, and per-frame change points of
the converted CUnit and of the player's gas.

Run with the private emulator environment (Unicorn), for example:
    koprulu-private/venv/bin/python tools/oracle/emu_m0e_extractor.py
Output: <out>/result.json, trace.jsonl (frame digests), coverage.json.
"""
from __future__ import annotations

import argparse
import logging
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "research"))

from sc_emulator import game  # noqa: E402
from sc_emulator import scenarios as sc  # noqa: E402

log = logging.getLogger("emu_m0e_extractor")

DEFAULT_OUT = (Path(__file__).resolve().parents[2] / "koprulu-private/emu/m0e/extractor-zerg02")
WARP, CD = "zerg2", "original"
GEYSER_POSITION = (960, 256)          # Zerg02 UNIT section 0 record 14 (native-extractor.md)
T_DRONE, T_EXTRACTOR, T_GEYSER = 41, 149, 188
GAS_ORDERS = (81, 82, 83, 84)

# Code addresses (preferred VAs of the 1.17.0.1 executable; atlas entries)
BUILD_REFINERY = 0x41B3F0             # BuildRefinery: existing geyser lookup and conversion
DRONE_EXTRACTOR_BRANCH = 0x43BF80     # Drone Extractor branch of DroneBuild26
BUILD_SELF_CREEP_CALL = 0x43B8DB      # BuildSelf45 calls 0x43c220(type, x, y)
BUILD_SELF_CREEP_RETURN = 0x43B8E0    # instruction after that call
CREEP_STAMP = 0x43C220                # completion creep/footprint consumer
CRT_RAND_GETLASTERROR = 0x4D7466      # call [0x4eb17c] GetLastError: the probe's stop boundary
COMPLETE_UNIT = 0x424DA0              # completion helper (ECX = unit)

# CUnit fields (atlas P12 / BWAPI 1.16.1 layout)
U_ORDER_STATE = 0x4E
U_BUILD_TIME = 0xAC                   # u16 remaining build time
U_RESOURCE = 0xD0                     # u16 resource amount
U_STATUS = 0xDC


def unit_state(m, ptr: int) -> dict:
    b = m.read(ptr, game.UNIT_SIZE)
    return {
        "type": struct.unpack_from("<H", b, game.U_TYPE)[0],
        "owner": b[game.U_OWNER],
        "order": b[game.U_ORDER],
        "order_state": b[U_ORDER_STATE],
        "hp": struct.unpack_from("<i", b, game.U_HP)[0],
        "completed": bool(struct.unpack_from("<I", b, U_STATUS)[0] & 1),
        "build_time": struct.unpack_from("<H", b, U_BUILD_TIME)[0],
        "amount": struct.unpack_from("<H", b, U_RESOURCE)[0],
        "position": list(struct.unpack_from("<HH", b, game.U_POS)),
    }


def run(out: Path, harvest_frames: int, max_frames: int) -> dict:
    s, pl = sc.start(WARP, CD)
    rec = sc.Recorder(s, pl, out)
    m = s.m
    hooks: list[dict] = []
    in_stamp = {"active": False}
    tracked: dict = {"geyser": None, "timeline": [], "gas": [], "last": None, "last_gas": None}

    def frame() -> int:
        return m.r32(game.FRAME_COUNTER)

    def ecx() -> int:
        from unicorn import x86_const as X
        return m.reg(X.UC_X86_REG_ECX)

    def note(kind: str, **kw) -> None:
        hooks.append({"frame": frame(), "kind": kind, **kw})

    s.hook(BUILD_REFINERY, lambda: note("BuildRefinery entry", worker=ecx()))
    s.hook(DRONE_EXTRACTOR_BRANCH, lambda: note("Drone Extractor branch entry", ecx=ecx()))

    def stamp_call() -> None:
        in_stamp["active"] = True
        note("BuildSelf45 calls 0x43c220", type=ecx())

    def stamp_return() -> None:
        in_stamp["active"] = False
        note("0x43c220 returned to BuildSelf45")

    s.hook(BUILD_SELF_CREEP_CALL, stamp_call)
    s.hook(BUILD_SELF_CREEP_RETURN, stamp_return)
    s.hook(CRT_RAND_GETLASTERROR, lambda: in_stamp["active"] and note("CRT rand GetLastError call inside 0x43c220"))

    def complete_unit() -> None:
        g = tracked["geyser"]
        if g is not None and ecx() == g:
            note("0x424da0 complete unit", unit=ecx())

    s.hook(COMPLETE_UNIT, complete_unit)

    def per_frame() -> None:
        g = tracked["geyser"]
        if g is None:
            return
        st = unit_state(m, g)
        if st != tracked["last"]:
            tracked["last"] = st
            tracked["timeline"].append({"frame": frame(), **st})
        gas = pl.resources()[1]
        if gas != tracked["last_gas"]:
            tracked["last_gas"] = gas
            tracked["gas"].append({"frame": frame(), "gas": gas, "amount": st["amount"]})

    s.hook(game.FRAME_END_PC, per_frame)

    sc.enter_game(s, pl)
    geysers = [u for u in pl.units(types=[T_GEYSER]) if (u["x"], u["y"]) == GEYSER_POSITION]
    if len(geysers) != 1:
        raise SystemExit(f"expected the Zerg02 record-14 geyser at {GEYSER_POSITION}, found {geysers}")
    g = geysers[0]
    tracked["geyser"] = g["ptr"]
    rec.event("start", local=pl.local, resources=list(pl.resources()), geyser=sc.unit_brief(g),
              geyser_state=unit_state(m, g["ptr"]),
              drones=[sc.unit_brief(u) for u in pl.mine([T_DRONE])])

    drones = sorted(pl.mine([T_DRONE]), key=lambda u: abs(u["x"] - g["x"]) + abs(u["y"] - g["y"]))
    points = [(g["x"] + dx, g["y"] + dy) for dy in range(-64, 33, 16) for dx in range(-96, 33, 16)]
    placed = sc.place_first_valid(pl, rec, drones, ord("B"), T_EXTRACTOR, points, "extractor")
    if placed is None:
        return rec.finish({"status": "placement failed", "hooks": hooks, "geyser_timeline": tracked["timeline"]})

    converted = pl.until(lambda: unit_state(m, g["ptr"])["type"] == T_EXTRACTOR, max_frames=1500)
    rec.event("converted" if converted else "not_converted", state=unit_state(m, g["ptr"]))
    done = converted and pl.until(lambda: unit_state(m, g["ptr"])["completed"], max_frames=max_frames)
    rec.event("extractor_completed" if done else "extractor_not_completed", state=unit_state(m, g["ptr"]))
    if done:
        workers = [u for u in pl.mine([T_DRONE])][:3]
        if workers:
            sc.gather(pl, rec, workers, pl.unit(g["ptr"]), ctrl=False)
        pl.frames(harvest_frames)
        rec.event("harvest_window_end", state=unit_state(m, g["ptr"]), resources=list(pl.resources()),
                  drone_orders=[u["order"] for u in pl.mine([T_DRONE])])
    rec.snap("end")
    return rec.finish({
        "status": "complete" if done else "incomplete",
        "warp": WARP, "cd": CD, "geyser_ptr": g["ptr"],
        "hooks": hooks, "geyser_timeline": tracked["timeline"], "gas_timeline": tracked["gas"],
        "harvest_frames": harvest_frames, "max_frames": max_frames,
    })


def main() -> int:
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(name)s %(message)s")
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT)
    parser.add_argument("--harvest-frames", type=int, default=600, help="frames to observe gas harvesting")
    parser.add_argument("--max-frames", type=int, default=2000, help="bound for construction to complete")
    args = parser.parse_args()
    res = run(args.out, args.harvest_frames, args.max_frames)
    log.info("status %s, %d frames; output %s", res.get("status"), res.get("frames"), args.out)
    return 0 if res.get("status") == "complete" else 1


if __name__ == "__main__":
    sys.exit(main())
