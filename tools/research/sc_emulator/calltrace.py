"""Native call traces of a window of the game (phase 3 porting aid, docs/phase3.md).

Between an execution of `start` and the next execution of `stop` (code addresses), every
entry of an atlas function in StarCraft.exe is recorded with the stack pointer at entry;
`windows` such windows are recorded in a row, each headed by a `# window <k>` line. Nesting is recovered from the stack pointers: a call is a child of the closest
earlier open call whose entry ESP is higher. The block hook only exists inside a window, so
the rest of the run keeps its speed; the translation cache is flushed when the hook is added,
because Unicorn does not apply a new block hook to code it has already translated.

Output: <out>/calltrace.txt, one line per entry, indented by depth:
`<depth spaces><address> <atlas name>`; and calltrace_counts.json (address -> calls).
"""
from __future__ import annotations

import json
import logging
from pathlib import Path

from unicorn import UC_HOOK_BLOCK

from . import coverage

log = logging.getLogger(__name__)

TEXT_BASE, TEXT_END = 0x401000, 0x4EB000


def install(session, start: int, stop: int, out: Path, windows: int = 1) -> dict:
    m = session.m
    atlas = coverage.load_atlas()
    functions = set(atlas)
    out.mkdir(parents=True, exist_ok=True)
    state = {"windows": [], "hook": None}

    def on_block(uc, address, size, _):
        if address in functions:
            state["windows"][-1].append((address, m.esp))

    def on_start():
        if state["hook"] is None and len(state["windows"]) < windows:
            state["windows"].append([])
            state["hook"] = m.uc.hook_add(UC_HOOK_BLOCK, on_block, begin=TEXT_BASE, end=TEXT_END)
            m.uc.ctl_flush_tb()
            log.info("call trace window %d started at %#x", len(state["windows"]), start)

    def on_stop():
        if state["hook"] is None:
            return
        m.uc.hook_del(state["hook"])
        state["hook"] = None
        log.info("call trace window %d stopped at %#x: %d entries", len(state["windows"]), stop,
                 len(state["windows"][-1]))
        if len(state["windows"]) == windows:
            write(state["windows"], atlas, out)

    session.hook(start, on_start)
    session.hook(stop, on_stop)
    return state


def write(windows, atlas, out: Path) -> None:
    lines, counts = [], {}
    for k, entries in enumerate(windows, start=1):
        lines.append(f"# window {k}")
        open_esp = []
        for address, esp in entries:
            while open_esp and open_esp[-1] <= esp:
                open_esp.pop()
            name = atlas.get(address, {}).get("name") or ""
            lines.append(f"{' ' * len(open_esp)}{address:#x} {name}")
            open_esp.append(esp)
            counts[f"{address:#x}"] = counts.get(f"{address:#x}", 0) + 1
    (out / "calltrace.txt").write_text("\n".join(lines) + "\n")
    (out / "calltrace_counts.json").write_text(json.dumps(counts, indent=1, sort_keys=True))
