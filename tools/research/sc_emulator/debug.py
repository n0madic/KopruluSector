"""Debugging aids: trace calls of chosen guest functions with their stack arguments."""
from __future__ import annotations

import json
import logging
from pathlib import Path

from unicorn import UC_HOOK_CODE

log = logging.getLogger(__name__)
STORM_ORDINALS = (Path.home() / "koprulu-private/work/storm_ordinals.json")


def trace_function(m, addr: int, name: str, nargs: int = 4, regs=()):
    from unicorn import x86_const as X
    names = {"ecx": X.UC_X86_REG_ECX, "edx": X.UC_X86_REG_EDX, "eax": X.UC_X86_REG_EAX}

    def hook(uc, address, size, _):
        esp = m.esp
        args = [m.r32(esp + 4 + 4 * i) for i in range(nargs)]
        extra = " ".join(f"{r}={m._fmt_arg(m.reg(names[r]))}" for r in regs)
        log.info("[%x] %s(%s) %s <- %#x", m.current.tid, name, ", ".join(m._fmt_arg(a) for a in args), extra,
                 m.r32(esp))

    m.uc.hook_add(UC_HOOK_CODE, hook, begin=addr, end=addr)


def trace_storm(m, pattern: str = "SFile", nargs: int = 4):
    storm = m.images["storm.dll"]
    names = {int(k): v for k, v in json.loads(STORM_ORDINALS.read_text())["names"].items()}
    for ordinal, name in names.items():
        if pattern in name:
            addr = storm.export(ordinal)
            if addr:
                trace_function(m, addr, name, nargs)
