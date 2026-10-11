"""Synthetic user input: window messages posted to the game window's queue.

Keys are posted as WM_KEYDOWN/WM_KEYUP only; the game's own message loop calls
TranslateMessage, which (as on Windows) adds the WM_CHAR. Text is posted as WM_CHAR.
Mouse actions post WM_MOUSEMOVE/button messages with client coordinates and move the
virtual cursor; between the steps of an action the guest runs for a few virtual
milliseconds so that the game samples each state, as with a real mouse.
"""
from __future__ import annotations

WM_KEYDOWN, WM_KEYUP, WM_CHAR = 0x100, 0x101, 0x102
WM_MOUSEMOVE, WM_LBUTTONDOWN, WM_LBUTTONUP, WM_RBUTTONDOWN, WM_RBUTTONUP = 0x200, 0x201, 0x202, 0x204, 0x205
VK_RETURN, VK_ESCAPE = 0x0D, 0x1B
MK_LBUTTON, MK_RBUTTON = 1, 2


class Input:
    def __init__(self, session):
        self.s = session
        self.m = session.m
        self.wm = self.m.services["wm"]
        self.log: list[dict] = []

    @property
    def hwnd(self):
        return self.wm.foreground

    def wait(self, ms: int):
        end = self.m.clock_us + ms * 1000
        self.s.run_until(lambda: self.m.clock_us >= end, 600)

    def _note(self, kind, **kw):
        self.log.append({"t": self.m.clock_us // 1000, "kind": kind, **kw})

    def key(self, vk: int):
        self._note("key", vk=vk)
        self.wm.post(self.hwnd, WM_KEYDOWN, vk, 1)
        self.wm.post(self.hwnd, WM_KEYUP, vk, 0xC0000001)

    def text(self, s: str):
        self._note("text", text=s)
        for ch in s:
            self.wm.post(self.hwnd, WM_CHAR, ord(ch), 1)

    def _pos(self, x, y):
        self.wm.cursor_pos = [x, y]
        return (y << 16) | (x & 0xFFFF)

    def move(self, x, y, buttons=0):
        self.wm.post(self.hwnd, WM_MOUSEMOVE, buttons, self._pos(x, y))

    def click(self, x, y, right=False):
        self._note("rclick" if right else "click", x=x, y=y)
        down, up, mk = (WM_RBUTTONDOWN, WM_RBUTTONUP, MK_RBUTTON) if right else (WM_LBUTTONDOWN, WM_LBUTTONUP, MK_LBUTTON)
        self.move(x, y)
        self.wait(150)
        self.wm.post(self.hwnd, down, mk, self._pos(x, y))
        self.wait(60)
        self.wm.post(self.hwnd, up, 0, self._pos(x, y))

    def drag(self, x0, y0, x1, y1, steps=8):
        """Left-button drag (selection box)."""
        self._note("drag", x0=x0, y0=y0, x1=x1, y1=y1)
        self.move(x0, y0)
        self.wait(150)
        self.wm.post(self.hwnd, WM_LBUTTONDOWN, MK_LBUTTON, self._pos(x0, y0))
        self.wait(60)
        for i in range(1, steps + 1):
            self.move(x0 + (x1 - x0) * i // steps, y0 + (y1 - y0) * i // steps, MK_LBUTTON)
            self.wait(40)
        self.wm.post(self.hwnd, WM_LBUTTONUP, 0, self._pos(x1, y1))
