"""DirectDraw HLE: COM objects whose vtables live in guest memory and point at HLE stubs.

Surfaces are plain guest memory. The primary surface is 640x480 at 32 bpp (what the
game requests); off-screen surfaces get the requested size. ``Lock`` returns the guest
pointer and pitch; ``Unlock`` of the primary is counted as a presented frame and
reported to observers (``machine.services["present_hooks"]``). Palettes record their
entries. No pixel is ever converted by HLE code: Storm's SDrawUnlockSurface writes the
32-bit pixels itself.
"""
from __future__ import annotations

import logging
import struct
from dataclasses import dataclass, field

from . import api

log = logging.getLogger(__name__)
D = "ddraw.dll"
DD_OK = 0
E_NOINTERFACE = 0x80004002
DDERR_INVALIDPARAMS = 0x80070057
DDERR_UNSUPPORTED = 0x80004001

DD_METHODS = ["QueryInterface", "AddRef", "Release", "Compact", "CreateClipper", "CreatePalette", "CreateSurface",
              "DuplicateSurface", "EnumDisplayModes", "EnumSurfaces", "FlipToGDISurface", "GetCaps", "GetDisplayMode",
              "GetFourCCCodes", "GetGDISurface", "GetMonitorFrequency", "GetScanLine", "GetVerticalBlankStatus",
              "Initialize", "RestoreDisplayMode", "SetCooperativeLevel", "SetDisplayMode", "WaitForVerticalBlank"]
SURF_METHODS = ["QueryInterface", "AddRef", "Release", "AddAttachedSurface", "AddOverlayDirtyRect", "Blt", "BltBatch",
                "BltFast", "DeleteAttachedSurface", "EnumAttachedSurfaces", "EnumOverlayZOrders", "Flip",
                "GetAttachedSurface", "GetBltStatus", "GetCaps", "GetClipper", "GetColorKey", "GetDC", "GetFlipStatus",
                "GetOverlayPosition", "GetPalette", "GetPixelFormat", "GetSurfaceDesc", "Initialize", "IsLost", "Lock",
                "ReleaseDC", "Restore", "SetClipper", "SetColorKey", "SetOverlayPosition", "SetPalette", "Unlock",
                "UpdateOverlay", "UpdateOverlayDisplay", "UpdateOverlayZOrder"]
PAL_METHODS = ["QueryInterface", "AddRef", "Release", "GetCaps", "GetEntries", "Initialize", "SetEntries"]
CLIP_METHODS = ["QueryInterface", "AddRef", "Release", "GetClipList", "GetHWnd", "Initialize", "IsClipListChanged",
                "SetClipList", "SetHWnd"]


@dataclass
class ComObject:
    iface: str
    addr: int
    refs: int = 1
    data: dict = field(default_factory=dict)


def com_registry(m) -> dict[int, ComObject]:
    return m.services.setdefault("com_objects", {})


def make_vtable(m, dll, iface, methods):
    key = ("vtable", iface)
    if key in m.services:
        return m.services[key]
    vt = m.alloc(4 * len(methods))
    for i, name in enumerate(methods):
        m.w32(vt + 4 * i, m.stub_for(dll, f"{iface}::{name}"))
    m.services[key] = vt
    return vt


def new_com(m, dll, iface, methods, **data) -> ComObject:
    obj = m.alloc(16)
    m.w32(obj, make_vtable(m, dll, iface, methods))
    o = ComObject(iface, obj, data=data)
    com_registry(m)[obj] = o
    return o


def this(m, p) -> ComObject:
    return com_registry(m)[p]


def method(dll, iface, name, nargs):
    """Register a COM method (``this`` counts as the first stack argument)."""
    return api(dll, nargs + 1, name=f"{iface}::{name}")


def _addref(m, p):
    o = this(m, p)
    o.refs += 1
    return o.refs


def _release(m, p):
    o = this(m, p)
    o.refs = max(0, o.refs - 1)
    return o.refs


for _iface in ("IDirectDraw", "IDirectDrawSurface", "IDirectDrawPalette", "IDirectDrawClipper"):
    method(D, _iface, "AddRef", 0)(lambda m, p: _addref(m, p))
    method(D, _iface, "Release", 0)(lambda m, p: _release(m, p))
    method(D, _iface, "QueryInterface", 2)(lambda m, p, iid, out: (m.w32(out, 0), E_NOINTERFACE)[1])


def dd_state(m):
    return m.services.setdefault("ddraw", {"mode": (640, 480, 32), "primary": None, "unlocks": 0, "flips": 0})


@api(D, 3)
def DirectDrawCreate(m, guid, out, outer):
    """Creates the IDirectDraw object (any GUID, including the emulation-only GUID 2)."""
    o = new_com(m, D, "IDirectDraw", DD_METHODS)
    m.w32(out, o.addr)
    log.info("DirectDrawCreate -> %#x", o.addr)
    return DD_OK


@api(D, 2)
def DirectDrawEnumerateA(m, cb, ctx):
    """Enumerates nothing (DD_OK)."""
    return DD_OK


@method(D, "IDirectDraw", "SetCooperativeLevel", 2)
def dd_SetCooperativeLevel(m, p, hwnd, flags):
    """Records the window and flags; DD_OK."""
    dd_state(m)["coop"] = (hwnd, flags)
    return DD_OK


@method(D, "IDirectDraw", "SetDisplayMode", 3)
def dd_SetDisplayMode(m, p, w, h, bpp):
    """Accepts any mode and records it."""
    dd_state(m)["mode"] = (w, h, bpp)
    log.info("SetDisplayMode %dx%dx%d", w, h, bpp)
    return DD_OK


@method(D, "IDirectDraw", "RestoreDisplayMode", 0)
def dd_RestoreDisplayMode(m, p):
    """DD_OK."""
    return DD_OK


@method(D, "IDirectDraw", "WaitForVerticalBlank", 2)
def dd_WaitForVerticalBlank(m, p, flags, ev):
    """Returns immediately (no time is charged)."""
    return DD_OK


@method(D, "IDirectDraw", "GetVerticalBlankStatus", 1)
def dd_GetVerticalBlankStatus(m, p, out):
    """Always inside the vertical blank (TRUE)."""
    m.w32(out, 1)
    return DD_OK


@method(D, "IDirectDraw", "GetDisplayMode", 1)
def dd_GetDisplayMode(m, p, desc):
    """Current mode as DDSURFACEDESC."""
    w, h, bpp = dd_state(m)["mode"]
    _write_desc(m, desc, w, h, bpp, w * bpp // 8, 0, 0)
    return DD_OK


@method(D, "IDirectDraw", "GetCaps", 2)
def dd_GetCaps(m, p, hw, hel):
    """Zeroed caps (sizes preserved)."""
    for c in (hw, hel):
        if c:
            n = m.r32(c)
            m.write(c + 4, bytes(max(n - 4, 0)))
    return DD_OK


@method(D, "IDirectDraw", "CreatePalette", 4)
def dd_CreatePalette(m, p, flags, entries, out, outer):
    """Palette object initialised from the 256 entries."""
    o = new_com(m, D, "IDirectDrawPalette", PAL_METHODS, entries=bytearray(m.read(entries, 1024)) if entries else
                bytearray(1024))
    m.w32(out, o.addr)
    return DD_OK


@method(D, "IDirectDraw", "CreateClipper", 3)
def dd_CreateClipper(m, p, flags, out, outer):
    """Clipper object (no clipping is performed)."""
    o = new_com(m, D, "IDirectDrawClipper", CLIP_METHODS)
    m.w32(out, o.addr)
    return DD_OK


def _write_desc(m, d, w, h, bpp, pitch, surface, caps):
    # DDSURFACEDESC (108 bytes): size, flags, height, width, pitch, backbuffers, refresh, alpha,
    # reserved, surface (+0x24), colour keys, pixel format (+0x48), caps (+0x68)
    m.write(d, struct.pack("<IIIIiIIIII", 108, 0x100F | 0x1000, h, w, pitch, 0, 0, 0, 0, surface))
    pf = d + 72
    if bpp == 8:
        m.write(pf, struct.pack("<8I", 32, 0x60, 0, 8, 0, 0, 0, 0))
    else:
        m.write(pf, struct.pack("<8I", 32, 0x40, 0, bpp, 0xFF0000, 0xFF00, 0xFF, 0))
    m.w32(d + 104, caps)


@method(D, "IDirectDraw", "CreateSurface", 3)
def dd_CreateSurface(m, p, desc, out, outer):
    """DDSCAPS_PRIMARYSURFACE -> 640x480 surface in the display depth; otherwise the requested
    width/height (8 bpp unless a pixel format is given). Pixels are zeroed guest memory."""
    flags, h, w = struct.unpack("<III", m.read(desc + 4, 12))
    caps = m.r32(desc + 104)
    mw, mh, mbpp = dd_state(m)["mode"]
    primary = bool(caps & 0x200)
    if primary:
        w, h, bpp = mw, mh, mbpp
    else:
        bpp = m.r32(desc + 72 + 12) if flags & 0x1000 else 8
        bpp = bpp or 8
    pitch = w * bpp // 8
    bits = m.alloc(pitch * h)
    o = new_com(m, D, "IDirectDrawSurface", SURF_METHODS, w=w, h=h, bpp=bpp, pitch=pitch, bits=bits,
                primary=primary, caps=caps, palette=0, locks=0)
    if primary:
        dd_state(m)["primary"] = o
    m.w32(out, o.addr)
    log.info("CreateSurface %dx%dx%d primary=%s -> %#x (bits %#x)", w, h, bpp, primary, o.addr, bits)
    return DD_OK


@method(D, "IDirectDrawSurface", "Lock", 4)
def surf_Lock(m, p, rect, desc, flags, ev):
    """Returns the guest pixel pointer (offset by the rectangle) and pitch."""
    o = this(m, p).data
    off = 0
    if rect:
        l, t = struct.unpack("<ii", m.read(rect, 8))
        off = t * o["pitch"] + l * o["bpp"] // 8
    _write_desc(m, desc, o["w"], o["h"], o["bpp"], o["pitch"], o["bits"] + off, o["caps"])
    o["locks"] += 1
    return DD_OK


@method(D, "IDirectDrawSurface", "Unlock", 1)
def surf_Unlock(m, p, ptr):
    """Unlocking the primary surface counts as a presented frame (observers are notified)."""
    o = this(m, p).data
    if o["primary"]:
        st = dd_state(m)
        st["unlocks"] += 1
        for cb in m.services.get("present_hooks", []):
            cb(o)
    return DD_OK


@method(D, "IDirectDrawSurface", "GetSurfaceDesc", 1)
def surf_GetSurfaceDesc(m, p, desc):
    """Size, pitch and pixel format."""
    o = this(m, p).data
    _write_desc(m, desc, o["w"], o["h"], o["bpp"], o["pitch"], 0, o["caps"])
    return DD_OK


@method(D, "IDirectDrawSurface", "GetPixelFormat", 1)
def surf_GetPixelFormat(m, p, pf):
    """8-bit palettised or 32-bit RGB."""
    o = this(m, p).data
    if o["bpp"] == 8:
        m.write(pf, struct.pack("<8I", 32, 0x60, 0, 8, 0, 0, 0, 0))
    else:
        m.write(pf, struct.pack("<8I", 32, 0x40, 0, o["bpp"], 0xFF0000, 0xFF00, 0xFF, 0))
    return DD_OK


@method(D, "IDirectDrawSurface", "GetCaps", 1)
def surf_GetCaps(m, p, caps):
    """Creation caps."""
    m.w32(caps, this(m, p).data["caps"])
    return DD_OK


@method(D, "IDirectDrawSurface", "IsLost", 0)
def surf_IsLost(m, p):
    """Never lost."""
    return DD_OK


@method(D, "IDirectDrawSurface", "Restore", 0)
def surf_Restore(m, p):
    """DD_OK."""
    return DD_OK


@method(D, "IDirectDrawSurface", "SetPalette", 1)
def surf_SetPalette(m, p, pal):
    """Attaches the palette."""
    this(m, p).data["palette"] = pal
    return DD_OK


@method(D, "IDirectDrawSurface", "GetPalette", 1)
def surf_GetPalette(m, p, out):
    """Attached palette or DDERR_NOPALETTEATTACHED."""
    pal = this(m, p).data["palette"]
    m.w32(out, pal)
    return DD_OK if pal else 0x8876023C


@method(D, "IDirectDrawSurface", "SetClipper", 1)
def surf_SetClipper(m, p, c):
    """Accepted."""
    return DD_OK


@method(D, "IDirectDrawSurface", "SetColorKey", 2)
def surf_SetColorKey(m, p, flags, key):
    """Accepted."""
    return DD_OK


@method(D, "IDirectDrawSurface", "Flip", 2)
def surf_Flip(m, p, target, flags):
    """Counted; no buffer exchange (the game uses no flipping chain)."""
    dd_state(m)["flips"] += 1
    return DD_OK


@method(D, "IDirectDrawSurface", "GetFlipStatus", 1)
def surf_GetFlipStatus(m, p, flags):
    """DD_OK."""
    return DD_OK


@method(D, "IDirectDrawSurface", "GetBltStatus", 1)
def surf_GetBltStatus(m, p, flags):
    """DD_OK."""
    return DD_OK


@method(D, "IDirectDrawSurface", "Blt", 5)
def surf_Blt(m, p, dst_rect, src, src_rect, flags, fx):
    """Colour fill (DDBLT_COLORFILL) and same-format rectangle copies are performed in guest memory."""
    o = this(m, p).data
    dl, dt, dr, db = struct.unpack("<4i", m.read(dst_rect, 16)) if dst_rect else (0, 0, o["w"], o["h"])
    bpp = o["bpp"] // 8
    if flags & 0x400:  # COLORFILL
        color = m.r32(fx + 80) if fx else 0
        px = struct.pack("<I", color)[:bpp]
        row = px * (dr - dl)
        for y in range(dt, db):
            m.write(o["bits"] + y * o["pitch"] + dl * bpp, row)
        return DD_OK
    if src:
        s = this(m, src).data
        sl, st, sr, sb = struct.unpack("<4i", m.read(src_rect, 16)) if src_rect else (0, 0, s["w"], s["h"])
        if s["bpp"] == o["bpp"] and sr - sl == dr - dl and sb - st == db - dt:
            for y in range(db - dt):
                m.write(o["bits"] + (dt + y) * o["pitch"] + dl * bpp,
                        m.read(s["bits"] + (st + y) * s["pitch"] + sl * bpp, (dr - dl) * bpp))
            return DD_OK
    log.warning("unsupported Blt flags=%#x", flags)
    return DD_OK


@method(D, "IDirectDrawSurface", "BltFast", 5)
def surf_BltFast(m, p, x, y, src, src_rect, flags):
    """Same-format rectangle copy."""
    o = this(m, p).data
    s = this(m, src).data
    sl, st, sr, sb = struct.unpack("<4i", m.read(src_rect, 16)) if src_rect else (0, 0, s["w"], s["h"])
    bpp = o["bpp"] // 8
    for row in range(sb - st):
        m.write(o["bits"] + (y + row) * o["pitch"] + x * bpp,
                m.read(s["bits"] + (st + row) * s["pitch"] + sl * bpp, (sr - sl) * bpp))
    return DD_OK


@method(D, "IDirectDrawSurface", "GetDC", 1)
def surf_GetDC(m, p, out):
    """Opaque DC."""
    m.w32(out, 0x20002)
    return DD_OK


@method(D, "IDirectDrawSurface", "ReleaseDC", 1)
def surf_ReleaseDC(m, p, dc):
    """DD_OK."""
    return DD_OK


@method(D, "IDirectDrawSurface", "GetAttachedSurface", 2)
def surf_GetAttachedSurface(m, p, caps, out):
    """DDERR_NOTFOUND (no attached surfaces)."""
    m.w32(out, 0)
    return 0x887600FF


@method(D, "IDirectDrawPalette", "SetEntries", 4)
def pal_SetEntries(m, p, flags, start, n, entries):
    """Stores PALETTEENTRY values."""
    e = this(m, p).data["entries"]
    e[4 * start:4 * (start + n)] = m.read(entries, 4 * n)
    return DD_OK


@method(D, "IDirectDrawPalette", "GetEntries", 4)
def pal_GetEntries(m, p, flags, start, n, entries):
    """Returns stored entries."""
    m.write(entries, bytes(this(m, p).data["entries"][4 * start:4 * (start + n)]))
    return DD_OK


@method(D, "IDirectDrawClipper", "SetHWnd", 2)
def clip_SetHWnd(m, p, flags, hwnd):
    """Accepted."""
    return DD_OK
