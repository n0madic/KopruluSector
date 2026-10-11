"""Frame capture: PNG encoding of the game screen bitmap and of the presented surface."""
from __future__ import annotations

import struct
import zlib
from pathlib import Path

SCREEN_BITMAP = 0x6D0B88   # {u16 width, u16 height, u32 pixels} main 640x480 8-bit bitmap (P45)
GAME_PALETTE = 0x6D2388    # current 256-entry palette, 4 bytes per entry (r, g, b, flags) (P44)


def png_rgb(width: int, height: int, rgb: bytes) -> bytes:
    raw = b"".join(b"\0" + rgb[y * width * 3:(y + 1) * width * 3] for y in range(height))

    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


def screen_bitmap(m) -> tuple[int, int, bytes, bytes]:
    """(width, height, 8-bit pixels, 1024-byte palette) of the game's screen bitmap."""
    w, h, ptr = struct.unpack("<HHI", m.read(SCREEN_BITMAP, 8))
    pixels = m.read(ptr, w * h) if ptr and w and h else b""
    return w, h, pixels, m.read(GAME_PALETTE, 1024)


def save_screen_bitmap(m, path: Path) -> bool:
    w, h, pixels, pal = screen_bitmap(m)
    if not pixels:
        return False
    lut = [pal[4 * i:4 * i + 3] for i in range(256)]
    rgb = b"".join(lut[p] for p in pixels)
    Path(path).write_bytes(png_rgb(w, h, rgb))
    return True


def save_primary_surface(m, path: Path) -> bool:
    """The 32-bit DirectDraw primary surface as presented by Storm's SDrawUnlockSurface."""
    st = m.services.get("ddraw", {})
    prim = st.get("primary")
    if prim is None:
        return False
    d = prim.data
    w, h, pitch = d["w"], d["h"], d["pitch"]
    data = m.read(d["bits"], pitch * h)
    rgb = bytearray(w * h * 3)
    for y in range(h):
        row = data[y * pitch:y * pitch + w * 4]
        # pixels are 0x00RRGGBB little-endian: bytes B, G, R, 0
        rgb[y * w * 3:(y + 1) * w * 3] = bytes(b for i in range(0, len(row), 4) for b in (row[i + 2], row[i + 1], row[i]))
    Path(path).write_bytes(png_rgb(w, h, bytes(rgb)))
    return True
