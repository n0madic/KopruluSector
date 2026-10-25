"""Screen captures for the panel oracle of plan phase 9 (docs/phase9.md): the composed 640x480 screen.

PresentFrame 0x4c5360 composites and flips the screen layers (0x4caf10, atlas P43/P44): the dialogs (layer 2, the
console's panels, menus and glue screens) and the tooltip are drawn into the screen bitmap 0x6d0b88, the cursor
(layer 0) is drawn over a saved background for the flip and taken off again before 0x4caf10 returns. A capture is
taken there (0x4c546b, only when the call at 0x4c5466 ran): the screen without the cursor, the palette, where the
cursor was (layer 0's rectangle, 0x6d0b90), the tooltip's layer (layer 1 0x6d0ba4: its rectangle, and the visible flag
0x5c999c) and the open dialogs (the list 0x6ce6d8, topmost first: their BIN and
record index as uilog.py names them, rectangle and flags), so that a panel's crop is found wherever it is.

In game (mode 3) the first composite at or after each listed frame is taken; `glue_ms` (milliseconds, or a list of
them) takes, outside the game, the first composite at least that many virtual milliseconds after each change of the
glue state or of its topmost dialog (the screen once the slide-in animations have settled; the header's after_ms says
when).
With `glue_ms`, a modeless dialog over the game (OpenModelessDialog 0x46ecc0, e.g. the result dialogs) is taken
MODELESS_MS after it opens, as <out>/modeless-<k>.cap.
File: <out>/screen<frame>.cap (in game) or <out>/glue<state>-<k>.cap: JSON header line, then the blobs it lists:
"pixels" (zlib-compressed 640x480 indices) and "palette" (256 x 4 bytes); a modeless capture adds "under" (the screen
bitmap 0x6d0b88 before the dialog's first composite, compressed likewise: the game view the dialog first blends
over. A translucent dialog (flag 0x10000000) is blended into the screen bitmap at every repaint of a rectangle, over
what the last repaint left where the game view was not redrawn, and the surface shows the blend of the view's last
redraw: the later views are not kept).
"""
from __future__ import annotations

import json
import logging
import struct
import zlib
from pathlib import Path

from . import render, uilog

log = logging.getLogger(__name__)

COMPOSITE_CALL, PRESENTED = 0x4C5466, 0x4C546B
# UnlockPrimarySurface 0x4cb6d0 (ECX = the lock struct: +8 pitch, +0xc the locked pixels of Storm's surface 0, which
# LockPrimarySurface 0x4cb560 filled): the console's dialogs are blitted straight into that surface (0x6cfbdc), and
# the game view reaches it from the screen bitmap through the HUD mask (0x4cb4f0, P44), so it holds the whole screen
UNLOCK_PRIMARY = 0x4CB6D0
DIALOG_LIST = 0x6CE6D8
MODELESS = 0x672578  # OpenModelessDialog 0x46ecc0's dialog (0 none)
OPEN_MODELESS = 0x46ECC0  # OpenModelessDialog: each opened dialog is captured once
MODELESS_MS = (1000,)  # before chain.py answers the victory dialog (1.5 s)
LAYER0 = 0x6D0B90  # {u8 enabled, u8 flags, s16 x, y, w, h, ...}
LAYER1, TOOLTIP_VISIBLE = 0x6D0BA4, 0x5C999C  # the tooltip's layer (InitTooltip 0x419220) and its visible flag
MAX_DIALOGS = 64


def _write(path: Path, header: dict, blobs: list[tuple[str, bytes]]) -> None:
    header["blobs"], body, offset = [], [], 0
    for name, data in blobs:
        header["blobs"].append([name, offset, len(data)])
        body.append(data)
        offset += len(data)
    with open(path, "wb") as f:
        f.write(json.dumps(header).encode() + b"\n")
        for data in body:
            f.write(data)


def install(session, out: Path, frames: tuple[int, ...] = (), glue_ms: int = 0, names=None,
            debug_png: bool = False, modeless: bool | None = None) -> dict:
    """`names(pointer)` gives a dialog's [BIN, index] (the input log's dialog table), else its address is kept;
    `modeless` captures each modeless dialog over the game (default: with the glue captures)."""
    from . import game  # the guest's globals (needs unicorn; reading captures does not)
    m = session.m
    out.mkdir(parents=True, exist_ok=True)
    pending = sorted(set(frames))
    st = {"composited": False, "glue": None, "glue_since": 0, "glue_count": {}, "glue_next": 0,
          "modeless": 0, "modeless_since": 0, "modeless_next": 0, "modeless_count": 0, "opened": 0}
    offsets = sorted(glue_ms) if isinstance(glue_ms, (tuple, list)) else ((glue_ms,) if glue_ms else ())
    capture_modeless = bool(offsets) if modeless is None else modeless

    def dialogs() -> list[dict]:
        found, p = [], m.r32(DIALOG_LIST)
        while p and len(found) < MAX_DIALOGS:
            left, top, right, bottom = struct.unpack("<4h", m.read(p + 4, 8))
            flags = m.r32(p + 0x18)
            found.append({"ref": names(p) if names else f"{p:#x}", "rect": [left, top, right, bottom],
                          "flags": flags})
            p = m.r32(p)
        return found

    def on_unlock():
        lock = m.reg(X.UC_X86_REG_ECX)
        st["surface"] = (m.r32(lock + 0xC), m.r32(lock + 8))


    def capture(path: Path, header: dict, under: bool = False) -> None:
        w, h, pixels, palette = render.screen_bitmap(m)
        blobs = [("under", zlib.compress(st["under"], 6))] if under and st.get("under") else []
        if st.get("surface"):
            ptr, pitch = st["surface"]
            header["surface"] = [f"{ptr:#x}", pitch]
            pixels = b"".join(m.read(ptr + y * pitch, w) for y in range(h))
        enabled, flags, x, y, cw, ch = struct.unpack("<BBhhhh", m.read(LAYER0, 10))
        t_enabled, _, tx, ty, tw, th = struct.unpack("<BBhhhh", m.read(LAYER1, 10))
        header.update(pixels_at=f"{m.r32(render.SCREEN_BITMAP + 4):#x}", size=[w, h], cursor={"enabled": enabled, "rect": [x, y, cw, ch]}, dialogs=dialogs(),
                      tooltip={"visible": m.r32(TOOLTIP_VISIBLE), "enabled": t_enabled, "rect": [tx, ty, tw, th]},
                      mode=m.r16(uilog.GAME_MODE), glue=m.r32(uilog.GLUE_STATE), clock_ms=m.clock_us // 1000)
        _write(path, header, [("pixels", zlib.compress(pixels, 6)), ("palette", palette)] + blobs)
        if debug_png:
            render.save_primary_surface(m, path.with_suffix(".presented.png"))

    def on_composite():
        st["composited"] = True
        modeless = m.r32(MODELESS) if m.r16(uilog.GAME_MODE) == 3 else 0
        if modeless and st.get("under_of") != st["opened"]:  # the game view before the dialog's first composite
            st["under"], st["under_of"] = render.screen_bitmap(m)[2], st["opened"]

    def on_presented():
        if not st["composited"]:
            return
        st["composited"] = False
        mode = m.r16(uilog.GAME_MODE)
        if mode == 3:
            f = m.r32(game.FRAME_COUNTER)
            if pending and f >= pending[0]:
                while pending and pending[0] <= f:
                    wanted = pending.pop(0)
                capture(out / f"screen{wanted:05d}.cap", {"frame": wanted, "at_frame": f})
                log.info("screen captured at frame %d", f)
            modeless = m.r32(MODELESS)
            if modeless and capture_modeless:  # a modeless dialog over the game, MODELESS_MS after it opens
                if st["opened"] != st["modeless"]:  # a dialog opened since (a menu reopens at the same address)
                    st.update(modeless=st["opened"], modeless_since=m.clock_us, modeless_next=0)
                while st["modeless_next"] < len(MODELESS_MS) and \
                        m.clock_us - st["modeless_since"] >= MODELESS_MS[st["modeless_next"]] * 1000:
                    st["modeless_next"] += 1
                    k = st["modeless_count"]
                    st["modeless_count"] = k + 1
                    capture(out / f"modeless-{k}.cap", {"frame": f, "after_ms": (m.clock_us - st["modeless_since"]) // 1000},
                            under=True)
                    log.info("modeless dialog captured at frame %d", f)
            else:
                st["modeless"] = 0
            return
        if not offsets:
            return
        g = m.r32(uilog.GLUE_STATE)
        key = (g, m.r32(DIALOG_LIST))  # a screen: the glue state with its topmost dialog (a ready room after its text)
        if key != st["glue"]:
            st.update(glue=key, glue_since=m.clock_us, glue_next=0)
        while st["glue_next"] < len(offsets) and m.clock_us - st["glue_since"] >= offsets[st["glue_next"]] * 1000:
            st["glue_next"] += 1
            k = st["glue_count"].get(g, 0)
            st["glue_count"][g] = k + 1
            capture(out / f"glue{g:02d}-{k}.cap", {"glue_state": g, "after_ms": (m.clock_us - st["glue_since"]) // 1000})
            log.info("glue screen %d captured", g)

    def on_open_modeless():
        st["opened"] += 1

    from unicorn import x86_const as X
    session.hook(OPEN_MODELESS, on_open_modeless)
    session.hook(UNLOCK_PRIMARY, on_unlock)
    session.hook(COMPOSITE_CALL, on_composite)
    session.hook(PRESENTED, on_presented)
    return st


class ScreenCapture:
    """A capture file read back: the header, the 640x480 indices and the palette."""

    def __init__(self, path: Path) -> None:
        raw = Path(path).read_bytes()
        nl = raw.index(b"\n")
        self.header = json.loads(raw[:nl])
        body = raw[nl + 1:]
        blobs = {name: body[off:off + size] for name, off, size in self.header["blobs"]}
        self.pixels = zlib.decompress(blobs["pixels"])
        self.palette = blobs["palette"]
        self.under = zlib.decompress(blobs["under"]) if "under" in blobs else None
        self.width, self.height = self.header["size"]

    def crop(self, left: int, top: int, right: int, bottom: int) -> bytes:
        """The indices of an inclusive rectangle, row by row."""
        return b"".join(self.pixels[y * self.width + left:y * self.width + right + 1] for y in range(top, bottom + 1))
