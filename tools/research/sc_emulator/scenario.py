"""Test scenarios in the full game (phase 3 acceptance level C, docs/phase3.md).

A scenario spec (tests/scenarios/<name>.json) names a base campaign mission. The game is
started through that mission's normal start path; at every call of ChkLoadSections 0x4c1200
(the header, version and section loads) the map buffer is replaced by the scenario CHK that
converter/scenarios.py builds from the same base map, so the original plays exactly the map
Koprulu converts. Then:
- every executed game command is logged at RecordCommand 0x478830 with the frame counter and
  the issuing player (commands.jsonl), so Koprulu can replay the same command stream;
- the state is dumped at the entry of object steps 1..frames+1 (sc_emulator/state.py);
- inputs of the spec (at given frame counter values) are sent as window messages.

Usage (from tools/research, private venv):
  python -m sc_emulator scenario ../../tests/scenarios/idle_marines.json [--out-root DIR]
"""
from __future__ import annotations

import json
import logging
import sys
from pathlib import Path

from . import campaigns, game, process, state
from .mission import run_mission

log = logging.getLogger(__name__)

REPO = Path(__file__).resolve().parents[3]
OUT_ROOT = Path("~/koprulu-private/emu/scenarios")
CHK_LOAD_SECTIONS = 0x4C1200  # ECX = CHK bytes, EDX = length
RECORD_COMMAND = 0x478830     # ECX = command bytes, EDX = length
COMMAND_PLAYER = 0x508D44     # issuing player's slot while commands execute


def build_chk(spec: dict) -> bytes:
    if str(REPO) not in sys.path:
        sys.path.insert(0, str(REPO))
    from converter import scenarios
    from converter.mpq.profile import Resolver

    with Resolver(process.DEFAULT_EXE.parent, full_hash=False) as resolver:
        return scenarios.scenario_chk(scenarios.base_chk(resolver, spec), spec)


def mission_spec(spec: dict):
    """The campaign row whose map is the scenario's base (generated/missions/index.txt order)."""
    names = (REPO / "generated" / "missions" / "index.txt").read_text().split("\n")
    index = 1 + next(i for i, line in enumerate(names) if line.split(" ")[0] == spec["base"])
    return next(m for m in campaigns.missions(process.DEFAULT_EXE) if m.mapdata_index == index)


def install(session, spec: dict, chk: bytes, out: Path) -> dict:
    m = session.m
    buffer = m.alloc(len(chk))
    m.write(buffer, chk)
    log_file = open(out / "commands.jsonl", "w")
    st = {"substituted": 0, "commands": 0}

    def on_chk_load():
        from unicorn import x86_const as X
        m.set_reg(X.UC_X86_REG_ECX, buffer)
        m.set_reg(X.UC_X86_REG_EDX, len(chk))
        st["substituted"] += 1

    def on_command():
        from unicorn import x86_const as X
        data = m.read(m.reg(X.UC_X86_REG_ECX), m.reg(X.UC_X86_REG_EDX))
        log_file.write(json.dumps({"frame": m.r32(game.FRAME_COUNTER), "player": m.read(COMMAND_PLAYER, 1)[0],
                                   "bytes": data.hex()}) + "\n")
        log_file.flush()
        st["commands"] += 1

    session.hook(CHK_LOAD_SECTIONS, on_chk_load)
    session.hook(RECORD_COMMAND, on_command)
    state.install(session, tuple(range(1, spec["frames"] + 2)), out / "states")
    if spec.get("inputs"):
        raise NotImplementedError("scenario inputs (sc_emulator/play.py) are added with the order scenarios")
    return st


def run(spec_path: Path, out_root: Path = OUT_ROOT, calltrace: tuple[int, int] | None = None):
    spec = json.loads(Path(spec_path).read_text())
    spec.setdefault("name", Path(spec_path).stem)
    chk = build_chk(spec)
    base = mission_spec(spec)
    base.name = spec["name"]
    st = {}

    def setup(session, out):
        st["run"] = install(session, spec, chk, out)
        if calltrace:
            from . import calltrace as ct
            ct.install(session, calltrace[0], calltrace[1], out / "calltrace")

    result = run_mission(base, frames=spec["frames"] + 1, png_frames=(), out_root=out_root, setup=setup)
    log.info("%s: %s, %d frames, CHK substituted %d times", spec["name"], result.status, result.frames,
             st["run"]["substituted"] if "run" in st else 0)
    return result
