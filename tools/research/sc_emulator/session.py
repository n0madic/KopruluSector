"""A run of the emulated game: machine + coverage + observers + stop conditions."""
from __future__ import annotations

import json
import logging
import time
from pathlib import Path

from . import coverage, process
from .machine import Machine

log = logging.getLogger(__name__)

# Game globals used by observers (see the atlas for each)
GAME_MODE = 0x6662B8      # 4 = glue, 3 = in game, ... (P22 0x4679b0)
GLUE_STATE = 0x66A274     # glue state machine (P19)
MAIN_MENU_FN = 0x45A8B0   # glue main menu handler (P19 state 0)
OUT_DIR = (Path.home() / "koprulu-private/emu")


class Session:
    def __init__(self, cfg: process.Config | None = None):
        self.cfg = cfg or process.Config()
        self.m: Machine = process.build(self.cfg)
        self.cov = self.m.native
        self.hits: dict[int, int] = {}
        self.t0 = time.time()
        self.files: list[str] = []
        storm = self.m.images["storm.dll"]
        self.hook(storm.export(268), self._on_sfile_open)  # SFileOpenFileEx(archive, name, scope, out)

    def _on_sfile_open(self):
        name = self.m.cstr(self.m.r32(self.m.esp + 8)).decode("latin-1")
        self.files.append(name)

    def hook(self, addr: int, fn):
        """Python code hook at one guest address (called before the instruction executes)."""
        self.m.add_hook(addr, fn)

    def start(self):
        process.start_program(self.m)

    def run_until(self, predicate, max_seconds: float):
        t0 = time.time()
        reason = None

        def stop():
            nonlocal reason
            if predicate():
                reason = "predicate"
                return True
            if time.time() - t0 > max_seconds:
                reason = "time limit"
                return True
            return False
        self.m.run(stop=stop)
        return reason or "stopped"

    def coverage_summary(self, atlas=None) -> dict:
        if self.cov is None:
            return {}
        atlas = atlas or coverage.load_atlas()
        starts = set(self.cov.block_starts("exe"))
        s = coverage.summarize(starts, atlas)
        storm_blocks = self.cov.block_starts("storm")
        storm = self.m.images["storm.dll"]
        exports = {a: o for o, a in storm.exports_by_ordinal.items()}
        s["storm_blocks"] = len(storm_blocks)
        s["storm_exports_executed"] = sorted(exports[a] for a in storm_blocks if a in exports)
        s["blocks_executed_total"] = self.cov.blocks_executed()
        return s

    def executed_functions(self, atlas) -> list[int]:
        starts = set(self.cov.block_starts("exe"))
        return sorted(a for a in atlas if a in starts)


def write_json(path: Path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=1, sort_keys=True))
