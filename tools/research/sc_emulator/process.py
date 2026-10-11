"""Construct the emulated StarCraft process.

The loader maps StarCraft.exe and storm.dll at their preferred bases (0x400000 and
0x15000000), binds StarCraft.exe's Storm imports (by ordinal, plus two by mangled name)
to storm.dll's exports and every Windows import to HLE stubs, prepares the PEB and the
main thread, runs storm.dll's DllMain(PROCESS_ATTACH) and then starts the original
program entry point (the Visual C++ CRT start-up, which calls WinMain).
"""
from __future__ import annotations

import logging
import struct
from dataclasses import dataclass, field
from pathlib import Path

from . import memory as M
from . import vfs
from .hle.misc import Registry
from .hle.user32 import WindowManager
from .machine import Machine

log = logging.getLogger(__name__)

DEFAULT_GAME_DIR = Path("~/koprulu-private/full/app")
DEFAULT_EXE = Path("~/koprulu-private/retail/extracted/app/StarCraft.exe")
EXE_SHA256 = "94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0"
REG_SC = "HKCU\\Software\\Blizzard Entertainment\\Starcraft"
REG_DWORD = 4


def default_registry() -> dict:
    """Registry state of an installation whose player has already watched the intro videos
    (options ``intro``/``introX`` = 0) and turned off the start-of-game tips dialog (``tip`` = 0,
    which would otherwise wait for a click); every other option keeps the game's default."""
    off = (0).to_bytes(4, "little")
    return {REG_SC: {"intro": (REG_DWORD, off), "introX": (REG_DWORD, off), "tip": (REG_DWORD, off)}}


@dataclass
class Config:
    game_dir: Path = DEFAULT_GAME_DIR
    exe: Path = DEFAULT_EXE
    command_line: str = '"C:\\StarCraft\\StarCraft.exe"'
    registry: dict = field(default_factory=default_registry)
    sound_device: bool = False
    # Which CD archive the installation finds first (FindCdArchive tries BroodWar.mpq, then
    # StarCraft.mpq): "expansion" keeps both; "original" hides BroodWar.mpq, i.e. the original
    # StarCraft CD is the one in the drive, which the original campaign requires.
    cd: str = "expansion"
    log_api: bool = False


def build(cfg: Config) -> Machine:
    import hashlib
    digest = hashlib.sha256(Path(cfg.exe).read_bytes()).hexdigest()
    if digest != EXE_SHA256:
        raise ValueError(f"unexpected StarCraft.exe hash {digest}")
    m = Machine(cfg.game_dir, cfg.exe, log_api=cfg.log_api)
    m.services["fs"] = vfs.FileSystem(cfg.game_dir, hidden=("BroodWar.mpq",) if cfg.cd == "original" else ())
    m.services["registry"] = Registry(cfg.registry)
    wmgr = WindowManager(m)
    m.services["wm"] = wmgr
    m.services["idle"] = wmgr
    m.services["command_line"] = cfg.command_line
    m.services["dsound_device"] = cfg.sound_device

    exe = m.load_image(cfg.exe)
    exe.is_dll = False
    m.exe = exe
    storm = m.load_image(Path(cfg.game_dir) / "storm.dll")
    m.bind(storm)
    m.bind(exe)
    # native block hook: coverage maps for both images and deterministic time slices
    from .coverage import Coverage
    m.native = Coverage(m, [("exe", exe.base, exe.size), ("storm", storm.base, storm.size)])

    # PEB (only the fields the CRT and Storm read)
    peb = M.PEB_ADDR
    m.write(peb, bytes(0x1000))
    m.w32(peb + 0x08, exe.base)
    m.w32(peb + 0x18, 0x00050000)
    m.w32(peb + 0xA4, 5)
    m.w32(peb + 0xA8, 1)
    m.w32(peb + 0xAC, 2600)
    m.w32(peb + 0xB0, 2)

    main = m.create_thread(0, 0, 0x100000, name="main")
    main.ctx = m._fresh_context(main.stack_top - 0x40, 0, main.fs_selector)
    main.state = "host"
    m.uc.context_restore(main.ctx)
    m.current = main

    # Windows passes a non-NULL lpvReserved (a CONTEXT pointer) to DLLs loaded at process
    # start-up. storm.dll depends on it: with NULL (dynamic loading) its DllMain regenerates
    # the MPQ crypt table with a different seed, and no archive file can be found.
    static_ctx = m.alloc(0x2CC)
    r = m.call(storm.entry, storm.base, 1, static_ctx)
    log.info("storm.dll DllMain -> %d", r)
    if not r:
        raise RuntimeError("storm.dll DllMain failed")
    return m


def start_program(m: Machine):
    """Set up the main thread to execute the program entry point (the CRT start-up)."""
    t = m.threads[0]
    m.uc.context_restore(t.ctx)
    m.current = t
    sp = (m.esp - 0x100) & ~0xF
    from .machine import STUB_THREAD_EXIT
    m.write(sp, struct.pack("<I", STUB_THREAD_EXIT))
    from unicorn import x86_const as X
    m.set_reg(X.UC_X86_REG_ESP, sp)
    m.set_reg(X.UC_X86_REG_EIP, m.exe.entry)
    t.state = "ready"
