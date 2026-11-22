"""The glue menus' oracle of plan phase 9E (docs/phase9.md): kp_ui walks the campaign path the emulator walked.

`python -m sc_emulator menus DIR` (menus.py) records the original's screens driven by their hotkeys (the main menu,
the game mode, a new ID on the login screen, the expansion campaign menu, a ready room, the load screen, back to Exit)
with the input log (DIR/menus/input.jsonl) and the glue captures (DIR/screens). kp_oracle menus_replay opens the
title (rez\titledlg.bin, when the run starts with it) at the tick the original did, runs its loading text's blink and
the idle events of the start's load, closes it where RunGlueLoop 0x457510's DestroyTitleDialog deleted its timer,
opens the main menu at the tick the original did and follows kp_ui's glue loop (kp_ui_menus_enter, kp_ui_menus_poll; a
ready room for a mission) with the original's input and timer passes; the UI timer records, the movies, the flic
buttons' frames, the menus' CRT draws and every capture (the screen's dialog with its popups composited; the title's
palette too) must equal the original's. Exit status 1 when they differ.

The network screens of phase N (stage N3, docs/phase-network.md) take a player of the network oracle's recordings
(`python -m sc_emulator net`, koprulu-private/emu/net-n2/<game>/<player>: input.jsonl, screens/, player.json): the
replay gets Storm's providers as the original enumerated them ("provider" lines from the player's list and
standard.snp's and battle.snp's caps.dat) and follows the player through the connection, login, join and create
screens. Its map lists are the product's (kp_flow_interface_host) over a user folder made like the emulator's: the
game's map named "!" + its name in the user's maps (its BroodWar for an expansion game) for a player that has it, and
the player's preloaded files there; the install's maps are koprulu-private/full/app's, as the emulator's C:\\StarCraft.
When tools/net_check.py's goldens hold the player's game, the chat room's lobby is Koprulu's (kp_netgame.h behind the
screens' adapter kp_netui.h) over the player's recorded SNet calls, gated by the run's clock (kp_oracle_net.h): the
calls Koprulu makes must be the original's. The game the lobby starts is test_lobby_trace's (stage N4): the run
leaves its records out, and where the player saw the score screen after it, the screen opens with the original's data
(kp_ui_score_open; test_lobby_trace compares Koprulu's own data with it) and the run follows it like the menus, its
captures compared; its Ok goes back by the provider (SetGlueStateFromProvider 0x457e50).

Usage (the emulator's venv): python3 tools/menus_check.py --root DIR
       python3 tools/menus_check.py --player DIR
"""
from __future__ import annotations

import argparse
import json
import logging
import shutil
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import oracle_cli  # noqa: E402
import room_check  # noqa: E402

log = logging.getLogger("menus_check")
MAIN_MENU = "rez\\gluMain.bin"
TITLE = "rez\\titledlg.bin"
# the emulator's user maps folder (sc_emulator vfs.py DOCUMENTS_DIR + "\\Starcraft\\maps\\") and its install
USER_MAPS = "C:\\Users\\Player\\Documents\\Starcraft\\maps\\"
INSTALL = room_check.ROOT / "koprulu-private/full/app"
ROOM_STATES = (7, 8, 9)  # the glue states of the ready rooms (Terran, Zerg, Protoss)
LEFT_DOWN, LEFT_UP, LEFT_DOUBLE = 4, 5, 6  # the input events' types (kp_ui.h's KpUiEventType)
TICK_BASE_MS = 3600000  # the emulator's GetTickCount at virtual time 0 (hle/kernel32.py)
RELEASE_AFTER_MS = {"click": 210, "combo": 0}  # the release after the driver's note (inputs.py, custom.py)
# Storm's providers of 1.17 by the name the connection screen lists: id, caps flags and maximum players (the caps.dat
# entries of standard.snp and battle.snp; storm-udp.md), in SNetEnumProviders' order (CompareProviders): the screen
# inserts each at its list's head, so it lists them in reverse.
PROVIDERS = {
    "Local Area Network (UDP/IP)": (0x5544504E, 3, 256),
    "Direct Cable Connection": (0x5343424C, 0, 4),
    "Modem": (0x4D4F444D, 0, 2),
    "Local Area Network (IPX)": (0x4950584E, 3, 256),
    "Battle.net\u0099": (0x424E4554, 0, 256),
}


def score_line(path: Path, tick: int, player: str) -> list[str]:
    """The line "score DATA" of a network player's score screen whose "score" record is at tick: its segment's data
    (room_check.score_screen_data's) with the player's Storm ID (its roster entry's; else the first human's, said),
    "network" 1 and the local result ComputeGameResults gave (a draw 3 or a disconnect 4 sort as the original's; the
    glue state tells only a victory), as kp_glue_score_data takes it."""
    seg = room_check.score_segment(path, tick)
    if seg is None:
        return []
    d = seg["data"]
    me = next((e["storm"] for e in d["roster"] if e["name"] == player), None)
    if me is None:
        log.warning("%s: not in the score screen's roster, its first human's Storm ID taken", player)
    data = dict(room_check.score_screen_data(d, me), network=1)
    if "local_result" in d:
        data["result"] = d["local_result"]
    return ["score " + json.dumps(data, separators=(",", ":"))]


def menus_lines(path: Path, player: str | None = None, input_log: list[dict] | None = None) \
        -> tuple[list[str], list[dict], list[tuple[int, int]]]:
    """The records from the title's first timer on (the main menu's when the run has no title), as kp_oracle
    room_replay's lines: "title TICK" when it has one, "start TICK" the main menu's first timer; a glue state change is
    a line "glue STATE" before the record that has it. A network player's run (player: its name) leaves its game's
    records out: from the first timer set outside the glue (the game console's) to the score screen's data (uilog.py's
    "score" record), which becomes the line "score DATA" (score_line); a run that ends in the game ends at its start. With the driver's input_log, the
    releases that ran a modal loop move back to their dispatch (unnest). Also the game's span, [its first tick, the
    score's), for the captures to leave out."""
    owners, records, started, glue, title, start = {}, [], False, None, None, None
    game, gaps = None, []
    for line in path.read_text().splitlines():
        r = json.loads(line)
        if "owner" in r:
            owners[r["owner"]] = r["ref"]
            continue
        if player is not None and started and game is None and "tset" in r and "glue" not in r:
            game = r["t"]
        if game is not None:
            if "score" not in r:
                continue
            gaps.append((game, r["t"]))
            game = None
        bin_name = owners.get(r["tset"][0], [None])[0] if "tset" in r else None
        if bin_name == TITLE and title is None and start is None:
            title, started = r["t"], True
        if bin_name == MAIN_MENU and start is None:
            start, started = r["t"], True
        if started:
            records.append(r)
    if start is None:
        return [], [], []
    if input_log:
        records = unnest(records, input_log)
    lines =([f"title {title}"] if title is not None else []) + [f"start {start}"]
    glue_at = [None]

    def new_room(r: dict) -> bool:
        """The glue entering a ready room's state (a new game's briefing)."""
        before, glue_at[0] = glue_at[0], r.get("glue", glue_at[0])
        return glue_at[0] in ROOM_STATES and before not in ROOM_STATES

    for r in room_check.renumber(records, new_room):
        if "glue" in r and r["glue"] != glue:
            glue = r["glue"]
            lines.append(f"glue {glue}")
        if "score" in r and player is not None:
            lines += score_line(path, r["t"], player)
        lines += room_check.record_lines(r, owners)
    return lines, records, gaps


def action_tick(r: dict) -> int | None:
    """The tick of a record the replay acts on (an input event, a timer pass, a lobby turn), None for the others."""
    if "ev" in r or "turn" in r:
        return r["t"]
    return r.get("tpass")


def unnest(records: list[dict], input_log: list[dict]) -> list[dict]:
    """The original logs an input event when its dispatch ends (uilog.py's on_dispatch_tick), so a left release whose
    handler ran a modal loop (a confirmation popup's) follows that loop's records: a release after a release, with
    no press between. It moves back to its dispatch: the driver's release at its position (the player's input_log;
    inputs.py's click releases RELEASE_AFTER_MS after its note, custom.py's select_combo notes the release), before
    the first pass, event or turn after that release, with its tick."""
    releases = sorted((e["t"] + RELEASE_AFTER_MS[e["kind"]] + TICK_BASE_MS, e["x"], e["y"])
                      for e in input_log if e.get("kind") in RELEASE_AFTER_MS)
    out, button, k = list(records), None, 0
    while k < len(out):
        r = out[k]
        kind = r["ev"][0] if "ev" in r else None
        if kind in (LEFT_DOWN, LEFT_DOUBLE):
            button = LEFT_DOWN
        elif kind == LEFT_UP:
            if button == LEFT_UP:
                x, y = r["ev"][1], r["ev"][2]
                tick = max((t for t, rx, ry in releases if (rx, ry) == (x, y) and t <= r["t"]), default=None)
                if tick is None:
                    log.warning("an unpaired release at %d (%d, %d) without the driver's", r["t"], x, y)
                else:
                    at = next(j for j in range(k + 1) if j == k or (action_tick(out[j]) or -1) > tick)
                    out.insert(at, dict(out.pop(k), t=tick))
            button = LEFT_UP
        k += 1
    return out


def provider_lines(player: Path) -> list[str] | None:
    """The "provider ID FLAGS MAX NAME" lines of the providers the player's connection screen listed (player.json's
    "providers" event, top to bottom), in Storm's order; None when a name is not one of 1.17's."""
    events = json.loads((player / "player.json").read_text()).get("events", [])
    listed = next((e["items"] for e in events if e.get("kind") == "providers"), [])
    if any(name not in PROVIDERS for name in listed):
        return None
    return [f"provider {PROVIDERS[n][0]} {PROVIDERS[n][1]} {PROVIDERS[n][2]} {n}" for n in reversed(listed)]


def maps_lines(player: Path, pref: Path) -> list[str]:
    """The map lists' folders of a network player ("maps_pref", "maps_install" lines): pref made the emulator's user
    folder (sc_emulator net.py: the game's map preloaded as "!" + its name, the player's preloads below the maps)."""
    spec = json.loads((player.parent / "spec.json").read_text())
    me = next(p for p in spec["players"] if p["name"] == player.name)
    maps = pref / "maps"
    folder = maps / "BroodWar" if spec["expansion"] else maps
    folder.mkdir(parents=True)
    if me["has_map"]:
        source = (room_check.ROOT / "tools/research" / spec["map_path"]).resolve()
        shutil.copyfile(source, folder / ("!" + source.name))
    for guest, host in me["preload"]:
        if guest.lower().startswith(USER_MAPS.lower()):
            to = maps.joinpath(*guest[len(USER_MAPS):].split("\\"))
            to.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(host, to)
    return [f"maps_pref {pref}/", f"maps_install {INSTALL}"]


def net_lines(player: Path) -> list[str]:
    """The "net GOLDEN PLAYER" line of a network player whose game the network oracle's goldens hold
    (tools/net_check.py's tests/golden/oracle/net/<game>.json): the chat room's lobby then plays over the player's
    recorded SNet calls; none without the golden."""
    golden = room_check.ROOT / "tests/golden/oracle/net" / f"{player.parent.name}.json"
    return [f"net {golden} {player.name}"] if golden.is_file() else []


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    where = ap.add_mutually_exclusive_group(required=True)
    where.add_argument("--root", type=Path, help="the menus run's directory (DIR of menus.py)")
    where.add_argument("--player", type=Path, help="a player of the network oracle (DIR/<game>/<player>)")
    a = ap.parse_args(argv)
    logging.basicConfig(level=logging.INFO, format="%(levelname)s %(message)s")
    run = a.root / "menus" if a.root else a.player
    input_log = json.loads((a.player / "player.json").read_text()).get("input_log") if a.player else None
    lines, records, gaps = menus_lines(run / "input.jsonl", a.player.name if a.player else None, input_log)
    if not lines:
        log.error("%s: no main menu", run)
        return 1
    if a.player:
        providers = provider_lines(a.player)
        if providers is None:
            log.error("%s: a provider that is not one of 1.17's", a.player)
            return 1
    screens = (a.root or a.player) / "screens"
    with tempfile.TemporaryDirectory(prefix="menus_check-") as tmp:
        if a.player:
            lines = providers + net_lines(a.player) + maps_lines(a.player, Path(tmp)) + lines
        ok = room_check.replay("menus", oracle_cli.command("menus_replay"), lines, screens, records, "glue*.cap",
                               encoding="latin-1", gaps=gaps)
    print(f"menus_check: {'equal' if ok else 'differs'}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
