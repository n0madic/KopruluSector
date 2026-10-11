"""GDI32 HLE: headless GDI.

Nothing is rasterised: the game draws into its own 8-bit bitmap and presents through
DirectDraw. GDI objects are opaque handles; text measurement uses a nominal 8x16 font;
DIB sections get real guest pixel memory so callers can write into them.
"""
from __future__ import annotations

import struct

from . import api

G = "gdi32.dll"


def _objects(m):
    return m.services.setdefault("gdi_objects", {})


def _new(m, kind, **kw):
    objs = _objects(m)
    h = 0x40000 + 4 * (len(objs) + 1)
    objs[h] = dict(kind=kind, **kw)
    return h


@api(G, 1)
def CreateCompatibleDC(m, dc):
    """New memory DC handle."""
    return _new(m, "dc")


@api(G, 1)
def DeleteDC(m, dc):
    """TRUE."""
    _objects(m).pop(dc, None)
    return 1


@api(G, 1)
def DeleteObject(m, h):
    """TRUE."""
    _objects(m).pop(h, None)
    return 1


@api(G, 2)
def SelectObject(m, dc, h):
    """Returns the previously selected object of the same kind (opaque)."""
    d = _objects(m).setdefault(dc, {"kind": "dc"})
    kind = _objects(m).get(h, {}).get("kind", "obj")
    prev = d.get(kind, 0x40000)
    d[kind] = h
    return prev


@api(G, 2)
def GetCurrentObject(m, dc, typ):
    """Opaque handle."""
    return _objects(m).get(dc, {}).get("bitmap", 0x40000)


@api(G, 1)
def GetStockObject(m, i):
    """Opaque stock handle."""
    return 0x41000 + i


@api(G, 2)
def GetDeviceCaps(m, dc, idx):
    """640x480 display, 32 bits per pixel, 1 plane, 96 dpi, no palette device."""
    return {8: 640, 10: 480, 12: 32, 14: 1, 24: 0xFFFFFFFF, 88: 96, 90: 96, 104: 0, 38: 0x7E99, 116: 60,
            4: 320, 6: 240}.get(idx, 0)


@api(G, 14)
def CreateFontA(m, *a):
    """Opaque font handle."""
    return _new(m, "font")


@api(G, 1)
def CreateFontIndirectA(m, p):
    """Opaque font handle."""
    return _new(m, "font")


@api(G, 2)
def GetTextMetricsA(m, dc, p):
    """Nominal font: height 16, ascent 13, descent 3, average width 8, max width 8."""
    m.write(p, struct.pack("<7i", 16, 13, 3, 0, 0, 8, 8) + bytes(56 - 28))
    return 1


@api(G, 4)
def GetTextExtentPoint32A(m, dc, s, n, size):
    """8 pixels per character, 16 pixels high."""
    m.write(size, struct.pack("<2i", 8 * n, 16))
    return 1


@api(G, 7)
def GetTextExtentExPointA(m, dc, s, n, maxw, fit, dx, size):
    """Fixed 8-pixel advance."""
    if fit:
        m.w32(fit, min(n, maxw // 8) if maxw else n)
    if dx:
        for i in range(n):
            m.w32(dx + 4 * i, 8 * (i + 1))
    m.write(size, struct.pack("<2i", 8 * n, 16))
    return 1


@api(G, 4)
def GetCharABCWidthsA(m, dc, first, last, p):
    """A=0, B=8, C=0 for every character."""
    m.write(p, struct.pack("<iIi", 0, 8, 0) * (last - first + 1))
    return 1


@api(G, 5)
def TextOutA(m, dc, x, y, s, n):
    """No drawing."""
    return 1


@api(G, 8)
def ExtTextOutA(m, dc, x, y, opts, r, s, n, dx):
    """No drawing."""
    return 1


@api(G, 2)
def SetTextColor(m, dc, c):
    """Returns black."""
    return 0


@api(G, 2)
def SetBkColor(m, dc, c):
    """Returns white."""
    return 0xFFFFFF


@api(G, 2)
def SetBkMode(m, dc, mode):
    """Returns OPAQUE."""
    return 2


@api(G, 2)
def SetTextAlign(m, dc, a):
    """Returns 0."""
    return 0


@api(G, 4)
def MoveToEx(m, dc, x, y, p):
    """No drawing."""
    if p:
        m.write(p, bytes(8))
    return 1


@api(G, 5)
def Rectangle(m, dc, l, t, r, b):
    """No drawing."""
    return 1


@api(G, 3)
def GetObjectA(m, h, n, p):
    """BITMAP for DIB sections/bitmaps (width, height, 8 bpp); 0 otherwise."""
    o = _objects(m).get(h)
    if o and o.get("kind") == "bitmap" and p:
        w, hh, bpp, bits = o["w"], o["h"], o["bpp"], o.get("bits", 0)
        m.write(p, struct.pack("<iiiHHI", 0, w, hh, ((w * bpp + 31) // 32) * 4, 1, bpp, bits)[:n])
        return min(n, 24)
    return 0


@api(G, 6)
def CreateDIBSection(m, dc, bmi, usage, bits_out, section, offset):
    """Allocates zeroed guest pixel memory from the BITMAPINFOHEADER size."""
    size, w, h, planes, bpp = struct.unpack("<IiiHH", m.read(bmi, 16))
    stride = ((w * bpp + 31) // 32) * 4
    p = m.alloc(stride * abs(h))
    if bits_out:
        m.w32(bits_out, p)
    return _new(m, "bitmap", w=w, h=abs(h), bpp=bpp, bits=p)


@api(G, 3)
def CreateCompatibleBitmap(m, dc, w, h):
    """Opaque 32 bpp bitmap."""
    return _new(m, "bitmap", w=w, h=h, bpp=32)


@api(G, 6)
def CreateDIBitmap(m, dc, hdr, init, bits, bmi, usage):
    """Opaque bitmap with the header's size."""
    size, w, h = struct.unpack("<Iii", m.read(hdr, 12))
    return _new(m, "bitmap", w=w, h=abs(h), bpp=8)


@api(G, 3)
def GetBitmapBits(m, h, n, p):
    """Zero bytes."""
    m.write(p, bytes(n))
    return n


@api(G, 7)
def GetDIBits(m, dc, h, start, lines, bits, bmi, usage):
    """Reports the lines; pixels are zero."""
    return lines


@api(G, 12)
def SetDIBitsToDevice(m, *a):
    """No drawing; returns the line count."""
    return a[6]


@api(G, 9)
def BitBlt(m, *a):
    """No drawing."""
    return 1


@api(G, 11)
def StretchBlt(m, *a):
    """No drawing."""
    return 1


@api(G, 13)
def StretchDIBits(m, *a):
    """No drawing."""
    return a[8] if len(a) > 8 else 0


@api(G, 6)
def PatBlt(m, *a):
    """No drawing."""
    return 1


@api(G, 2)
def SetStretchBltMode(m, dc, mode):
    """Returns 1."""
    return 1


@api(G, 3)
def GetPixel(m, dc, x, y):
    """Black."""
    return 0


@api(G, 4)
def SetPixel(m, dc, x, y, c):
    """No drawing."""
    return c


@api(G, 4)
def CreateRectRgn(m, l, t, r, b):
    """Opaque region."""
    return _new(m, "region", rect=(l, t, r, b))


@api(G, 3)
def GetRegionData(m, rgn, n, p):
    """0 (empty)."""
    return 0


@api(G, 4)
def GetPaletteEntries(m, pal, start, n, p):
    """Zeroed entries."""
    m.write(p, bytes(4 * n))
    return n


@api(G, 4)
def GetSystemPaletteEntries(m, dc, start, n, p):
    """Zeroed entries."""
    if p:
        m.write(p, bytes(4 * n))
    return n


@api(G, 1)
def GetSystemPaletteUse(m, dc):
    """SYSPAL_STATIC."""
    return 1


@api(G, 1)
def CreatePalette(m, p):
    """Opaque palette."""
    return _new(m, "palette")


@api(G, 3)
def SelectPalette(m, dc, pal, bg):
    """Opaque previous palette."""
    return 0x41000 + 15


@api(G, 1)
def RealizePalette(m, dc):
    """0 entries changed."""
    return 0
