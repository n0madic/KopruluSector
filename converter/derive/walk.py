"""Native walk raster (`0x48b1c0`): one byte per 8x8-pixel mini-cell.

Contract: docs/research/native-region-generation.md ("Walk raster ..."). The original
allocates a fixed 1024x1024 buffer (row stride 1024); this module returns only the map area,
`width*4` bytes per row and `height*4` rows, which is what `0x48b20c`/`0x48b2b8` write.

Byte value:
- `0x80` (bit 7): blocked. VF4 bit 0 clear (`0x48ad80`), or a cell under the bottom UI:
  the last 8 rows block 20 cells at each side, the last 4 rows block the whole width;
- otherwise bits 1/2/4/8 mark a blocked (or off-map) neighbour at top/right/bottom/left.

The megatile ID's high bit is ignored (`& 0x7fff`). The result equals the native output
hashes of all 68 campaign CHKs (converter/tests/test_derive.py).
"""
from __future__ import annotations

import functools
import struct

ALGORITHM = b"WLK1"
VERSION = 1

BLOCKED = 0x80
UI_ROWS = 8  # bottom rows with blocked side margins
UI_FULL_ROWS = 4  # bottom rows blocked entirely
UI_MARGIN = 20  # cells blocked at each side of the UI rows


@functools.lru_cache(maxsize=8)
def minitile_rows(vf4: bytes) -> tuple[tuple[bytes, bytes, bytes, bytes], ...]:
    """Per megatile, its four 4-byte walk rows (`0x48ad80`: VF4 bit 0 set -> 0, else 0x80)."""
    out = []
    for words in struct.iter_unpack("<16H", vf4):
        cells = bytes(0 if w & 1 else BLOCKED for w in words)
        out.append((cells[0:4], cells[4:8], cells[8:12], cells[12:16]))
    return tuple(out)


def walk_grid(width: int, height: int, megatile_ids: list[int], vf4: bytes) -> bytes:
    """The walk raster of the map area, row-major, `width*4` bytes per row, `height*4` rows."""
    rows_by_mega = minitile_rows(vf4)
    ww, wh = width * 4, height * 4
    rows: list[bytearray] = []
    for ty in range(height):
        tiles = [rows_by_mega[i & 0x7FFF] for i in megatile_ids[ty * width:(ty + 1) * width]]
        for sy in range(4):
            rows.append(bytearray(b"".join(t[sy] for t in tiles)))
    for y in range(max(0, wh - UI_ROWS), wh):
        row = rows[y]
        if y >= wh - UI_FULL_ROWS:
            row[:] = bytes([BLOCKED]) * ww
        else:
            row[:UI_MARGIN] = bytes([BLOCKED]) * min(UI_MARGIN, ww)
            lo = max(0, ww - UI_MARGIN)
            row[lo:] = bytes([BLOCKED]) * (ww - lo)
    # Direction bits on whole rows at once: shift a "1 per blocked cell" big integer by
    # whole bytes to look at the neighbours, as the independent four-neighbour definition.
    ones = int.from_bytes(b"\x01" * ww, "little")
    first, last = 1, 1 << (8 * (ww - 1))
    blocked = [int.from_bytes(r, "little") >> 7 for r in rows]
    out = bytearray()
    for y in range(wh):
        b = blocked[y]
        free = ones ^ b
        above = ones if y == 0 else blocked[y - 1]
        below = ones if y == wh - 1 else blocked[y + 1]
        right = (b >> 8) | last
        left = ((b << 8) & ones) | first
        value = (b << 7) | (free & above) | ((free & right) << 1) | ((free & below) << 2) | ((free & left) << 3)
        out += value.to_bytes(ww, "little")
    return bytes(out)
