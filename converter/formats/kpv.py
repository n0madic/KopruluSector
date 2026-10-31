"""KPV1: Koprulu's indexed video, the stand-in for an SMK that only StarCraft: Remastered's WebM copy has.

Remastered replaced 1.17's Smacker files (portraits, the glue's overlays) by WebM, lossy (docs/research/
remastered.md). The converter decodes a WebM with FFmpeg into one palette of 255 colours and index 0 for alpha
below 128 (palettegen and paletteuse, no dithering) and writes this format, which `kp_smk.c` plays like an SMK
and `smk.decode` reads back.

Layout (little endian): "KPV1", u32 width, u32 height, u32 frames, i32 frame rate (the SMK header's: > 0
milliseconds, < 0 in 10 microsecond units), u32 flags (the SMK header's: 1 a ring movie, whose extra last frame
leads back to frame 1), 768 bytes of palette (R, G, B), then for each of frames + ring stored frames u32 size and
its pixels in runs: a control byte c < 128 is followed by c + 1 literal indices, c >= 128 by one index
repeated c - 125 times (3 to 130).
"""
from __future__ import annotations

import array
import struct
import subprocess
import tempfile

from ..errors import ConverterError
from . import ffmpeg

MAGIC = b"KPV1"
HEADER = struct.Struct("<4sIIIiI")
PALETTE_SIZE = 768
RUN_MIN, RUN_MAX, LITERAL_MAX = 3, 130, 128
LIBVPX = {"vp8": "libvpx", "vp9": "libvpx-vp9"}
ALPHA_CUT = 128  # a pixel below this alpha is transparent (index 0)
PAL8_PALETTE = 1024  # FFmpeg's pal8 rawvideo: 256 x (B, G, R, A) after each frame
TRANSPARENT_PAL8 = 255  # palettegen's reserved transparent entry
PALETTE_FILTER = ("[0:v]split[a][b];[a]palettegen=max_colors=256:reserve_transparent=1:stats_mode=full[p];"
                  "[b][p]paletteuse=dither=none:alpha_threshold=128")


class KpvError(ConverterError, ValueError):
    """Malformed KPV1 data or a decoder failure."""


def pack_runs(pixels: bytes) -> bytes:
    out, literal, i, n = bytearray(), bytearray(), 0, len(pixels)

    def flush() -> None:
        for k in range(0, len(literal), LITERAL_MAX):
            chunk = literal[k:k + LITERAL_MAX]
            out.append(len(chunk) - 1)
            out.extend(chunk)
        literal.clear()

    while i < n:
        run = 1
        while i + run < n and run < RUN_MAX and pixels[i + run] == pixels[i]:
            run += 1
        if run >= RUN_MIN:
            flush()
            out += bytes((run + 125, pixels[i]))
        else:
            literal.extend(pixels[i:i + run])
        i += run
    flush()
    return bytes(out)


def unpack_runs(data: bytes, size: int) -> bytes:
    out, i = bytearray(), 0
    while len(out) < size:
        if i >= len(data):
            raise KpvError("frame data truncated")
        c = data[i]
        if c < LITERAL_MAX:
            out += data[i + 1:i + 2 + c]
            i += 2 + c
        else:
            out += bytes([data[i + 1]]) * (c - 125)
            i += 2
    if len(out) != size:
        raise KpvError("frame data overruns the frame")
    return bytes(out)


FLAG_RING = 1


def encode(width: int, height: int, frame_rate: int, palette: bytes, frames: list[bytes], ring: bool = False) -> bytes:
    """`frames` are the stored frames: the header's count and, for a ring movie, the extra one."""
    if len(palette) != PALETTE_SIZE:
        raise KpvError("palette must be 768 bytes")
    if len(frames) < 1 + ring:
        raise KpvError("no frames")
    out = bytearray(HEADER.pack(MAGIC, width, height, len(frames) - ring, frame_rate, FLAG_RING if ring else 0)
                    + palette)
    for f in frames:
        if len(f) != width * height:
            raise KpvError("frame size differs from the header's")
        body = pack_runs(f)
        out += struct.pack("<I", len(body)) + body
    return bytes(out)


def header(data: bytes) -> tuple[int, int, int, int, bool]:
    """(width, height, frames, frame rate, ring) of a KPV1 file, its frames not read."""
    if len(data) < HEADER.size + PALETTE_SIZE or data[:4] != MAGIC:
        raise KpvError("not a KPV1 file")
    _, width, height, count, rate, flags = HEADER.unpack_from(data)
    return width, height, count, rate, bool(flags & FLAG_RING)


def _frame_spans(data: bytes) -> list[tuple[int, int]]:
    """(offset, size) of each stored frame's runs; KpvError when one runs past the end."""
    _, _, count, _, ring = header(data)
    at, spans = HEADER.size + PALETTE_SIZE, []
    for _ in range(count + ring):
        if at + 4 > len(data):
            raise KpvError("frame record past the end")
        size = struct.unpack_from("<I", data, at)[0]
        at += 4
        if at + size > len(data):
            raise KpvError("frame data past the end")
        spans.append((at, size))
        at += size
    return spans


def complete(data: bytes) -> bool:
    """Whether data is a KPV1 file whose every stored frame lies within it (a cache file written whole)."""
    try:
        _frame_spans(data)
    except KpvError:
        return False
    return True


def decode(data: bytes) -> tuple[tuple[int, int, int, int, bool], bytes, list[bytes]]:
    """((width, height, frames, frame rate, ring), palette, stored frames)."""
    head = header(data)
    palette = data[HEADER.size:HEADER.size + PALETTE_SIZE]
    frames = [unpack_runs(data[at:at + size], head[0] * head[1]) for at, size in _frame_spans(data)]
    return head, palette, frames


def from_webm(webm: bytes, frame_rate: int, frames: int | None = None, ring: bool = False,
              palette: bytes | None = None, index=None) -> bytes:
    """A WebM (VP8/VP9, alpha kept by libvpx) as KPV1. Without `palette`, FFmpeg's palettegen and paletteuse give
    one palette of the whole movie without dithering, its reserved transparent entry (255) becomes index 0; with
    it (a glue movie, drawn in its screen's palette), `index(r, g, b, a)` gives each pixel's index. `frames` (+ the
    ring frame) is the SMK's count: the WebM's frames are cut to it or its last repeated."""
    ffmpeg.require(KpvError, "convert")
    with tempfile.NamedTemporaryFile(suffix=".webm") as f:
        f.write(webm)
        f.flush()
        fields = ffmpeg.probe(f.name, "v:0", ("codec_name", "width", "height"), KpvError)
        if fields is None:
            raise KpvError("the WebM has no video stream")
        codec, width, height = fields[0], int(fields[1]), int(fields[2])
        decoder = ["-c:v", LIBVPX[codec]] if codec in LIBVPX else []  # FFmpeg's own VP8/VP9 decoders drop alpha
        filters = [] if palette is not None else ["-filter_complex", PALETTE_FILTER]
        pixels = "rgba" if palette is not None else "pal8"
        run = subprocess.run(["ffmpeg", "-v", "error", *decoder, "-i", f.name, *filters, "-f", "rawvideo",
                              "-pix_fmt", pixels, "-"], capture_output=True, check=False)
        if run.returncode:
            raise KpvError(f"ffmpeg failed: {run.stderr.decode(errors='replace').strip()}")
    if palette is not None:
        rgba, size = run.stdout, width * height * 4
        if not rgba or len(rgba) % size:
            raise KpvError("decoded video size is not whole frames")
        words = array.array("I", rgba)  # one lookup per pixel: its RGBA as one word
        known: dict[int, int] = {}

        def pixel(w: int) -> int:
            k = known.get(w)
            if k is None:
                k = known[w] = index(w & 0xFF, (w >> 8) & 0xFF, (w >> 16) & 0xFF, w >> 24)
            return k

        per_frame = width * height
        stored = [bytes(map(pixel, words[at:at + per_frame])) for at in range(0, len(words), per_frame)]
        return encode(width, height, frame_rate, palette, _count(stored, frames, ring), ring)
    frame_bytes = width * height + PAL8_PALETTE
    data = run.stdout
    if not data or len(data) % frame_bytes:
        raise KpvError("decoded video size is not whole frames")
    bgra = data[width * height:frame_bytes]
    palette = bytearray(b"".join(bytes((bgra[i + 2], bgra[i + 1], bgra[i])) for i in range(0, PAL8_PALETTE, 4)))
    palette[0:3], palette[765:768] = palette[765:768], palette[0:3]
    palette[0:3] = bytes(3)  # index 0: transparent
    swap = bytes.maketrans(bytes((0, TRANSPARENT_PAL8)), bytes((TRANSPARENT_PAL8, 0)))
    stored = [data[at:at + width * height].translate(swap) for at in range(0, len(data), frame_bytes)]
    return encode(width, height, frame_rate, bytes(palette), _count(stored, frames, ring), ring)


def _count(stored: list[bytes], frames: int | None, ring: bool) -> list[bytes]:
    if frames is None:
        return stored
    want = frames + ring
    return stored[:want] + stored[-1:] * (want - len(stored))
