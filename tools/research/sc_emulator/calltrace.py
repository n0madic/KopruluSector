"""Native call traces of a window of the game (phase 3 porting aid, docs/phase3.md).

Between the first execution of `start` and the next execution of `stop` (code addresses),
every entry of an atlas function in StarCraft.exe is recorded with the stack pointer at
entry. Nesting is recovered from the stack pointers: a call is a child of the closest
earlier open call whose entry ESP is higher. The block hook only exists inside the window,
so the rest of the run keeps its speed.

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


def install(session, start: int, stop: int, out: Path) -> dict:
    m = session.m
    atlas = coverage.load_atlas()
    functions = set(atlas)
    out.mkdir(parents=True, exist_ok=True)
    state = {"entries": [], "hook": None, "done": False}

    def on_block(uc, address, size, _):
        if address in functions:
            state["entries"].append((address, m.esp))

    def on_start():
        if state["hook"] is None and not state["done"]:
            state["hook"] = m.uc.hook_add(UC_HOOK_BLOCK, on_block, begin=TEXT_BASE, end=TEXT_END)
            log.info("call trace started at %#x", start)

    def on_stop():
        if state["hook"] is None or state["done"]:
            return
        m.uc.hook_del(state["hook"])
        state["hook"], state["done"] = None, True
        write(state["entries"], atlas, out)
        log.info("call trace stopped at %#x: %d entries", stop, len(state["entries"]))

    session.hook(start, on_start)
    session.hook(stop, on_stop)
    return state


def write(entries, atlas, out: Path) -> None:
    lines, counts, open_esp = [], {}, []
    for address, esp in entries:
        while open_esp and open_esp[-1] <= esp:
            open_esp.pop()
        name = atlas.get(address, {}).get("name") or ""
        lines.append(f"{' ' * len(open_esp)}{address:#x} {name}")
        open_esp.append(esp)
        counts[f"{address:#x}"] = counts.get(f"{address:#x}", 0) + 1
    (out / "calltrace.txt").write_text("\n".join(lines) + "\n")
    (out / "calltrace_counts.json").write_text(json.dumps(counts, indent=1, sort_keys=True))
