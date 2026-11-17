#!/usr/bin/env python3
"""Writes docs/compatibility.md, the campaign regression matrix: one row per campaign row of the six
campaigns (the manifest defs/campaign), its checks filled from the goldens that hold them.

Columns:
- load: the mission's scenario read from the install, with its resource packs recorded (defs/campaign/packs-<profile>.json);
- initial state: tests/golden/oracle/initial_state (the start equal to the original's);
- run: tests/golden/oracle/runs (2,000 idle steps equal: triggers, AI, economy at every 20th step);
- chain: tests/golden/oracle/chains (the game won with a cheat in its campaign chain, started from the game before,
  and the campaign's next mission);
- play: tests/golden/oracle/plays (a scripted win through the mission's own objectives, equal at every 20th step,
  and its save continuations);
- defeat: the lost game of a chain (the retry);
- status: the short status: ✅ a scripted win equal to the original's, 🟡 the mission plays with what the
  other checks show, ❌ not converted. The manual playthrough and the briefings belong to the interface of phase 9.

After the campaigns, the custom games (phase M): one row per game of the original's Play Custom screen in
tests/golden/oracle/custom, its map, the game type its lobby chose, the steps its golden holds and its AI trace.

Usage: python3 tools/compatibility.py [--check]   (the missions' names come from the 1.17 install's catalog,
KOPRULU_ORIGINAL: without it --check is skipped and nothing is written)
"""
from __future__ import annotations

import argparse
import json
import logging
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools" / "research"))
import mission_index  # noqa: E402
from scdata.missions import load_packs  # noqa: E402

PROFILE = "bw-1.17.0.1"
GOLDEN = ROOT / "tests" / "golden" / "oracle"
OUT = ROOT / "docs" / "compatibility.md"
MANIFEST = ROOT / "defs" / "campaign" / "bw-1.17.0.1.json"
RACE_RUN = {("terran", 0): "terran", ("zerg", 0): "zerg", ("protoss", 0): "protoss",
            ("terran", 1): "xterran", ("zerg", 1): "xzerg", ("protoss", 1): "xprotoss"}
TABLE_RACE = {"zerg": "zerg", "terran": "terran", "protoss": "protoss", "expzerg": "zerg", "expterran": "terran",
              "expprotoss": "protoss"}
EPISODES = (("terran", "Terran (Episode I)"), ("zerg", "Zerg (Episode II)"), ("protoss", "Protoss (Episode III)"),
            ("expprotoss", "Protoss (Brood War, Episode IV)"), ("expterran", "Terran (Brood War, Episode V)"),
            ("expzerg", "Zerg (Brood War, Episode VI)"))
PLAY_MISSION = re.compile(r"^missions/(.+)$")  # a play golden's mission name (`terran/terran01`)
log = logging.getLogger("compatibility")


def index_names(kind: str) -> set[str]:
    """The names of a golden index (its JSON twin, tools/state_check.py)."""
    path = GOLDEN / kind / "index.json"
    return set(json.loads(path.read_text())) if path.exists() else set()


GAME_TYPE_LIST = "17"  # the Play Custom screen's game type combo box (its control id in the lobby's record)
SUBTYPE_LIST = "18"


def custom_rows() -> list[str]:
    """The custom games' table (tests/golden/oracle/custom), one row per game of the original's Play Custom screen."""
    rows = []
    for name in sorted(index_names("custom")):
        g = json.loads((GOLDEN / "custom" / f"{name}.json").read_text())
        screen = g.get("lobby", {}).get("screen", {})
        kinds = []
        for control in (GAME_TYPE_LIST, SUBTYPE_LIST):
            box = screen.get(control, {})
            items, sel = box.get("items", []), box.get("selected", 255)
            if box.get("usable", control == GAME_TYPE_LIST) and 0 <= sel < len(items):
                kinds.append(items[sel])
        steps = sorted(int(k) for k in g["steps"])
        traced = bool(g.get("ai_trace", {}).get("count"))
        if "ai_patch" in g:
            kinds.append("our own AI code")
        rows.append(f"| `{name}` | `{g['mission'].removeprefix('maps/')}` | {', '.join(kinds) or '?'} | "
                    f"{len(steps)} of {steps[0]}–{steps[-1]} | {'✓' if traced else '—'} |")
    return rows


def build(data: Path) -> str:
    manifest = json.loads(MANIFEST.read_text())
    names = {m.index: m.name for m in mission_index.catalog(data)}
    packs = {d.lower() for d in load_packs(PROFILE)}
    initial, runs, chains, plays = (index_names(k) for k in ("initial_state", "runs", "chains", "plays"))
    chain_by_mission: dict[int, str] = {}
    defeat_by_mission: dict[int, str] = {}
    for c in sorted(chains):
        m = int(c.rsplit("-m", 1)[1])
        (defeat_by_mission if c.startswith("defeat-") else chain_by_mission).setdefault(m, c)
    play_by_mission: dict[int, list[str]] = {}
    index_of = {n: k for k, n in names.items()}
    for p in sorted(plays):  # by the mission the play's golden names (a play's name need not carry it)
        m = PLAY_MISSION.match(json.loads((GOLDEN / "plays" / f"{p}.json").read_text())["mission"])
        if m and m.group(1) in index_of:
            play_by_mission.setdefault(index_of[m.group(1)], []).append(p)
    tables = {t["name"]: t for t in manifest["tables"]}
    lines = ["# Campaign compatibility", "",
             "Generated by `tools/compatibility.py` from the goldens. ✓ = the golden holds it and the "
             "tests check it on every runtime; — = not yet. Status: ✅ a scripted win equal to the original's "
             "along the whole play (with its transition and save continuations), 🟡 the mission plays as far as the "
             "other checks show, ❌ not converted. Briefings, scripted events seen by a player and the manual "
             "playthrough on each platform come with the interface of phase 9.", ""]
    total = {"rows": 0, "won": 0}
    for name, title in EPISODES:
        t = tables[name]
        lines += [f"## {title}", "", "| Row | Mission | Hidden | Load | Initial state | Idle run | Chain | Scripted win | "
                  "Defeat | Status |", "|---|---|---|---|---|---|---|---|---|---|"]
        k = 0
        for label, mission, cinematic, race, hidden in t["rows"]:
            if mission == 0 or cinematic != 0:
                continue
            k += 1  # the playable rows in table order
            mname = names.get(mission, "?")
            run = f"{RACE_RUN[(TABLE_RACE[name], t['expansion'])]}-m{mission:02d}"
            loaded = "campaign\\" + mname.replace("/", "\\") in packs
            won = bool(play_by_mission.get(mission))
            total["rows"] += 1
            total["won"] += won
            status = "❌" if not loaded else "✅" if won else "🟡"
            mark = lambda b: "✓" if b else "—"  # noqa: E731
            lines.append(f"| {k} | `{mname}` | {'yes' if hidden else ''} | {mark(loaded)} | {mark(run in initial)} | "
                         f"{mark(run in runs)} | {mark(mission in chain_by_mission)} | "
                         f"{', '.join(f'`{p}`' for p in play_by_mission.get(mission, [])) or '—'} | "
                         f"{mark(mission in defeat_by_mission)} | {status} |")
        lines.append("")
    lines += [f"Scripted wins: {total['won']} of {total['rows']} playable rows.", ""]
    custom = custom_rows()
    if custom:
        lines += ["## Custom games (phase M)", "",
                  "The games of the original's Play Custom screen against computers ([phase M](phase-multiplayer.md)), "
                  "each started from its lobby's setup on the install's map and equal to the original's at every step "
                  "its golden holds, with the computers' AI trace (`test_scenarios`); `test_seats` plays each again "
                  "with every other player as a seat. Their Play Custom screens at Ok equal the original's pixel for "
                  "pixel outside the cursor, and their lists and lobby records equal its records "
                  "(`test_flow.play_custom_screens_are_the_originals`). The games of our own AI code run a probe "
                  "of the melee opcodes no stock script reaches beside each computer's melee script "
                  "([phase M](phase-multiplayer.md#the-melee-opcodes-against-the-original-2026-11-17)).", "",
                  "| Game | Map | Game type | Steps equal | AI trace |", "|---|---|---|---|---|", *custom, ""]
    return "\n".join(lines)


def main() -> int:
    logging.basicConfig(level=logging.INFO, format="%(message)s")
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--check", action="store_true", help="fail if docs/compatibility.md is out of date")
    a = ap.parse_args()
    data = mission_index.install()
    if data is None:
        if a.check:
            log.info("no 1.17 install (KOPRULU_ORIGINAL) to read the catalog from, skipped")
            return 0
        log.error("no 1.17 install: name it with KOPRULU_ORIGINAL")
        return 1
    text = build(data)
    if a.check:
        if not OUT.exists() or OUT.read_text() != text:
            log.error("out of date: %s", OUT.relative_to(ROOT))
            return 1
        return 0
    OUT.write_text(text)
    log.info("wrote %s", OUT.relative_to(ROOT))
    return 0


if __name__ == "__main__":
    sys.exit(main())
