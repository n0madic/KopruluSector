"""KPW1: the stand-in for an SMK that only StarCraft: Remastered's WebM copy has (ADR 0018, stage D6).

Remastered replaced 1.17's Smacker files (portraits, the glue's overlays) by WebM, lossy (docs/research/
remastered.md). A KPW1 is that WebM as it is, behind the SMK's header fields and the palette of the screen it is
drawn on; `kp_smk.c` plays it like an SMK (libvpx through the host's codec) and `decode` reads it back the same way.

Layout (little endian): "KPW1", u32 width, u32 height (the WebM's: Remastered drew 4 of profile bw-1.17.0.1's 549
movies narrower than their SMKs, glue\\mainmenu\\multion.smk 240 x 224 for 356 x 224), u32 frames, i32 frame rate
(the SMK header's: > 0 milliseconds, < 0 in 10 microsecond units), u32 flags (the SMK header's: 1 a ring movie, whose
extra last frame leads back to frame 1; KPW1's own: 2, a set of usable entries follows the palette), 768 bytes of
palette (R, G, B), with flag 2 the 32 bytes of a set of the palette's entries (bit i & 7 of byte i >> 3: entry i
usable; a portrait's: the entries the tilesets' palettes share, which it is drawn over in a game, as are the ready
rooms' in a briefing), then the WebM. Frame n is the WebM's frame n (its last past its end), each pixel its colour's
index in the palette among the usable entries by formats/palette.py's IndexMap, transparent (index 0) below alpha 128.
"""
from __future__ import annotations

import array
import struct
import subprocess
import tempfile

from ..errors import ConverterError
from . import ffmpeg
from .palette import OPAQUE, IndexMap

MAGIC = b"KPW1"
HEADER = struct.Struct("<4sIIIiI")
PALETTE_SIZE = 768
FLAG_RING = 1
FLAG_SHARED = 2
USABLE_SIZE = 32  # the set of usable entries after the palette with FLAG_SHARED
ALPHA_CUT = 128  # a pixel below this alpha is transparent (index 0)
LIBVPX = {"vp8": "libvpx", "vp9": "libvpx-vp9"}  # FFmpeg's own VP8/VP9 decoders drop alpha


class KpwError(ConverterError, ValueError):
    """Malformed KPW1 data or a decoder failure."""


def usable_bits(entries) -> bytes:
    """The set of palette entries as KPW1 stores it."""
    bits = bytearray(USABLE_SIZE)
    for i in entries:
        bits[i >> 3] |= 1 << (i & 7)
    return bytes(bits)


def encode(width: int, height: int, frames: int, frame_rate: int, ring: bool, palette: bytes, webm: bytes,
           usable: frozenset[int] | None = None) -> bytes:
    """`frames` is the SMK header's count (without the ring frame); `usable` the entries its pixels may take (None:
    any)."""
    if len(palette) != PALETTE_SIZE:
        raise KpwError("palette must be 768 bytes")
    flags = (FLAG_RING if ring else 0) | (FLAG_SHARED if usable is not None else 0)
    shared = usable_bits(usable) if usable is not None else b""
    return HEADER.pack(MAGIC, width, height, frames, frame_rate, flags) + palette + shared + webm


# Matroska's elements on the way to a video track's size (the IDs keep their length marker)
SEGMENT, TRACKS, TRACK_ENTRY, TRACK_TYPE, VIDEO, PIXEL_WIDTH, PIXEL_HEIGHT, CLUSTER = (
    0x18538067, 0x1654AE6B, 0xAE, 0x83, 0xE0, 0xB0, 0xBA, 0x1F43B675)
TRACK_VIDEO = 1


def _vint(data: bytes, at: int, marker: bool) -> tuple[int | None, int]:
    """An EBML variable-length integer at `at`: (value, the offset past it); an ID keeps its marker, a size of all ones
    is unknown (None)."""
    first = data[at]
    length = 9 - first.bit_length()
    if length > 8:
        raise IndexError("no EBML length marker")
    value = first if marker else first & (0xFF >> length)
    for b in data[at + 1:at + length]:
        value = value << 8 | b
    if at + length > len(data):
        raise IndexError("EBML integer past the end")
    if not marker and value == (1 << 7 * length) - 1:
        return None, at + length
    return value, at + length


def _elements(data: bytes, at: int, end: int):
    """(ID, body start, body end) of each element in data[at:end] (an unknown size runs to `end`)."""
    while at < end:
        ident, at = _vint(data, at, True)
        size, at = _vint(data, at, False)
        stop = end if size is None else min(at + size, end)
        yield ident, at, stop
        at = stop


def _uint(data: bytes, at: int, stop: int) -> int:
    return int.from_bytes(data[at:stop], "big")


def webm_size(webm: bytes) -> tuple[int, int] | None:
    """The width and height of a WebM's first video track (nestegg's, kp_webm_size), None when it has none."""
    try:
        for ident, at, stop in _elements(webm, 0, len(webm)):
            if ident != SEGMENT:
                continue
            for child, cat, cstop in _elements(webm, at, stop):
                if child == CLUSTER:
                    return None
                if child != TRACKS:
                    continue
                for entry, eat, estop in _elements(webm, cat, cstop):
                    if entry != TRACK_ENTRY:
                        continue
                    kind, size = None, {}
                    for field, fat, fstop in _elements(webm, eat, estop):
                        if field == TRACK_TYPE:
                            kind = _uint(webm, fat, fstop)
                        elif field == VIDEO:
                            for v, vat, vstop in _elements(webm, fat, fstop):
                                if v in (PIXEL_WIDTH, PIXEL_HEIGHT):
                                    size[v] = _uint(webm, vat, vstop)
                    if kind == TRACK_VIDEO:
                        return (size[PIXEL_WIDTH], size[PIXEL_HEIGHT]) if len(size) == 2 else None
            return None
    except IndexError:
        return None
    return None


def header(data: bytes) -> tuple[int, int, int, int, bool]:
    """(width, height, frames, frame rate, ring) of a KPW1 file."""
    if len(data) < HEADER.size + PALETTE_SIZE or data[:4] != MAGIC:
        raise KpwError("not a KPW1 file")
    _, width, height, count, rate, flags = HEADER.unpack_from(data)
    return width, height, count, rate, bool(flags & FLAG_RING)


def decode(data: bytes) -> tuple[tuple[int, int, int, int, bool], bytes, list[bytes]]:
    """((width, height, frames, frame rate, ring), palette, the frames + ring stored frames as indices), the WebM
    decoded by libvpx through FFmpeg."""
    head = header(data)
    width, height, frames, _, ring = head
    palette = data[HEADER.size:HEADER.size + PALETTE_SIZE]
    at, usable = HEADER.size + PALETTE_SIZE, None
    if HEADER.unpack_from(data)[5] & FLAG_SHARED:
        if len(data) < at + USABLE_SIZE:
            raise KpwError("its usable entries cut short")
        bits = data[at:at + USABLE_SIZE]
        usable = frozenset(i for i in range(256) if bits[i >> 3] >> (i & 7) & 1)
        at += USABLE_SIZE
    ffmpeg.require(KpwError, "decode StarCraft: Remastered's WebM")
    with tempfile.NamedTemporaryFile(suffix=".webm") as f:
        f.write(data[at:])
        f.flush()
        fields = ffmpeg.probe(f.name, "v:0", ("codec_name", "width", "height"), KpwError)
        if fields is None:
            raise KpwError("the WebM has no video stream")
        if (int(fields[1]), int(fields[2])) != (width, height):
            raise KpwError(f"the WebM is {fields[1]} x {fields[2]}, the header {width} x {height}")
        decoder = ["-c:v", LIBVPX[fields[0]]] if fields[0] in LIBVPX else []
        run = subprocess.run(["ffmpeg", "-v", "error", *decoder, "-i", f.name, "-f", "rawvideo", "-pix_fmt", "rgba",
                              "-"], capture_output=True, check=False)
    if run.returncode:
        raise KpwError(f"ffmpeg failed: {run.stderr.decode(errors='replace').strip()}")
    rgba, size = run.stdout, width * height * 4
    if not rgba or len(rgba) % size:
        raise KpwError("decoded video size is not whole frames")
    index = IndexMap(tuple(tuple(palette[i:i + 3]) for i in range(0, PALETTE_SIZE, 3)), usable)
    words = array.array("I", rgba)  # one lookup per pixel: its RGBA as one word (little endian: R lowest)
    known: dict[int, int] = {}

    def pixel(w: int) -> int:
        k = known.get(w)
        if k is None:
            k = known[w] = index(w & 0xFF, (w >> 8) & 0xFF, (w >> 16) & 0xFF, OPAQUE if w >> 24 >= ALPHA_CUT else 0)
        return k

    per_frame = width * height
    want = frames + ring
    decoded = range(0, len(words), per_frame)
    stored = [bytes(map(pixel, words[at:at + per_frame])) for at in decoded[:want]]
    return head, palette, stored + stored[-1:] * (want - len(stored))
