"""Native terrain collision contours (`0x48af10` -> `0x48a7f0`), sorted by native `qsort`.

Contract: docs/research/native-contours.md. Traversal (`0x48a88e`..`0x48acc8` with scanner
`0x48af50` and the lookup tables at `0x508ee8`..`0x508f59`) follows the boundary of blocked
walk cells and appends 8-byte records to four arrays (top, right, bottom, left):
`int16 axis, int16 start, int16 end, u8 direction, u8 info`. Each array is then sorted by the
original MSVC CRT `qsort` (`0x4d4490`, shortsort `0x4d47e0`) with comparator `0x48ad40`
(signed axis, then signed start). That qsort is not stable; it is reproduced exactly so equal
keys keep the native order.

Capacity: 5000 records per direction (`0x48a7f0`); before each append, if any array is full,
`0x48b040` grows all four by 2500. The storage is then shrunk to the counts (`0x48b600`), but
the capacity words keep the grown value; `build` reports them.

Checked against the native sorted-contour hashes of all 68 campaign CHKs and 30 synthetic
cases (converter/tests/test_derive.py).
"""
from __future__ import annotations

import re
import struct
from dataclasses import dataclass

ALGORITHM = b"CNT1"
VERSION = 1

RECORD = struct.Struct("<3h2B")
INITIAL_CAPACITY = 5000
CAPACITY_STEP = 2500
QSORT_CUTOFF = 8  # partitions of at most this many elements go to shortsort

# 0x508f08 (start) and 0x508f28 (end): pixel offsets (x, y) from cell*8, by direction + 4*mode.
START_OFFSETS = (-1, -1, 8, -1, 8, 8, -1, 8, 0, -1, 8, 0, 7, 8, -1, 7)
END_OFFSETS = (8, -1, 8, 8, -1, 8, -1, -1, 7, -1, 8, 7, 0, 8, -1, 0)
STEPS = ((0, -1), (1, 0), (0, 1), (-1, 0))  # 0x508f50 by direction: top, right, bottom, left
DIAGONALS = ((1, -1), (1, 1), (-1, 1), (-1, -1))  # 0x508f58
_PENDING = re.compile(rb"[\x01-\x7f\x81-\xff]")  # a direction bit left (`& 0x7f7f7f7f`)
_START = re.compile(rb"[\x01-\x7f]")  # signed byte >= 1


@dataclass
class Contours:
    arrays: list[list[tuple[int, int, int, int, int]]]  # top, right, bottom, left, sorted
    capacities: list[int]  # native capacity words after construction

    def counts(self) -> list[int]:
        return [len(a) for a in self.arrays]


def build(width: int, height: int, walk: bytes) -> Contours:
    """Contours of a walk raster (`walk.walk_grid`, stride `width*4`)."""
    arrays, capacity, _ = trace(width * 4, height * 4, walk)
    return Contours([native_qsort(a) for a in arrays], [capacity] * 4)


def trace(ww: int, wh: int, walk: bytes) -> tuple[list[list[tuple[int, int, int, int, int]]], int, bytes]:
    """Unsorted records in native append order, the resulting (common) capacity, and the
    walk raster afterwards (traversal consumes every direction bit)."""
    g = bytearray(walk)
    out: list[list[tuple[int, int, int, int, int]]] = [[], [], [], []]
    capacity = INITIAL_CAPACITY
    longest = 0

    def free_dir(x: int, y: int, d: int) -> bool:
        """Direction bit clear (cells outside the map read as zero)."""
        if 0 <= x < ww and 0 <= y < wh:
            return not g[y * ww + x] & (1 << d)
        return True

    pos = 0
    while True:
        # 0x48af50: from the 4-cell group of the last start, the first group (wrapping) with
        # a byte whose low 7 bits are set; then its first cell with a signed value >= 1.
        group = pos & ~3
        m = _PENDING.search(g, group) or _PENDING.search(g, 0, group)
        if m is None:
            break
        pos = _START.search(g, m.start() & ~3).start()
        y, x = divmod(pos, ww)
        if free_dir(x, y, 0) and free_dir(x, y, 1) and not free_dir(x, y, 2):
            x, y = x - 1, y + 1
        mode = 0 if not free_dir(x, y, 0) and not free_dir(x, y, 3) else 1
        d = next(d for d in range(4) if not free_dir(x, y, d))
        k = d + 4 * mode
        cx, cy = x * 8 + START_OFFSETS[k * 2], y * 8 + START_OFFSETS[k * 2 + 1]
        start = (cx, cy)
        g[y * ww + x] ^= 1 << d
        while True:
            nd = (d + 1) & 3
            dx, dy = STEPS[nd]
            g[y * ww + x] ^= 1 << d
            next_walk = 0
            if free_dir(x, y, nd):
                next_walk = 1
                while not free_dir(x + dx, y + dy, d):
                    x += dx
                    y += dy
                    g[y * ww + x] ^= 1 << d
                    if not free_dir(x, y, nd):
                        next_walk = 0
                        break
            k = d + 4 * next_walk
            ex, ey = x * 8 + END_OFFSETS[k * 2], y * 8 + END_OFFSETS[k * 2 + 1]
            f0 = (d ^ 2 * mode ^ (~(2 * d) & 3)) & 3
            f1 = (d ^ 2 * (next_walk ^ (d & 1)) ^ 1) & 3
            if longest == capacity:  # 0x48b040: some array is full before this append
                capacity += CAPACITY_STEP
            if d == 0:
                rec = (cy, cx, ex, d, f0 | f1 << 2 | mode << 4 | next_walk << 5)
            elif d == 1:
                rec = (cx, cy, ey, d, f0 | f1 << 2 | mode << 4 | next_walk << 5)
            elif d == 2:
                rec = (cy, ex, cx, d, f1 | f0 << 2 | next_walk << 4 | mode << 5)
            else:
                rec = (cx, ey, cy, d, f1 | f0 << 2 | next_walk << 4 | mode << 5)
            out[d].append(rec)
            longest = max(longest, len(out[d]))
            if not next_walk:
                d = nd
            else:
                x += DIAGONALS[d][0]
                y += DIAGONALS[d][1]
                d = (d - 1) & 3
            cx, cy = ex, ey
            mode = next_walk
            if (cx, cy) == start:
                break
        g[y * ww + x] ^= 1 << d
    return out, capacity, bytes(g)


def native_qsort(items: list) -> list:
    """MSVC CRT `qsort` (`0x4d4490`) with comparator `0x48ad40` on (axis, start), signed.

    Same partitioning, median-of-three, swaps and explicit stack as the original, so the
    resulting order of equal keys is the native one.
    """
    a = list(items)
    n = len(a)
    if n < 2:
        return a
    keys = [(r[0] << 16) + r[1] for r in a]  # signed 16-bit axis/start: order-preserving key
    if len(set(keys)) == n:  # distinct keys: the sorted order is unique, whatever the algorithm
        return [r for _, r in sorted(zip(keys, a), key=lambda kr: kr[0])]

    def swap(i: int, j: int) -> None:
        a[i], a[j] = a[j], a[i]
        keys[i], keys[j] = keys[j], keys[i]

    lo_stack: list[int] = []
    hi_stack: list[int] = []
    lo, hi = 0, n - 1
    while True:
        size = hi - lo + 1
        if size <= QSORT_CUTOFF:
            # shortsort: move the maximum (first of equals kept, later strictly greater wins) to hi.
            h = hi
            while h > lo:
                m = lo
                for p in range(lo + 1, h + 1):
                    if keys[p] > keys[m]:
                        m = p
                if m != h:
                    swap(m, h)
                h -= 1
        else:
            mid = lo + size // 2
            if keys[lo] > keys[mid]:
                swap(lo, mid)
            if keys[lo] > keys[hi]:
                swap(lo, hi)
            if keys[mid] > keys[hi]:
                swap(mid, hi)
            loguy, higuy = lo, hi
            while True:
                if mid > loguy:
                    loguy += 1
                    while loguy < mid and keys[loguy] <= keys[mid]:
                        loguy += 1
                if mid <= loguy:
                    loguy += 1
                    while loguy <= hi and keys[loguy] <= keys[mid]:
                        loguy += 1
                higuy -= 1
                while higuy > mid and keys[higuy] > keys[mid]:
                    higuy -= 1
                if higuy < loguy:
                    break
                swap(loguy, higuy)
                if mid == higuy:
                    mid = loguy
            higuy += 1
            if mid < higuy:
                higuy -= 1
                while higuy > mid and keys[higuy] == keys[mid]:
                    higuy -= 1
            if mid >= higuy:
                higuy -= 1
                while higuy > lo and keys[higuy] == keys[mid]:
                    higuy -= 1
            if higuy - lo >= hi - loguy:
                if lo < higuy:
                    lo_stack.append(lo)
                    hi_stack.append(higuy)
                if loguy < hi:
                    lo = loguy
                    continue
            else:
                if loguy < hi:
                    lo_stack.append(loguy)
                    hi_stack.append(hi)
                if lo < higuy:
                    hi = higuy
                    continue
        if not lo_stack:
            return a
        lo, hi = lo_stack.pop(), hi_stack.pop()


def encode_records(records: list[tuple[int, int, int, int, int]]) -> bytes:
    return b"".join(RECORD.pack(*r) for r in records)


CONT_HEADER = struct.Struct("<8H")  # four counts, four capacities (top, right, bottom, left)


def encode(c: Contours) -> bytes:
    return CONT_HEADER.pack(*c.counts(), *c.capacities) + b"".join(encode_records(a) for a in c.arrays)


def decode(payload: bytes) -> Contours:
    """Inverse of `encode`."""
    header = CONT_HEADER.unpack_from(payload, 0)
    arrays, pos = [], CONT_HEADER.size
    for n in header[:4]:
        arrays.append([RECORD.unpack_from(payload, pos + i * RECORD.size) for i in range(n)])
        pos += n * RECORD.size
    return Contours(arrays, list(header[4:]))
