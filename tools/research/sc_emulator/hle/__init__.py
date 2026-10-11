"""High-level emulation of the Windows API boundary.

Every function is registered with :func:`api` under its DLL and export name. The
docstring of each function is its documented semantics; ``python -m sc_emulator apis``
prints the full table that the project documentation reproduces.

HLE code must never implement StarCraft or Storm logic: it only provides operating
system services (files, memory, synchronization, time, windows, DirectDraw, ...).
"""
from __future__ import annotations

from typing import Callable

from ..machine import ApiSpec

REGISTRY: dict[tuple[str, str | int], ApiSpec] = {}


def api(dll: str, nargs: int, name: str | None = None, cdecl: bool = False, ordinal: int | None = None):
    """Register an HLE implementation.

    ``nargs`` is the number of 32-bit stack arguments (stdcall functions pop them);
    ``nargs = -1`` marks a C varargs function whose handler reads its own arguments.
    """
    dll = dll.lower()

    def deco(fn: Callable):
        n = name or fn.__name__
        spec = ApiSpec(dll, n, fn, nargs, cdecl or nargs < 0, (fn.__doc__ or "").strip())
        REGISTRY[(dll, n)] = spec
        if ordinal is not None:
            REGISTRY[(dll, ordinal)] = spec
        return fn

    return deco


def alias(dll: str, name: str, target: str, target_dll: str | None = None):
    spec = REGISTRY[((target_dll or dll).lower(), target)]
    REGISTRY[(dll.lower(), name)] = spec


def load_all():
    from . import kernel32, user32, gdi32, misc, ddraw, dsound  # noqa: F401


load_all()
