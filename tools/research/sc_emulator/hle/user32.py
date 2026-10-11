"""USER32 HLE: a headless window manager with a real message queue.

Windows are host records (class, procedure, rectangle, styles, window longs, props).
Messages are queued per process (the game has one UI thread); SendMessage and
DispatchMessage call guest window procedures by continuation. Timers produce WM_TIMER
from the virtual clock. Host code may inject input with :meth:`WindowManager.post`.
Every top-level window is 640x480 at (0, 0); the screen is 640x480.
"""
from __future__ import annotations

import logging
import re
import struct
from dataclasses import dataclass, field

from ..machine import Call, Wait
from . import api
from .kernel32 import module_name_from_handle, set_error

log = logging.getLogger(__name__)
U = "user32.dll"

WM_NULL, WM_CREATE, WM_DESTROY, WM_MOVE, WM_SIZE, WM_ACTIVATE, WM_SETFOCUS, WM_KILLFOCUS = 0, 1, 2, 3, 5, 6, 7, 8
WM_PAINT, WM_CLOSE, WM_QUIT, WM_ERASEBKGND, WM_SHOWWINDOW, WM_ACTIVATEAPP = 0xF, 0x10, 0x12, 0x14, 0x18, 0x1C
WM_SETCURSOR, WM_NCCREATE, WM_NCDESTROY, WM_NCACTIVATE = 0x20, 0x81, 0x82, 0x86
WM_KEYDOWN, WM_KEYUP, WM_CHAR, WM_SYSKEYDOWN, WM_SYSKEYUP = 0x100, 0x101, 0x102, 0x104, 0x105
# key-state words follow GetKeyState: bit 15 = down, bit 0 = toggle
WM_TIMER, WM_MOUSEMOVE, WM_LBUTTONDOWN, WM_LBUTTONUP = 0x113, 0x200, 0x201, 0x202
SCREEN_W, SCREEN_H = 640, 480
DESKTOP = 0x10010


@dataclass
class WndClass:
    name: str
    proc: int
    style: int = 0
    extra: int = 0
    instance: int = 0
    icon: int = 0
    cursor: int = 0
    brush: int = 0
    long_values: dict = field(default_factory=dict)


@dataclass
class Window:
    hwnd: int
    cls: WndClass
    proc: int
    style: int
    exstyle: int
    parent: int
    rect: list
    text: bytes = b""
    menu_id: int = 0
    longs: dict = field(default_factory=dict)
    props: dict = field(default_factory=dict)
    visible: bool = False
    enabled: bool = True
    destroyed: bool = False
    owner: int = 0


@dataclass
class Timer:
    hwnd: int
    ident: int
    period_us: int
    proc: int
    due_us: int


class WindowManager:
    def __init__(self, m):
        self.m = m
        self.classes: dict[str, WndClass] = {}
        self.windows: dict[int, Window] = {}
        self.next_hwnd = 0x10020
        self.queue: list[tuple] = []          # (hwnd, msg, wparam, lparam)
        self.timers: list[Timer] = []
        self.focus = 0
        self.active = 0
        self.foreground = 0
        self.capture = 0
        self.cursor_pos = [320, 240]
        self.cursor_show = 0
        self.keys: dict[int, int] = {}
        self.quit = False
        self.posted_log: list = []
        self.idle_quantum_us = 1000   # virtual time charged when the UI thread polls an empty queue
        self.idle_polls = 0

    # -- queue ------------------------------------------------------------------
    def post(self, hwnd, msg, wparam=0, lparam=0):
        self.queue.append((hwnd, msg, wparam & 0xFFFFFFFF, lparam & 0xFFFFFFFF))

    def next_due_us(self):
        return min((t.due_us for t in self.timers), default=None)

    def _due_timer(self):
        now = self.m.clock_us
        due = [t for t in self.timers if t.due_us <= now]
        if not due:
            return None
        t = min(due, key=lambda x: x.due_us)
        t.due_us = now + t.period_us
        return (t.hwnd, WM_TIMER, t.ident, t.proc)

    def _matches(self, entry, hwnd, lo, hi):
        h, msg = entry[0], entry[1]
        if hwnd not in (0, 0xFFFFFFFF) and h != hwnd and not self._is_child(h, hwnd):
            return False
        if lo == 0 and hi == 0:
            return True
        return lo <= msg <= hi

    def _is_child(self, h, parent):
        w = self.windows.get(h)
        while w is not None and w.parent:
            if w.parent == parent:
                return True
            w = self.windows.get(w.parent)
        return False

    def take(self, hwnd, lo, hi, remove):
        for i, e in enumerate(self.queue):
            if self._matches(e, hwnd, lo, hi):
                if remove:
                    del self.queue[i]
                    if e[1] in (WM_KEYDOWN, WM_SYSKEYDOWN):
                        self.keys[e[2] & 0xFF] = 0x8000 | (self.keys.get(e[2] & 0xFF, 0) & 1) ^ 1
                    elif e[1] in (WM_KEYUP, WM_SYSKEYUP):
                        self.keys[e[2] & 0xFF] = self.keys.get(e[2] & 0xFF, 0) & 1
                return e
        if self.quit:
            return None
        t = None
        if lo == 0 and hi == 0 or lo <= WM_TIMER <= hi:
            now = self.m.clock_us
            due = [x for x in self.timers if x.due_us <= now and (hwnd in (0, 0xFFFFFFFF) or x.hwnd == hwnd)]
            if due:
                x = min(due, key=lambda y: y.due_us)
                if remove:
                    x.due_us = now + x.period_us
                t = (x.hwnd, WM_TIMER, x.ident, x.proc)
        return t

    def has_input(self):
        return bool(self.queue) or any(t.due_us <= self.m.clock_us for t in self.timers)

    # -- windows ----------------------------------------------------------------
    def new_window(self, cls, style, exstyle, parent, rect, text, menu):
        hwnd = self.next_hwnd
        self.next_hwnd += 4
        w = Window(hwnd, cls, cls.proc, style, exstyle, parent, rect, text, menu)
        self.windows[hwnd] = w
        return w

    def get(self, hwnd) -> Window | None:
        w = self.windows.get(hwnd)
        return None if w is None or w.destroyed else w


def wm(m) -> WindowManager:
    return m.services["wm"]


def write_msg(m, p, e):
    hwnd, msg, wp, lp = e
    m.write(p, struct.pack("<IIIIIii", hwnd, msg, wp, lp, (m.clock_us // 1000) & 0xFFFFFFFF, *wm(m).cursor_pos))


def send(m, hwnd, msg, wp=0, lp=0):
    """Generator: deliver a message synchronously to a window procedure."""
    w = wm(m).get(hwnd)
    if w is None:
        return 0
    if w.proc == 0 or w.proc >= 0x7E000000:
        return default_proc(m, hwnd, msg, wp, lp)
    return (yield Call(w.proc, (hwnd, msg, wp, lp)))


def default_proc(m, hwnd, msg, wp, lp):
    if msg == WM_NCCREATE:
        return 1
    if msg == WM_ERASEBKGND:
        return 1
    if msg == WM_CLOSE:
        _destroy(m, hwnd)
        return 0
    return 0


def _destroy(m, hwnd):
    w = wm(m).get(hwnd)
    if w:
        w.destroyed = True
        for c in list(wm(m).windows.values()):
            if c.parent == hwnd and not c.destroyed:
                _destroy(m, c.hwnd)


# ----------------------------------------------------------------------------- classes / windows
def _class_name(m, p):
    if p < 0x10000:
        return f"#{p}"
    return m.cstr(p).decode("latin-1")


@api(U, 1)
def RegisterClassA(m, p):
    """Records WNDCLASSA (procedure, extra bytes, icon, cursor, brush); returns an atom."""
    style, proc, cls_extra, wnd_extra, inst, icon, cursor, brush, menu, name = struct.unpack("<10I", m.read(p, 40))
    n = _class_name(m, name)
    wm(m).classes[n.lower()] = WndClass(n, proc, style, wnd_extra, inst, icon, cursor, brush)
    return 0xC000 + len(wm(m).classes)


@api(U, 1)
def RegisterClassExA(m, p):
    """Records WNDCLASSEXA; returns an atom."""
    size, style, proc, cls_extra, wnd_extra, inst, icon, cursor, brush, menu, name, sm = struct.unpack(
        "<12I", m.read(p, 48))
    n = _class_name(m, name)
    wm(m).classes[n.lower()] = WndClass(n, proc, style, wnd_extra, inst, icon, cursor, brush)
    return 0xC000 + len(wm(m).classes)


@api(U, 2)
def UnregisterClassA(m, name, inst):
    """Forgets the class."""
    wm(m).classes.pop(_class_name(m, name).lower(), None)
    return 1


@api(U, 3)
def GetClassInfoA(m, inst, name, out):
    """Fills WNDCLASSA for registered classes; FALSE otherwise."""
    c = wm(m).classes.get(_class_name(m, name).lower())
    if c is None:
        return 0
    m.write(out, struct.pack("<10I", c.style, c.proc, 0, c.extra, c.instance, c.icon, c.cursor, c.brush, 0, name))
    return 1


@api(U, 12)
def CreateWindowExA(m, exstyle, cls_name, title, style, x, y, w, h, parent, menu, inst, param):
    """Creates a headless window and sends, like Windows, WM_NCCREATE and WM_CREATE (with a
    CREATESTRUCTA). A visible top-level window then receives WM_SHOWWINDOW, WM_ACTIVATEAPP(1),
    WM_NCACTIVATE(1), WM_ACTIVATE(1), WM_SETFOCUS, WM_SIZE and WM_MOVE, and becomes the
    active, foreground and focus window. Default sizes (CW_USEDEFAULT) become 640x480."""
    n = _class_name(m, cls_name)
    c = wm(m).classes.get(n.lower())
    if c is None:
        c = WndClass(n, 0)  # system class (static, button, ...) handled by DefWindowProc
    sx = lambda v: struct.unpack("<i", struct.pack("<I", v))[0]
    x, y, w_, h_ = sx(x), sx(y), sx(w), sx(h)
    if x == -0x80000000:
        x, y = 0, 0
    if w_ == -0x80000000:
        w_, h_ = SCREEN_W, SCREEN_H
    text = m.cstr(title) if title else b""
    win = wm(m).new_window(c, style, exstyle, parent if style & 0x40000000 else 0, [x, y, x + w_, y + h_], text,
                           menu if style & 0x40000000 else 0)
    win.owner = 0 if style & 0x40000000 else parent
    log.info("CreateWindowEx class=%s title=%r -> %#x (proc %#x)", n, text, win.hwnd, c.proc)
    cs = m.alloc(48)
    m.write(cs, struct.pack("<12I", param, inst, menu, parent, h_ & 0xFFFFFFFF, w_ & 0xFFFFFFFF, y & 0xFFFFFFFF,
                            x & 0xFFFFFFFF, style, title, cls_name, exstyle))
    r = yield from send(m, win.hwnd, WM_NCCREATE, 0, cs)
    if not r:
        win.destroyed = True
        return 0
    r = yield from send(m, win.hwnd, WM_CREATE, 0, cs)
    if r == 0xFFFFFFFF:
        win.destroyed = True
        return 0
    if style & 0x10000000:  # WS_VISIBLE
        win.visible = True
        yield from send(m, win.hwnd, WM_SHOWWINDOW, 1, 0)
        if not win.parent:
            wm(m).active = wm(m).foreground = wm(m).focus = win.hwnd
            yield from send(m, win.hwnd, WM_ACTIVATEAPP, 1, 0)
            yield from send(m, win.hwnd, WM_NCACTIVATE, 1, 0)
            yield from send(m, win.hwnd, WM_ACTIVATE, 1, 0)
            yield from send(m, win.hwnd, WM_SETFOCUS, 0, 0)
        yield from send(m, win.hwnd, WM_SIZE, 0, (h_ << 16) | (w_ & 0xFFFF))
        yield from send(m, win.hwnd, WM_MOVE, 0, (y << 16) | (x & 0xFFFF))
    return win.hwnd


@api(U, 1)
def DestroyWindow(m, hwnd):
    """Sends WM_DESTROY and WM_NCDESTROY, then marks the window (and children) destroyed."""
    w = wm(m).get(hwnd)
    if w is None:
        return 0
    yield from send(m, hwnd, WM_DESTROY, 0, 0)
    yield from send(m, hwnd, WM_NCDESTROY, 0, 0)
    _destroy(m, hwnd)
    for attr in ("focus", "active", "foreground", "capture"):
        if getattr(wm(m), attr) == hwnd:
            setattr(wm(m), attr, 0)
    return 1


@api(U, 4)
def DefWindowProcA(m, hwnd, msg, wp, lp):
    """WM_NCCREATE -> TRUE, WM_ERASEBKGND -> 1, WM_CLOSE destroys the window, others 0."""
    if msg == WM_CLOSE:
        return (yield from DestroyWindow(m, hwnd))
    return default_proc(m, hwnd, msg, wp, lp)


@api(U, 4)
def DefDlgProcA(m, hwnd, msg, wp, lp):
    """As DefWindowProcA."""
    return default_proc(m, hwnd, msg, wp, lp)


@api(U, 5)
def CallWindowProcA(m, proc, hwnd, msg, wp, lp):
    """Calls the guest procedure (or the default procedure for HLE addresses)."""
    if proc == 0 or proc >= 0x7E000000:
        return default_proc(m, hwnd, msg, wp, lp)
    return (yield Call(proc, (hwnd, msg, wp, lp)))


@api(U, 4)
def SendMessageA(m, hwnd, msg, wp, lp):
    """Synchronous call of the window procedure (HWND_BROADCAST is ignored)."""
    if hwnd == 0xFFFF:
        return 0
    return (yield from send(m, hwnd, msg, wp, lp))


@api(U, 4)
def PostMessageA(m, hwnd, msg, wp, lp):
    """Appends to the message queue."""
    wm(m).post(hwnd, msg, wp, lp)
    return 1


@api(U, 1)
def PostQuitMessage(m, code):
    """Queues WM_QUIT."""
    wm(m).post(0, WM_QUIT, code, 0)


def _peek(m, msgp, hwnd, lo, hi, flags):
    e = wm(m).take(hwnd, lo, hi, bool(flags & 1))
    if e is None:
        return None
    write_msg(m, msgp, e)
    return e


@api(U, 5)
def PeekMessageA(m, msgp, hwnd, lo, hi, flags):
    """Posted messages first, then due timers. An empty poll charges ``idle_quantum_us`` (1 ms)
    of virtual time to the clock: this is how time passes for the game's busy loops."""
    e = _peek(m, msgp, hwnd, lo, hi, flags)
    if e is None:
        w = wm(m)
        w.idle_polls += 1
        m.clock_us += w.idle_quantum_us
        return 0
    return 1


@api(U, 4)
def GetMessageA(m, msgp, hwnd, lo, hi):
    """Blocks (cooperatively) until a message or timer is available; the virtual clock jumps to
    the next timer when every thread is idle. Returns 0 for WM_QUIT."""
    while True:
        e = _peek(m, msgp, hwnd, lo, hi, 1)
        if e is not None:
            return 0 if e[1] == WM_QUIT else 1
        yield Wait(predicate=wm(m).has_input)


@api(U, 5)
def MsgWaitForMultipleObjects(m, n, handles, wait_all, timeout, mask):
    """Waits for the handles or for queued input."""
    hs = struct.unpack(f"<{n}I", m.read(handles, 4 * n)) if n else ()
    return (yield Wait(tuple(hs), timeout, bool(wait_all), predicate=wm(m).has_input))


@api(U, 1)
def TranslateMessage(m, msgp):
    """Posts WM_CHAR for WM_KEYDOWN of printable keys (US layout, no shift state)."""
    hwnd, msg, wp, lp = struct.unpack("<IIII", m.read(msgp, 16))
    if msg == WM_KEYDOWN and (0x30 <= wp <= 0x39 or 0x41 <= wp <= 0x5A or wp in (0x20, 0x0D, 0x08, 0x1B)):
        ch = wp if not (0x41 <= wp <= 0x5A) else wp + 0x20
        wm(m).queue.insert(0, (hwnd, WM_CHAR, ch, lp))
        return 1
    return 0


@api(U, 1)
def DispatchMessageA(m, msgp):
    """WM_TIMER with a callback calls it; otherwise the target window's procedure."""
    hwnd, msg, wp, lp = struct.unpack("<IIII", m.read(msgp, 16))
    if msg == WM_TIMER and lp:
        return (yield Call(lp, (hwnd, msg, wp, (m.clock_us // 1000) & 0xFFFFFFFF)))
    return (yield from send(m, hwnd, msg, wp, lp))


@api(U, 2)
def IsDialogMessageA(m, hwnd, msgp):
    """FALSE (no Win32 dialog navigation)."""
    return 0


@api(U, 3)
def TranslateAcceleratorA(m, hwnd, table, msgp):
    """Win32 accelerator matching: WM_KEYDOWN/WM_SYSKEYDOWN against FVIRTKEY entries and WM_CHAR
    against character entries, with the Shift/Ctrl/Alt state (FSHIFT 4, FCONTROL 8, FALT 0x10) from
    the key-state table. A match sends WM_COMMAND(0x10000 | cmd) to ``hwnd`` synchronously and
    returns TRUE."""
    data = m.services.setdefault("accel", {}).get(table)
    if not data:
        return 0
    h, msg, wp, lp = struct.unpack("<IIII", m.read(msgp, 16))
    if msg not in (WM_KEYDOWN, WM_SYSKEYDOWN, WM_CHAR):
        return 0
    keys = wm(m).keys
    mods = (4 if keys.get(0x10, 0) & 0x8000 else 0) | (8 if keys.get(0x11, 0) & 0x8000 else 0) | \
        (0x10 if keys.get(0x12, 0) & 0x8000 or msg == WM_SYSKEYDOWN else 0)
    for i in range(0, len(data) - 5, 6):
        fvirt, key, cmd = struct.unpack_from("<BxHH", data, i)
        if fvirt & 1:
            if msg == WM_CHAR or key != wp or (fvirt & 0x1C) != mods:
                continue
        else:
            if msg != WM_CHAR or key != wp:
                continue
        yield from send(m, hwnd, 0x111, 0x10000 | cmd, 0)
        return 1
    return 0


@api(U, 4)
def SetTimer(m, hwnd, ident, ms, proc):
    """Virtual-clock timer; replaces an existing (hwnd, id) timer."""
    w = wm(m)
    if hwnd == 0 and ident == 0:
        ident = 0x7000 + len(w.timers)
    w.timers = [t for t in w.timers if not (t.hwnd == hwnd and t.ident == ident)]
    w.timers.append(Timer(hwnd, ident, max(ms, 10) * 1000, proc, m.clock_us + max(ms, 10) * 1000))
    return ident or 1


@api(U, 2)
def KillTimer(m, hwnd, ident):
    """Removes the timer."""
    w = wm(m)
    before = len(w.timers)
    w.timers = [t for t in w.timers if not (t.hwnd == hwnd and t.ident == ident)]
    return 1 if len(w.timers) != before else 0


@api(U, 2)
def WaitForInputIdle(m, proc, ms):
    """0."""
    return 0


@api(U, 2)
def EnumWindows(m, cb, lp):
    """Calls the guest callback for every live top-level window, in creation order."""
    for hwnd, w in list(wm(m).windows.items()):
        if w.destroyed or w.parent:
            continue
        r = yield Call(cb, (hwnd, lp))
        if not r:
            break
    return 1


# ----------------------------------------------------------------------------- window state
def _rect(m, p, r):
    m.write(p, struct.pack("<4i", *r))


@api(U, 2)
def GetWindowRect(m, hwnd, p):
    """Window rectangle in screen coordinates."""
    w = wm(m).get(hwnd)
    _rect(m, p, w.rect if w else [0, 0, SCREEN_W, SCREEN_H])
    return 1


@api(U, 2)
def GetClientRect(m, hwnd, p):
    """(0, 0, width, height)."""
    w = wm(m).get(hwnd)
    r = w.rect if w else [0, 0, SCREEN_W, SCREEN_H]
    _rect(m, p, [0, 0, r[2] - r[0], r[3] - r[1]])
    return 1


@api(U, 3)
def AdjustWindowRectEx(m, p, style, menu, ex):
    """No frame: rectangle unchanged."""
    return 1


@api(U, 2)
def ScreenToClient(m, hwnd, p):
    """Subtracts the window origin."""
    w = wm(m).get(hwnd)
    if w:
        x, y = struct.unpack("<2i", m.read(p, 8))
        m.write(p, struct.pack("<2i", x - w.rect[0], y - w.rect[1]))
    return 1


@api(U, 2)
def ClientToScreen(m, hwnd, p):
    """Adds the window origin."""
    w = wm(m).get(hwnd)
    if w:
        x, y = struct.unpack("<2i", m.read(p, 8))
        m.write(p, struct.pack("<2i", x + w.rect[0], y + w.rect[1]))
    return 1


@api(U, 2)
def ShowWindow(m, hwnd, cmd):
    """Updates visibility; returns the previous visibility."""
    w = wm(m).get(hwnd)
    if not w:
        return 0
    prev = w.visible
    w.visible = cmd != 0
    return 1 if prev else 0


@api(U, 7)
def SetWindowPos(m, hwnd, after, x, y, cx, cy, flags):
    """Moves/resizes unless SWP_NOMOVE/SWP_NOSIZE; SWP_SHOWWINDOW/HIDEWINDOW honoured."""
    w = wm(m).get(hwnd)
    if not w:
        return 0
    sx = lambda v: struct.unpack("<i", struct.pack("<I", v))[0]
    if not flags & 2:
        dx, dy = sx(x) - w.rect[0], sx(y) - w.rect[1]
        w.rect = [w.rect[0] + dx, w.rect[1] + dy, w.rect[2] + dx, w.rect[3] + dy]
    if not flags & 1:
        w.rect[2], w.rect[3] = w.rect[0] + sx(cx), w.rect[1] + sx(cy)
    if flags & 0x40:
        w.visible = True
    if flags & 0x80:
        w.visible = False
    return 1


@api(U, 1)
def UpdateWindow(m, hwnd):
    """TRUE (painting is driven by the game itself)."""
    return 1


@api(U, 3)
def InvalidateRect(m, hwnd, r, erase):
    """TRUE (no WM_PAINT is generated: the game redraws its whole surface every frame)."""
    return 1


@api(U, 2)
def GetUpdateRgn(m, hwnd, rgn, erase):
    """NULLREGION."""
    return 1


@api(U, 2)
def BeginPaint(m, hwnd, ps):
    """Fills PAINTSTRUCT with a screen DC and an empty paint rectangle."""
    m.write(ps, struct.pack("<II", 0x20001, 0) + bytes(56))
    return 0x20001


@api(U, 2)
def EndPaint(m, hwnd, ps):
    """TRUE."""
    return 1


@api(U, 1)
def GetDC(m, hwnd):
    """Constant screen DC handle."""
    return 0x20001


@api(U, 1)
def GetWindowDC(m, hwnd):
    """Constant screen DC handle."""
    return 0x20001


@api(U, 2)
def ReleaseDC(m, hwnd, dc):
    """1."""
    return 1


GWL_WNDPROC, GWL_STYLE, GWL_EXSTYLE, GWL_ID, GWL_USERDATA, GWL_HWNDPARENT = -4, -16, -20, -12, -21, -8


def _sidx(v):
    return struct.unpack("<i", struct.pack("<I", v))[0]


@api(U, 2)
def GetWindowLongA(m, hwnd, idx):
    """GWL_WNDPROC/STYLE/EXSTYLE/ID/HWNDPARENT/USERDATA and extra window bytes."""
    w = wm(m).get(hwnd)
    if not w:
        return 0
    i = _sidx(idx)
    return {GWL_WNDPROC: w.proc, GWL_STYLE: w.style, GWL_EXSTYLE: w.exstyle, GWL_ID: w.menu_id,
            GWL_HWNDPARENT: w.parent or w.owner}.get(i, w.longs.get(i, 0))


@api(U, 3)
def SetWindowLongA(m, hwnd, idx, value):
    """Sets the value and returns the previous one (subclassing changes the procedure)."""
    w = wm(m).get(hwnd)
    if not w:
        return 0
    i = _sidx(idx)
    prev = GetWindowLongA(m, hwnd, idx)
    if i == GWL_WNDPROC:
        w.proc = value
    elif i == GWL_STYLE:
        w.style = value
    elif i == GWL_EXSTYLE:
        w.exstyle = value
    elif i == GWL_ID:
        w.menu_id = value
    else:
        w.longs[i] = value
    return prev


@api(U, 2)
def GetClassLongA(m, hwnd, idx):
    """GCL_WNDPROC (-24), GCL_HCURSOR (-12), GCL_HICON (-14), GCL_HBRBACKGROUND (-10)."""
    w = wm(m).get(hwnd)
    if not w:
        return 0
    c = w.cls
    return {-24: c.proc, -12: c.cursor, -14: c.icon, -10: c.brush, -26: c.style}.get(_sidx(idx),
                                                                                    c.long_values.get(_sidx(idx), 0))


@api(U, 3)
def SetClassLongA(m, hwnd, idx, value):
    """Sets a class value; returns the previous one."""
    w = wm(m).get(hwnd)
    if not w:
        return 0
    prev = GetClassLongA(m, hwnd, idx)
    i = _sidx(idx)
    c = w.cls
    if i == -24:
        c.proc = value
    elif i == -12:
        c.cursor = value
    elif i == -14:
        c.icon = value
    elif i == -10:
        c.brush = value
    else:
        c.long_values[i] = value
    return prev


@api(U, 3)
def SetPropA(m, hwnd, name, value):
    """Window property store."""
    w = wm(m).get(hwnd)
    if not w:
        return 0
    w.props[name if name < 0x10000 else m.cstr(name)] = value
    return 1


@api(U, 2)
def GetPropA(m, hwnd, name):
    """Window property lookup."""
    w = wm(m).get(hwnd)
    return w.props.get(name if name < 0x10000 else m.cstr(name), 0) if w else 0


@api(U, 2)
def RemovePropA(m, hwnd, name):
    """Removes and returns a property."""
    w = wm(m).get(hwnd)
    return w.props.pop(name if name < 0x10000 else m.cstr(name), 0) if w else 0


@api(U, 1)
def GetParent(m, hwnd):
    """Parent (child windows) or owner (top-level windows)."""
    w = wm(m).get(hwnd)
    return (w.parent or w.owner) if w else 0


@api(U, 2)
def GetWindow(m, hwnd, cmd):
    """GW_CHILD (5) first child, GW_HWNDNEXT (2) next sibling, GW_OWNER (4); others NULL."""
    ws = [w for w in wm(m).windows.values() if not w.destroyed]
    w = wm(m).get(hwnd)
    if cmd == 5:
        return next((c.hwnd for c in ws if c.parent == hwnd), 0)
    if cmd == 4:
        return w.owner if w else 0
    if cmd in (2, 3) and w:
        sib = [c.hwnd for c in ws if c.parent == w.parent]
        i = sib.index(hwnd)
        j = i + 1 if cmd == 2 else i - 1
        return sib[j] if 0 <= j < len(sib) else 0
    if cmd == 0 and w:
        sib = [c.hwnd for c in ws if c.parent == w.parent]
        return sib[0] if sib else 0
    return 0


@api(U, 1)
def GetTopWindow(m, hwnd):
    """First child of ``hwnd`` (or first top-level window)."""
    for w in wm(m).windows.values():
        if not w.destroyed and w.parent == hwnd:
            return w.hwnd
    return 0


@api(U, 0)
def GetDesktopWindow(m):
    """Constant desktop handle."""
    return DESKTOP


@api(U, 0)
def GetForegroundWindow(m):
    """The foreground window (the game window once created)."""
    return wm(m).foreground


@api(U, 1)
def SetForegroundWindow(m, hwnd):
    """Sets the foreground window."""
    wm(m).foreground = hwnd
    return 1


@api(U, 0)
def GetActiveWindow(m):
    """The active window."""
    return wm(m).active


@api(U, 1)
def SetActiveWindow(m, hwnd):
    """Sets the active window; returns the previous one."""
    prev, wm(m).active = wm(m).active, hwnd
    return prev


@api(U, 0)
def GetFocus(m):
    """The focus window."""
    return wm(m).focus


@api(U, 1)
def SetFocus(m, hwnd):
    """Moves the focus, sending WM_KILLFOCUS/WM_SETFOCUS; returns the previous focus."""
    prev = wm(m).focus
    if prev == hwnd:
        return prev
    wm(m).focus = hwnd
    if prev and wm(m).get(prev):
        yield from send(m, prev, WM_KILLFOCUS, hwnd, 0)
    if hwnd and wm(m).get(hwnd):
        yield from send(m, hwnd, WM_SETFOCUS, prev, 0)
    return prev


@api(U, 1)
def GetLastActivePopup(m, hwnd):
    """The window itself."""
    return hwnd


@api(U, 1)
def IsIconic(m, hwnd):
    """FALSE."""
    return 0


@api(U, 1)
def IsWindow(m, hwnd):
    """TRUE for live windows."""
    return 1 if wm(m).get(hwnd) else 0


@api(U, 1)
def IsWindowVisible(m, hwnd):
    """Visibility flag."""
    w = wm(m).get(hwnd)
    return 1 if w and w.visible else 0


@api(U, 1)
def IsWindowEnabled(m, hwnd):
    """Enabled flag."""
    w = wm(m).get(hwnd)
    return 1 if w and w.enabled else 0


@api(U, 2)
def EnableWindow(m, hwnd, en):
    """Sets the enabled flag; returns TRUE when it was previously disabled."""
    w = wm(m).get(hwnd)
    if not w:
        return 0
    prev = not w.enabled
    w.enabled = bool(en)
    return 1 if prev else 0


@api(U, 2)
def IsChild(m, parent, hwnd):
    """Parent-chain test."""
    return 1 if wm(m)._is_child(hwnd, parent) else 0


@api(U, 3)
def GetClassNameA(m, hwnd, buf, n):
    """Registered class name."""
    w = wm(m).get(hwnd)
    if not w:
        return 0
    s = w.cls.name.encode("latin-1")[:n - 1]
    m.write(buf, s + b"\0")
    return len(s)


def _find_window(m, parent, after, cls, title):
    cn = _class_name(m, cls).lower() if cls else None
    tt = m.cstr(title) if title else None
    seen = after == 0
    for w in wm(m).windows.values():
        if w.destroyed:
            continue
        if not seen:
            seen = w.hwnd == after
            continue
        if parent not in (0, DESKTOP) and w.parent != parent:
            continue
        if cn is not None and w.cls.name.lower() != cn:
            continue
        if tt is not None and w.text != tt:
            continue
        return w.hwnd
    return 0


@api(U, 2)
def FindWindowA(m, cls, title):
    """Searches the emulated windows (no other processes exist)."""
    return _find_window(m, 0, 0, cls, title)


@api(U, 4)
def FindWindowExA(m, parent, after, cls, title):
    """Searches the emulated windows."""
    return _find_window(m, parent, after, cls, title)


@api(U, 2)
def GetWindowThreadProcessId(m, hwnd, pid):
    """Main thread of this process."""
    if pid:
        m.w32(pid, 0x1000)
    return m.threads[0].tid


@api(U, 2)
def SetWindowTextA(m, hwnd, s):
    """Stores the text."""
    w = wm(m).get(hwnd)
    if w:
        w.text = m.cstr(s)
    return 1


@api(U, 3)
def GetWindowTextA(m, hwnd, buf, n):
    """Returns the stored text."""
    w = wm(m).get(hwnd)
    s = (w.text if w else b"")[:max(n, 1) - 1]
    if n:
        m.write(buf, s + b"\0")
    return len(s)


@api(U, 1)
def GetWindowTextLengthA(m, hwnd):
    """Length of the stored text."""
    w = wm(m).get(hwnd)
    return len(w.text) if w else 0


@api(U, 2)
def GetDlgItem(m, hwnd, ident):
    """Child with the given control id."""
    for w in wm(m).windows.values():
        if not w.destroyed and w.parent == hwnd and w.menu_id == ident:
            return w.hwnd
    return 0


@api(U, 1)
def GetDlgCtrlID(m, hwnd):
    """Control id."""
    w = wm(m).get(hwnd)
    return w.menu_id if w else 0


@api(U, 3)
def SetDlgItemTextA(m, hwnd, ident, s):
    """SetWindowTextA on the control."""
    h = GetDlgItem(m, hwnd, ident)
    return SetWindowTextA(m, h, s) if h else 0


@api(U, 4)
def GetDlgItemTextA(m, hwnd, ident, buf, n):
    """GetWindowTextA on the control."""
    h = GetDlgItem(m, hwnd, ident)
    if not h:
        if n:
            m.w8(buf, 0)
        return 0
    return GetWindowTextA(m, h, buf, n)


@api(U, 5)
def SendDlgItemMessageA(m, hwnd, ident, msg, wp, lp):
    """SendMessageA to the control."""
    h = GetDlgItem(m, hwnd, ident)
    if not h:
        return 0
    return (yield from send(m, h, msg, wp, lp))


@api(U, 2)
def EndDialog(m, hwnd, result):
    """Destroys the dialog window."""
    _destroy(m, hwnd)
    return 1


@api(U, 5)
def DialogBoxParamA(m, inst, template, parent, proc, param):
    """Win32 modal dialogs are only used for fatal start-up errors: logged, returns IDCANCEL (2)."""
    log.warning("DialogBoxParamA template=%#x proc=%#x param=%#x (returns IDCANCEL)", template, proc, param)
    m.services.setdefault("dialogs", []).append({"template": template, "proc": proc})
    return 2


@api(U, 4)
def MessageBoxA(m, hwnd, text, caption, flags):
    """Logged (text and caption are recorded); returns IDOK (1), or IDYES (6) for yes/no boxes."""
    t, c = m.cstr(text).decode("latin-1"), m.cstr(caption).decode("latin-1")
    log.warning("MessageBox [%s] %s", c, t)
    m.services.setdefault("message_boxes", []).append({"caption": c, "text": t})
    return 6 if flags & 0xF in (3, 4) else 1


# ----------------------------------------------------------------------------- input / cursor
@api(U, 1)
def GetKeyState(m, vk):
    """Key state as Windows keeps it: updated when WM_KEYDOWN/WM_KEYUP are retrieved from the
    queue (bit 15 down, sign-extended; bit 0 toggle). All keys start up."""
    v = wm(m).keys.get(vk, 0)
    return v | 0xFFFF0000 if v & 0x8000 else v


@api(U, 1)
def GetCursorPos(m, p):
    """Virtual cursor position (starts at 320, 240)."""
    m.write(p, struct.pack("<2i", *wm(m).cursor_pos))
    return 1


@api(U, 2)
def SetCursorPos(m, x, y):
    """Moves the virtual cursor."""
    wm(m).cursor_pos = [_sidx(x), _sidx(y)]
    return 1


@api(U, 1)
def ClipCursor(m, r):
    """TRUE (no clipping)."""
    return 1


@api(U, 1)
def ShowCursor(m, show):
    """Display counter."""
    w = wm(m)
    w.cursor_show += 1 if show else -1
    return w.cursor_show & 0xFFFFFFFF


@api(U, 1)
def SetCursor(m, c):
    """Returns the previous cursor handle."""
    prev = m.services.get("cursor", 0)
    m.services["cursor"] = c
    return prev


@api(U, 1)
def SetCapture(m, hwnd):
    """Sets the capture window; returns the previous one."""
    prev, wm(m).capture = wm(m).capture, hwnd
    return prev


@api(U, 0)
def ReleaseCapture(m):
    """Clears the capture."""
    wm(m).capture = 0
    return 1


@api(U, 2)
def LoadCursorA(m, inst, name):
    """Opaque cursor handle."""
    return 0x30000 + (name & 0xFFFF)


@api(U, 2)
def LoadIconA(m, inst, name):
    """Opaque icon handle."""
    return 0x31000 + (name & 0xFFFF)


@api(U, 6)
def LoadImageA(m, inst, name, typ, cx, cy, flags):
    """Opaque image handle."""
    return 0x32000 + (name & 0xFFFF)


# ----------------------------------------------------------------------------- resources / strings
@api(U, 4)
def LoadStringA(m, inst, ident, buf, n):
    """Real RT_STRING lookup in the module's resources (block ident/16+1, entry ident%16)."""
    from .kernel32 import _resource_entry
    x = module_name_from_handle(m, inst)
    s = b""
    if x is not None and not isinstance(x, str):
        found = _resource_entry(x, 6, (ident >> 4) + 1)
        if found:
            rva, size = found
            data = m.read(x.base + rva, size)
            off = 0
            for i in range(16):
                ln = struct.unpack_from("<H", data, off)[0]
                off += 2
                if i == (ident & 15):
                    s = data[off:off + 2 * ln].decode("utf-16-le").encode("cp1252", "replace")
                    break
                off += 2 * ln
    if n == 0:
        return 0
    s = s[:n - 1]
    m.write(buf, s + b"\0")
    return len(s)


def _new_accel_handle(m):
    n = m.services.get("accel_next", 0) + 1
    m.services["accel_next"] = n
    return 0x34000 + 4 * n


@api(U, 2)
def LoadAcceleratorsA(m, inst, name):
    """Real RT_ACCELERATOR (9) lookup in the module's resources; the 8-byte resource entries
    {fVirt, key, cmd, pad} become a host table of 6-byte ACCEL records (NULL when absent)."""
    from .kernel32 import _res_key, _resource_entry
    x = module_name_from_handle(m, inst)
    if x is None or isinstance(x, str):
        return 0
    found = _resource_entry(x, 9, _res_key(m, name))
    if not found:
        return 0
    rva, size = found
    raw = m.read(x.base + rva, size)
    out = bytearray()
    for i in range(0, size - 7, 8):
        fvirt, key, cmd = struct.unpack_from("<HHH", raw, i)
        out += struct.pack("<BxHH", fvirt & 0x7F, key, cmd)
        if fvirt & 0x80:
            break
    t = m.services.setdefault("accel", {})
    h = _new_accel_handle(m)
    t[h] = bytes(out)
    return h


@api(U, 2)
def CreateAcceleratorTableA(m, p, n):
    """Copies the ACCEL entries into a host-side table."""
    t = m.services.setdefault("accel", {})
    h = _new_accel_handle(m)
    t[h] = m.read(p, 6 * n)
    return h


@api(U, 3)
def CopyAcceleratorTableA(m, h, p, n):
    """Copies back the entries (count when p is NULL)."""
    data = m.services.setdefault("accel", {}).get(h, b"")
    if not p:
        return len(data) // 6
    k = min(n, len(data) // 6)
    m.write(p, data[:6 * k])
    return k


@api(U, 1)
def DestroyAcceleratorTable(m, h):
    """Forgets the table."""
    m.services.setdefault("accel", {}).pop(h, None)
    return 1


# ----------------------------------------------------------------------------- metrics / geometry
@api(U, 1)
def GetSystemMetrics(m, idx):
    """SM_CXSCREEN/SM_CYSCREEN = 640x480, icon sizes 32/16, mouse present, everything else 0."""
    return {0: SCREEN_W, 1: SCREEN_H, 11: 32, 12: 32, 49: 16, 50: 16, 19: 1, 16: SCREEN_W, 17: SCREEN_H,
            78: SCREEN_W, 79: SCREEN_H, 43: 3}.get(idx, 0)


@api(U, 1)
def GetSysColor(m, idx):
    """Grey (0xC0C0C0)."""
    return 0xC0C0C0


@api(U, 4)
def SystemParametersInfoA(m, action, ui, pv, win):
    """SPI_GETWORKAREA (0x30) -> 640x480; other queries leave the buffer and return TRUE."""
    if action == 0x30 and pv:
        _rect(m, pv, [0, 0, SCREEN_W, SCREEN_H])
    return 1


@api(U, 5)
def SetRect(m, p, l, t, r, b):
    """Writes RECT."""
    m.write(p, struct.pack("<4I", l, t, r, b))
    return 1


@api(U, 3)
def OffsetRect(m, p, dx, dy):
    """Moves RECT."""
    l, t, r, b = struct.unpack("<4i", m.read(p, 16))
    dx, dy = _sidx(dx), _sidx(dy)
    m.write(p, struct.pack("<4i", l + dx, t + dy, r + dx, b + dy))
    return 1


@api(U, 3)
def PtInRect(m, p, x, y):
    """Point-in-rectangle test (POINT passed by value)."""
    l, t, r, b = struct.unpack("<4i", m.read(p, 16))
    x, y = _sidx(x), _sidx(y)
    return 1 if l <= x < r and t <= y < b else 0


@api(U, 2)
def EqualRect(m, a, b):
    """Rectangle equality."""
    return 1 if m.read(a, 16) == m.read(b, 16) else 0


@api(U, 3)
def IntersectRect(m, out, a, b):
    """Intersection; FALSE (and an empty rectangle) when disjoint."""
    l1, t1, r1, b1 = struct.unpack("<4i", m.read(a, 16))
    l2, t2, r2, b2 = struct.unpack("<4i", m.read(b, 16))
    l, t, r, bb = max(l1, l2), max(t1, t2), min(r1, r2), min(b1, b2)
    if l >= r or t >= bb:
        m.write(out, bytes(16))
        return 0
    m.write(out, struct.pack("<4i", l, t, r, bb))
    return 1


@api(U, 2)
def DrawFocusRect(m, dc, r):
    """No drawing (headless GDI)."""
    return 1


@api(U, 4)
def DrawEdge(m, dc, r, edge, flags):
    """No drawing (headless GDI)."""
    return 1


@api(U, 5)
def DrawTextA(m, dc, s, n, r, fmt):
    """No drawing; returns a nominal text height of 16."""
    return 16


# ----------------------------------------------------------------------------- wsprintfA
_FMT = re.compile(rb"%([-+ #0]*)(\d+|\*)?(?:\.(\d+))?(hh|h|ll|l|I64)?([diuxXcsSp%])")


def format_printf(m, fmt: bytes, argp: int) -> bytes:
    """C printf formatting of integer/string conversions reading 32-bit varargs from ``argp``."""
    out = bytearray()
    pos = 0
    for mo in _FMT.finditer(fmt):
        out += fmt[pos:mo.start()]
        pos = mo.end()
        flags, width, prec, size, conv = mo.groups()
        if conv == b"%":
            out += b"%"
            continue
        if width == b"*":
            width = str(m.rs32(argp)).encode()
            argp += 4
        if conv in b"diuxXcp":
            v = m.r32(argp)
            argp += 4
            if size in (b"ll", b"I64"):
                v |= m.r32(argp) << 32
                argp += 4
            if conv in b"di":
                v = struct.unpack("<q" if size in (b"ll", b"I64") else "<i",
                                  struct.pack("<Q" if size in (b"ll", b"I64") else "<I", v))[0]
                s = str(v).encode()
            elif conv == b"u":
                s = str(v).encode()
            elif conv in b"xX":
                s = (b"%x" % v) if conv == b"x" else (b"%X" % v)
                if b"#" in (flags or b"") and v:
                    s = (b"0x" if conv == b"x" else b"0X") + s
            elif conv == b"p":
                s = b"%08X" % v
            else:
                s = bytes([v & 0xFF])
            if prec is not None and conv not in b"c":
                s = s.rjust(int(prec), b"0")
        else:
            p = m.r32(argp)
            argp += 4
            s = m.cstr(p) if p else b"(null)"
            if conv == b"S":
                s = m.wstr(p).encode("cp1252", "replace") if p else b"(null)"
            if prec is not None:
                s = s[:int(prec)]
        w = int(width) if width else 0
        f = flags or b""
        if len(s) < w:
            if b"-" in f:
                s = s.ljust(w)
            elif b"0" in f and conv not in b"sSc":
                neg = s.startswith(b"-")
                body = s[1:] if neg else s
                s = (b"-" if neg else b"") + body.rjust(w - neg, b"0")
            else:
                s = s.rjust(w)
        out += s
    out += fmt[pos:]
    return bytes(out)


@api(U, -1)
def wsprintfA(m):
    """Integer/string printf formatting (no floating point, like Windows); output capped at 1024 bytes."""
    esp = m.esp
    buf, fmt = m.r32(esp + 4), m.r32(esp + 8)
    s = format_printf(m, m.cstr(fmt), esp + 12)[:1023]
    m.write(buf, s + b"\0")
    return len(s)
