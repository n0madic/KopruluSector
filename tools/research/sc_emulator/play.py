"""Scripted play through synthetic window messages: game-state readers and input recipes.

Everything here only reads guest memory and posts input messages (see inputs.py);
the game's own window procedure, input procedures, selection, command card, placement
and order code act on them. Coordinates: map pixels; the game view is the top 640x400
of the screen with its origin (map pixel of the top-left corner) at 0x543218/0x54321c.
"""
from __future__ import annotations

import json
import logging
import struct
import time
from pathlib import Path

from . import game, process, render
from .inputs import Input
from .session import Session

log = logging.getLogger(__name__)

LOCAL_PLAYER = 0x508D50
SCREEN_X, SCREEN_Y = 0x543218, 0x54321C          # view origin, map pixels (P41)
MAP_W_TILES, MAP_H_TILES = 0x5264B4, 0x5264B6     # u16 (P15)
SELECTION = 0x6A6070                              # 12 CUnit pointers (P41)
COMMAND_CARD = 0x6AAAE8                           # dialog; children 1-9 buttons (P35)
BUTTON_SET = 0x6AAAF0                             # u16 button set shown (P35)
MINIMAP_DIALOG = 0x671A8C                         # minimap dialog; child 1 = image (P24)
MINIMAP_W, MINIMAP_H = 0x671FB8, 0x671FBC
MINIMAP_DIV, MINIMAP_SHIFT = 0x671FA8, 0x671FB0
VIEW_W, VIEW_H = 640, 400
U_STATUS = 0xDC
U_QUEUE = 0x98         # five u16 build-queue entries (BWAPI layout)
U_QUEUE_SLOT = 0xC8    # u8 current queue slot
U_ORDER_TARGET = 0x58  # order target position x, y (u16) and unit pointer (+0x5c)


class Player:
    def __init__(self, s: Session):
        self.s = s
        self.m = s.m
        self.inp = Input(s)

    # ---------------------------------------------------------------- state readers
    @property
    def local(self) -> int:
        return self.m.r32(LOCAL_PLAYER)

    def frame(self) -> int:
        return self.m.r32(game.FRAME_COUNTER)

    def resources(self, p=None):
        p = self.local if p is None else p
        return self.m.r32(game.MINERALS + 4 * p), self.m.r32(game.GAS + 4 * p)

    def unit(self, ptr) -> dict:
        m = self.m
        b = m.read(ptr, game.UNIT_SIZE)
        x, y = struct.unpack_from("<HH", b, game.U_POS)
        q = list(struct.unpack_from("<5H", b, U_QUEUE))
        return {"index": (ptr - game.UNIT_POOL) // game.UNIT_SIZE + 1, "ptr": ptr,
                "type": struct.unpack_from("<H", b, game.U_TYPE)[0], "owner": b[game.U_OWNER], "x": x, "y": y,
                "order": b[game.U_ORDER], "hp": struct.unpack_from("<i", b, game.U_HP)[0] >> 8,
                "status": struct.unpack_from("<I", b, U_STATUS)[0],
                "queue": q, "target": list(struct.unpack_from("<HH", b, U_ORDER_TARGET)),
                "target_unit": struct.unpack_from("<I", b, U_ORDER_TARGET + 4)[0]}

    def units(self, owner=None, types=None) -> list[dict]:
        out = []
        p = self.m.r32(game.UNIT_ACTIVE_HEAD)
        n = 0
        while p and n < 1700:
            u = self.unit(p)
            if (owner is None or u["owner"] == owner) and (types is None or u["type"] in types):
                out.append(u)
            p = self.m.r32(p + game.U_NEXT)
            n += 1
        return out

    def mine(self, types=None):
        return self.units(self.local, types)

    def selection(self) -> list[dict]:
        return [self.unit(p) for p in struct.unpack("<12I", self.m.read(SELECTION, 48)) if p]

    def view(self):
        return self.m.rs32(SCREEN_X), self.m.rs32(SCREEN_Y)

    def map_pixels(self):
        return self.m.r16(MAP_W_TILES) * 32, self.m.r16(MAP_H_TILES) * 32

    def control_rect(self, dialog_ptr_addr: int, cid: int):
        """Absolute screen rectangle (l, t, r, b) of child ``cid`` of the dialog stored at the address."""
        dlg = self.m.r32(dialog_ptr_addr)
        if not dlg:
            return None
        dl, dt = struct.unpack("<hh", self.m.read(dlg + 4, 4))
        c = self.m.r32(dlg + 0x42)
        while c:
            if struct.unpack("<h", self.m.read(c + 0x20, 2))[0] == cid:
                l, t, r, b = struct.unpack("<4h", self.m.read(c + 4, 8))
                return dl + l, dt + t, dl + r, dt + b
            c = self.m.r32(c)
        return None

    def button_visible(self, slot: int) -> bool:
        dlg = self.m.r32(COMMAND_CARD)
        c = self.m.r32(dlg + 0x42) if dlg else 0
        while c:
            if struct.unpack("<h", self.m.read(c + 0x20, 2))[0] == slot:
                return bool(self.m.r32(c + 0x18) & 8)
            c = self.m.r32(c)
        return False

    # ---------------------------------------------------------------- time
    def wait(self, ms):
        self.inp.wait(ms)

    def frames(self, n, limit=900):
        """Run ``n`` game frames; when the game is paused (e.g. behind an end-of-mission dialog),
        give up after the virtual time those frames would take at the slowest speed (167 ms each)
        plus one second."""
        end, t0 = self.frame() + n, self.m.clock_us
        budget = (n * 167 + 1000) * 1000
        self.s.run_until(lambda: self.frame() >= end or self.m.clock_us - t0 > budget, limit)

    def until(self, pred, max_frames=5000, step_ms=200):
        """Wait (in virtual time steps) until ``pred()``; give up after ``max_frames`` game frames
        or, when the game is paused (e.g. behind an end-of-mission dialog), after the virtual time
        those frames would take at the fastest speed (42 ms per frame)."""
        start, t0 = self.frame(), self.m.clock_us
        while not pred():
            if self.frame() - start > max_frames or self.m.clock_us - t0 > max_frames * 42_000:
                return False
            self.wait(step_ms)
        return True

    # ---------------------------------------------------------------- geometry
    def minimap_point(self, x, y):
        """Screen point of the minimap pixel showing map pixel (x, y)."""
        r = self.control_rect(MINIMAP_DIALOG, 1)
        w, h = self.m.r16(MINIMAP_W), self.m.r16(MINIMAP_H)
        div, shift = max(self.m.r16(MINIMAP_DIV), 1), self.m.r16(MINIMAP_SHIFT)
        ox = r[0] + ((r[2] - r[0] + 1) - w) // 2
        oy = r[1] + ((r[3] - r[1] + 1) - h) // 2
        return ox + ((x // 32) << shift) // div, oy + ((y // 32) << shift) // div

    def screen_point(self, x, y):
        ox, oy = self.view()
        return x - ox, y - oy

    def on_view(self, x, y, margin=24, bottom=330):
        sx, sy = self.screen_point(x, y)
        return margin <= sx < VIEW_W - margin and margin <= sy < bottom

    def center(self, x, y):
        """Left-click the minimap so that the view centres on (x, y)."""
        mx, my = self.minimap_point(x, y)
        self.inp.click(mx, my)
        self.wait(300)

    def ensure_visible(self, x, y):
        if not self.on_view(x, y):
            self.center(x, y)

    # ---------------------------------------------------------------- actions
    def click_map(self, x, y, right=False):
        self.ensure_visible(x, y)
        sx, sy = self.screen_point(x, y)
        self.inp.click(sx, sy, right=right)
        self.wait(150)

    def select_unit(self, u, shift=False):
        if shift:
            self.key_down(0x10)
        self.click_map(u["x"], u["y"])
        if shift:
            self.key_up(0x10)

    def key_down(self, vk):
        self.inp.wm.post(self.inp.hwnd, 0x100, vk, 1)

    def key_up(self, vk):
        self.inp.wm.post(self.inp.hwnd, 0x101, vk, 0xC0000001)

    def hotkey(self, vk, ctrl=False, shift=False):
        if ctrl:
            self.key_down(0x11)
        if shift:
            self.key_down(0x10)
        self.inp.key(vk)
        if shift:
            self.key_up(0x10)
        if ctrl:
            self.key_up(0x11)
        self.wait(200)

    def button(self, slot):
        """Left-click command-card button ``slot`` (1-9)."""
        r = self.control_rect(COMMAND_CARD, slot)
        self.inp.click((r[0] + r[2]) // 2, (r[1] + r[3]) // 2)
        self.wait(200)

    def minimap_click(self, x, y, right=False):
        mx, my = self.minimap_point(x, y)
        self.inp.click(mx, my, right=right)
        self.wait(200)

    def snap(self, path: Path):
        render.save_primary_surface(self.m, path)


def start_mission(warp: str, cd: str, frames_after_start=24) -> tuple[Session, Player]:
    s = Session(process.Config(command_line=f'"C:\\StarCraft\\StarCraft.exe" ophelia {warp}', cd=cd))
    pl = Player(s)
    s.start()
    s.run_until(lambda: s.m.r32(game.GLUE_STATE) in (7, 8, 9) and s.m.r32(game.READY_ROOM_DIALOG) != 0, 900)
    pl.wait(2000)
    pl.inp.key(ord("S"))
    s.run_until(lambda: s.m.r16(game.GAME_MODE) == 3 and s.m.r32(game.FRAME_COUNTER) >= frames_after_start, 900)
    return s, pl
