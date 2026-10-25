"""The glue's menus driven by input (plan phase 9E E4): the campaign path through the original's screens.

From a normal start (no warp): the title, the main menu, Single Player, the expansion's game mode (Expansion), the
login screen (a new ID typed into its popup), the expansion campaign menu, a race's episode (its first ready room),
Cancel back to the campaign menu, the episode again with a profile that has progress (the mission history popup: its
third mission's ready room), Cancel, Load Saved and Cancel, Cancel back through the login screen to the main menu, Exit.
The progress is the profile's as StartCampaignEpisode 0x4bb5b0 has read it (its stack copy, PROGRESS_AT), set by a
hook and recorded in the input log ("progress": [field, value]); the profile's file keeps its own.
Each step waits until its screen (the glue state with its topmost dialog) has stood `settle_ms` virtual milliseconds,
so that the glue captures (uiscreen.py, `glue_ms`) see every screen after its animations. The input log, the UI timer
records and the captures are written like a chain's (scenario.install_logs, uiscreen.install): <out>/menus/input.jsonl,
<out>/screens/.
"""
from __future__ import annotations

import logging
import time
from pathlib import Path

from . import game, process
from .inputs import VK_RETURN, Input
from .machine import EmulationStop
from .session import Session, write_json

log = logging.getLogger(__name__)
DIALOG_LIST = 0x6CE6D8
PROFILE_NAME = "Koprulu"
PROGRESS_AT = 0x4BB61C  # StartCampaignEpisode after the profile's load: [ebp-0x48] original, [ebp-0x3c] expansion
EXPANSION = 0x536720
VK_DOWN = 0x28
HISTORY_PROGRESS = 35   # the expansion's Protoss rows up to mission 35: four rows in the history
# (expected glue state, its topmost dialog's BIN, keys (a string of hotkeys, a tuple of virtual keys) or None for the
# new ID's text, description, the progress StartCampaignEpisode reads or None)
STEPS = (
    (0, "rez\\gluMain.bin", "S", "main menu: Single Player"),
    (0, "rez\\gluGameMode.bin", "E", "game mode: Expansion"),
    (5, "rez\\gluPEdit.bin", None, "login: the new ID popup"),
    (5, "rez\\gluLogin.bin", "O", "login: Ok"),
    (22, "rez\\gluExpCmpgn.bin", "P", "expansion campaign: Protoss, Episode IV"),
    (9, "rez\\glurdyp.bin", "C", "ready room: Cancel"),
    (22, "rez\\gluExpCmpgn.bin", "P", "expansion campaign: Protoss with progress", HISTORY_PROGRESS),
    (22, "rez\\gluHist.bin", (VK_DOWN, VK_DOWN, ord("O")), "history: the third mission"),
    (9, "rez\\glurdyp.bin", "C", "ready room: Cancel"),
    (22, "rez\\gluExpCmpgn.bin", "L", "expansion campaign: Load Saved"),
    (13, "rez\\gluLoad.bin", "C", "load: Cancel"),
    (22, "rez\\gluExpCmpgn.bin", "C", "expansion campaign: Cancel"),
    (5, "rez\\gluLogin.bin", "C", "login: Cancel"),
    (0, "rez\\gluMain.bin", "X", "main menu: Exit"),
)


def run_menus(out: Path, cd: str = "expansion", settle_ms: int = 3500, glue_ms=(3000,), max_wall: float = 3600) -> dict:
    out.mkdir(parents=True, exist_ok=True)
    s = Session(process.Config(command_line='"C:\\StarCraft\\StarCraft.exe"', cd=cd))
    m = s.m
    inp = Input(s)
    from . import scenario, uiscreen
    logs = scenario.install_logs(s, out / "menus")
    uiscreen.install(s, out / "screens", (), glue_ms, names=logs["uilog"]["dialog_ref"])
    s.start()
    t0 = time.time()
    visited: list[list] = []

    def screen():
        return m.r16(game.GAME_MODE), m.r32(game.GLUE_STATE), m.r32(DIALOG_LIST)

    def top_bin():
        ref = logs["uilog"]["dialog_ref"](m.r32(DIALOG_LIST))
        return ref[0].lower() if isinstance(ref, list) else None

    def settle(want_state, want_bin):
        """Waits until the screen (glue state want_state, its topmost dialog of want_bin) stood settle_ms."""
        last, since = None, m.clock_us
        while time.time() - t0 < max_wall:
            inp.wait(100)
            cur = screen()
            if cur != last:
                last, since = cur, m.clock_us
                visited.append([m.clock_us // 1000, *cur[:2], top_bin()])
                continue
            if m.clock_us - since >= settle_ms * 1000 and cur[0] == 4 and cur[1] == want_state \
                    and top_bin() == want_bin.lower():
                return True
        return False

    progress = [None]

    def on_progress():
        from unicorn import x86_const as X
        if progress[0] is None:
            return
        race, expansion = m.reg(X.UC_X86_REG_EDI), m.read(EXPANSION, 1)[0] != 0
        m.w32(m.reg(X.UC_X86_REG_EBP) - (0x3C if expansion else 0x48) + 4 * race, progress[0])
        logs["uilog"]["record"]({"progress": [3 * expansion + race, progress[0]]})
        progress[0] = None

    s.hook(PROGRESS_AT, on_progress)
    done, exited = [], None
    try:
        for state, bin_name, keys, what, *rest in STEPS:
            if not settle(state, bin_name):
                log.warning("menus: %s: the screen did not come (at %s, %s)", what, screen(), top_bin())
                break
            log.info("menus: %s", what)
            progress[0] = rest[0] if rest else None
            if keys is None:  # the login screen of a fresh profile opens the new ID popup: type the name
                inp.text(PROFILE_NAME)
                inp.key(VK_RETURN)
            else:
                for k, key in enumerate(keys):
                    if k:
                        inp.wait(300)
                    inp.key(ord(key) if isinstance(key, str) else key)
            done.append(what)
        inp.wait(2000)
    except EmulationStop as e:  # Exit ends the process
        exited = str(e)
    result = {"steps": done, "exited": exited, "visited": visited, "virtual_ms": m.clock_us // 1000, "wall_seconds": time.time() - t0}
    write_json(out / "menus.json", result)
    return result
