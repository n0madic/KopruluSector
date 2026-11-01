"""The ready rooms' and result dialogs' oracle of plan phase 9E (docs/phase9.md): kp_ui runs each room and each victory
or defeat dialog of the emulator's campaign chains.

A chain recorded with the input log (`python -m sc_emulator chain NAME WARP --oracle DIR --glue-ms ... --ready-ms ...`,
DIR/<chain>/<k>-m<mission>/input.jsonl) holds each ready room the original ran: its dialog's UI timers (set, deleted,
fired, with the dialogs and controls that own them), the briefing VM's actions (`brief`), the movies begun (`svid`),
the menus' CRT states (`rand`) and the input events. kp_oracle room_replay opens the same room over the game's
mission at the tick the original did, runs kp_ui's timers at the original's passes and its input at the original's
events, and reports the first records where kp_ui differs. kp_oracle result_replay opens the game's result dialog
at the tick the original did (its timers, input, the exit code its answer left, and its pixels in the modeless
captures blended over their game view); kp_oracle score_replay opens the score screen over the data the original's
read (its timers, input and captures). Exit status 1 when a game differs.

Usage (the emulator's venv: the captures' reader): python3 tools/room_check.py --root DIR [--only CHAIN/k-mNN ...]
       [--jobs N] [--kind room|result|score ...]
"""
from __future__ import annotations

import argparse
import json
import logging
import re
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "tools" / "research"))  # sc_emulator, imported where captures are read (unicorn)
import oracle_cli  # noqa: E402
log = logging.getLogger("room_check")

ROOMS = {7: 1, 8: 0, 9: 2}  # glue state -> race (terran, zerg, protoss)
ROOM_BINS = ("rez\\glurdyt.bin", "rez\\glurdyz.bin", "rez\\glurdyp.bin")
RESULT_BINS = {"rez\\lmission.bin": 2, "rez\\wmission.bin": 3}
SCORE_STATES = range(14, 20)  # the score screens: zerg, terran, protoss, each defeat then victory
EXIT_CODES = {2: 7, 3: 6}  # RequestGameExit 0x444e00 after the defeat's and the victory's answer
GAME_DIR = re.compile(r"\d+-m(\d+)$")


def ref(owners: dict, n) -> str:
    r = owners.get(n)
    return f"{r[0].lower()}#{r[1]}" if isinstance(r, list) else f"?{n}"


def renumber(records: list[dict], new_room=None) -> list[dict]:
    """The briefing records' and the flic clips' numbers as kp_ui counts them, from 0 in the segment's first-use
    order (uilog.py counts them over the whole chain); new_room(record) (optional) starts the briefing records' count
    again (a ready room's game)."""
    maps = {"brief": {}, "flc": {}}
    out = []
    for r in records:
        if new_room is not None and new_room(r):
            maps["brief"] = {}
        r = renumber_one(r, maps)
        if "during" in r:
            r = dict(r, during=[[j, renumber_one(d, maps)] for j, d in r["during"]])
        out.append(r)
    return out


def renumber_one(r: dict, maps: dict) -> dict:
    """A record with its briefing record or clip number from the maps (a new one numbered next)."""
    for key, m in maps.items():
        if key in r:
            r = dict(r)
            r[key] = [m.setdefault(r[key][0], len(m))] + list(r[key][1:])
    return r


def record_lines(r: dict, owners: dict) -> list[str]:
    """A record of the input log as kp_oracle room_replay's lines (an owner as BIN#record)."""
    if "tset" in r:
        o, i, period, cb = r["tset"]
        return [f"set {ref(owners, o)} {i} {period} {cb} @{r['t']}"]
    if "tdel" in r:
        return [f"del {ref(owners, r['tdel'][0])} {r['tdel'][1]}"]
    if "tdelall" in r:
        return [f"delall {ref(owners, r['tdelall'])}"]
    if "brief" in r:
        return ["brief " + " ".join(map(str, r["brief"]))]
    if "svid" in r:
        return ["svid " + r["svid"].lower()]
    if "flc" in r or "bpc" in r:  # the clock SmackWait compared, else the tick after the continuation
        kind = "flc" if "flc" in r else "bpc"
        tick = r.get("wait", r.get("t"))
        return [f"{kind} " + " ".join(map(str, r[kind])) + (f" @{tick}" if tick is not None else "")]
    if "credits" in r:
        return [f"credits {r['t']} {r['credits']}"]
    if "page" in r:
        return [f"page {r['t']} {r.get('deadline', 0)}"]
    if "turn" in r:
        return [f"turn {r['t']}"]
    if "rand" in r:
        return [f"rand {r['rand']}"]
    if "progress" in r:  # the profile's progress the episode's start read (menus.py)
        return ["progress " + " ".join(map(str, r["progress"]))]
    if "ev" in r:
        e = r["ev"] + [0, 0]
        return [f"ev {r['t']} {e[0]} {e[1]} {e[2]}"]
    if "tpass" in r:
        out = [f"pass {r['tpass']}"]
        during = r.get("during", [])
        for k, (o, i) in enumerate(r["fired"]):
            out.append(f"fire {ref(owners, o)} {i}")
            for j, d in during:
                if j == k:
                    out += record_lines(d, owners)
        return out
    return []


def room_segment(path: Path) -> dict | None:
    """The records of the game's first ready room, from its establishing text (when it has one) or the room dialog's
    first timer to the room's end; `room` is the tick the room's dialog set its first timer."""
    owners, records, race, started, room = {}, [], None, False, None
    for line in path.read_text().splitlines():
        r = json.loads(line)
        if "owner" in r:
            owners[r["owner"]] = r["ref"]
            continue
        glue = r.get("glue")
        is_room = glue in ROOMS and "tset" in r and owners.get(r["tset"][0], [None])[0] in ROOM_BINS
        if not started:
            if glue in ROOMS and ("credits" in r or is_room):
                started, race = True, ROOMS[glue]
            else:
                continue
        if glue not in ROOMS:
            break
        if is_room and room is None:
            room = r["t"]
        records.append(r)
    if not started or room is None:
        return None
    return {"race": race, "owners": {str(k): v for k, v in owners.items()}, "records": records, "room": room}


def result_segment(path: Path) -> dict | None:
    """The records of the game's result dialog: from the dialog's first timer to the game's end (its first record
    outside the game); `result` is 2 for rez\\lmission.bin, 3 for rez\\wmission.bin."""
    owners, records, result = {}, [], None
    for line in path.read_text().splitlines():
        r = json.loads(line)
        if "owner" in r:
            owners[r["owner"]] = r["ref"]
            continue
        if result is None:
            if "tset" in r and "glue" not in r:
                bin_name = owners.get(r["tset"][0], [None])[0]
                if bin_name in RESULT_BINS:
                    result = RESULT_BINS[bin_name]
            if result is None:
                continue
        if "glue" in r:
            break
        records.append(r)
    if result is None:
        return None
    return {"result": result, "owners": owners, "records": records}


def score_segment(path: Path) -> dict | None:
    """The records of the game's score screen: from RunScoreScreen's "score" record to the first record outside the
    score states (14..19), with the screen's data (uilog.py's "score" and "elapsed" records)."""
    owners, records, data = {}, [], None
    for line in path.read_text().splitlines():
        r = json.loads(line)
        if "owner" in r:
            owners[r["owner"]] = r["ref"]
            continue
        if data is None:
            if "score" in r:
                data = dict(r["score"], t=r["t"])
            continue
        if "elapsed" in r:
            data["elapsed"] = r["elapsed"]
        if records and r.get("glue") not in SCORE_STATES:
            break
        if "glue" in r and r["glue"] in SCORE_STATES:
            data.setdefault("glue_state", r["glue"])
        records.append(r)
    if data is None or "glue_state" not in data:
        return None
    return {"data": data, "owners": owners, "records": records}


def captures(screens: Path, start: int, end: int, out: Path, pattern: str = "glue0[789]-*.cap") -> list[str]:
    """The captures (uiscreen.py) taken in [start, end], decompressed into out: `cap TICK PATH NAME` and the cursor's
    rectangle, for a modeless capture also the result dialog's rectangle and the game view under it."""
    from sc_emulator.uiscreen import ScreenCapture  # noqa: E402
    from sc_emulator.hle.kernel32 import TICK_BASE_MS  # noqa: E402
    lines = []
    for path in sorted(screens.glob(pattern)):
        cap = ScreenCapture(path)
        tick = cap.header["clock_ms"] + TICK_BASE_MS
        if not start <= tick <= end:
            continue
        raw = out / (path.stem + ".raw")
        raw.write_bytes(cap.pixels)
        (out / (path.stem + ".pal")).write_bytes(cap.palette)  # the screen's 256 entries, read beside the indices
        x, y, w, h = cap.header["cursor"]["rect"]  # the cursor's rectangle, drawn into the surface
        line = f"cap {tick} {raw} {path.name} {x} {y} {w} {h}"
        dialog = next((d for d in cap.header.get("dialogs", []) if isinstance(d["ref"], list)
                       and d["ref"][0] in RESULT_BINS), None)
        if dialog is not None and cap.under is not None:
            under = out / (path.stem + ".under.raw")
            under.write_bytes(cap.under)
            line += " " + " ".join(map(str, dialog["rect"])) + f" {under}"
        lines.append(line)
    return lines


def mission_kpm(mission: int) -> str | None:
    lines = (ROOT / "generated" / "missions" / "index.txt").read_text().splitlines()
    if not 1 <= mission <= len(lines):
        return None
    return f"generated/missions/{lines[mission - 1].split()[0]}.kpm"


def replay(name: str, tool: list[str], lines: list[str], screens: Path, records: list[dict], pattern: str) -> bool:
    """Runs a replay tool (oracle_cli.command) over the lines and the captures of the records' time; logs its
    output."""
    with tempfile.TemporaryDirectory() as tmp:
        ticks = [r.get("t", r.get("tpass", 0)) for r in records if "t" in r or "tpass" in r]
        if ticks:
            lines = lines + captures(screens, min(ticks), max(ticks), Path(tmp), pattern)
        data = Path(tmp) / "records.txt"
        data.write_text("\n".join(lines) + "\n")
        r = subprocess.run(tool + [str(data)], cwd=ROOT, capture_output=True, text=True)
    for line in (r.stdout + r.stderr).splitlines():
        (log.info if r.returncode == 0 else log.error)("%s: %s", name, line)
    return r.returncode == 0


def game_exit(game: Path) -> int:
    """The exit code the chain recorded for the game (the game_exit event of its mission in chain.json, written when
    the chain ends), -1 when unknown."""
    chain = game.parent.parent.parent / "chains" / game.parent.name / "chain.json"
    m = GAME_DIR.search(game.name)
    if not chain.exists() or m is None:
        return -1
    k, mission = int(game.name.split("-")[0]), int(m.group(1))
    missions = json.loads(chain.read_text()).get("missions", [])
    entry = missions[k - 1] if 1 <= k <= len(missions) and missions[k - 1].get("mission") == mission else \
        next((e for e in missions if e.get("mission") == mission), None)
    exits = [e.get("arg") for e in (entry or {}).get("events", []) if e.get("kind") == "game_exit"]
    return exits[0] if exits else -1


def check_game(game: Path, expansion: int, kinds: tuple[str, ...]) -> bool:
    m = GAME_DIR.search(game.name)
    log_path = game / "input.jsonl"
    if m is None or not log_path.exists():
        return True
    kpm = mission_kpm(int(m.group(1)))
    if kpm is None:
        log.warning("%s: no catalog mission %s", game, m.group(1))
        return True
    name, ok = f"{game.parent.name}/{game.name}", True
    head = [f"kpm {kpm}", f"expansion {expansion}", f"mission {int(m.group(1))}"]
    seg = room_segment(log_path) if "room" in kinds else None
    if seg is not None:
        owners = {int(k): v for k, v in seg["owners"].items()}
        lines = head + [f"race {seg['race']}", f"start {seg['records'][0].get('t', 0)}", f"room {seg['room']}"]
        for r in renumber(seg["records"]):
            lines += record_lines(r, owners)
        ok &= replay(name, oracle_cli.command("room_replay"), lines, game.parent / "screens", seg["records"],
                     "glue0[789]-*.cap")
    elif "room" in kinds:
        log.info("%s: no ready room", name)
    res = result_segment(log_path) if "result" in kinds else None
    if res is not None:
        start = next(r["t"] for r in res["records"] if "tset" in r)
        lines = head + [f"start {start}", f"result {res['result']}", f"exit {game_exit(game)}"]
        for r in res["records"]:
            lines += record_lines(r, res["owners"])
        ok &= replay(name, oracle_cli.command("result_replay"), lines, game.parent / "screens", res["records"],
                     "modeless-*.cap")
    elif "result" in kinds:
        log.info("%s: no result dialog", name)
    score = score_segment(log_path) if "score" in kinds else None
    if score is not None:
        d, state = score["data"], score["data"]["glue_state"]
        human = next((e for e in d["roster"] if e["type"] in (2, 10)), {"storm": 0})
        data = dict(d, result=1 if state % 2 else 2, race=(state - 14) // 2, local_storm=human["storm"])
        if d["player_colours"][:2] == [0, 0] and d["player_colours"][4] == 1:  # an older log's bytes of the u32s
            data["player_colours"] = list(range(8))  # TODO: drop with the logs before this fix
        lines = head + [f"start {d['t']}", "data " + json.dumps(data, separators=(",", ":"))]  # the score data
        for r in score["records"]:
            lines += record_lines(r, score["owners"])
        ok &= replay(name, oracle_cli.command("score_replay"), lines, game.parent / "screens", score["records"],
                     "glue1[4-9]-*.cap")
    elif "score" in kinds:
        log.info("%s: no score screen", name)
    return ok


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--root", required=True, type=Path, help="the chains' oracle directory (DIR of --oracle)")
    ap.add_argument("--only", action="append", default=[], help="CHAIN or CHAIN/<k>-m<NN>")
    ap.add_argument("--jobs", type=int, default=1)
    ap.add_argument("--kind", action="append", choices=("room", "result", "score"), help="the checks (default: all)")
    a = ap.parse_args(argv)
    logging.basicConfig(level=logging.INFO, format="%(levelname)s %(message)s")
    games = []
    for chain in sorted(p for p in a.root.iterdir() if p.is_dir()):
        for game in sorted(p for p in chain.iterdir() if p.is_dir() and GAME_DIR.search(p.name)):
            key = f"{chain.name}/{game.name}"
            if a.only and not any(key == o or chain.name == o for o in a.only):
                continue
            games.append((game, 1 if chain.name.startswith("exp") else 0))
    with ThreadPoolExecutor(max_workers=max(1, a.jobs)) as pool:
        kinds = tuple(a.kind or ("room", "result", "score"))
        failed = sum(not ok for ok in pool.map(lambda g: check_game(g[0], g[1], kinds), games))
    print(f"room_check: {len(games)} games, {failed} differ")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
