"""Smacker (SMK) videos: header fields and indexed frame decoding.

Frames are decoded with FFmpeg's smackvideo decoder to 8-bit indices plus each frame's
palette (`-pix_fmt pal8`): for all 621 SMKs of the archives its RGB output equals the
original smackw32.dll frame for frame (docs/research/evidence/smk-decoder-equivalence.json).
FFmpeg is a research tool only (`brew install ffmpeg`).
"""
from __future__ import annotations

import shutil
import struct
import subprocess
import tempfile
from dataclasses import dataclass
from fractions import Fraction

from ..errors import ConverterError
from . import kpw

PALETTE_BYTES = 1024  # FFmpeg pal8 rawvideo: 256 x (B, G, R, A) after each frame
HEADER_SIZE = 104  # the fixed header: signature, size, frames, rate, flags, 7 audio sizes, tree sizes, 7 audio rates
DEFAULT_MS_PER_FRAME = 100  # a frame rate field of 0


class SmkError(ConverterError, ValueError):
    """Malformed SMK or a decoder failure."""


@dataclass(frozen=True)
class Header:
    signature: bytes
    width: int
    height: int
    frames: int  # header frame count
    ring_frame: bool  # flag 1: an extra ring frame follows the stored frames
    interval: int  # the header's frame rate field: > 0 ms per frame, < 0 in 10 microsecond units, 0 the default

    @property
    def decoded_frames(self) -> int:
        return self.frames + self.ring_frame

    @property
    def rate(self) -> Fraction:
        """Frames per second, exactly: -6666 (the cinematics) is 50000/3333."""
        return 1000 / self.ms_per_frame_exact

    @property
    def ms_per_frame_exact(self) -> Fraction:
        """interval > 0: milliseconds per frame; < 0: units of 10 microseconds; 0: the default 100 ms."""
        if self.interval > 0:
            return Fraction(self.interval)
        return Fraction(-self.interval, 100) if self.interval < 0 else Fraction(DEFAULT_MS_PER_FRAME)

    @property
    def ms_per_frame(self) -> float:
        return float(self.ms_per_frame_exact)


@dataclass(frozen=True)
class Frame:
    indices: bytes
    palette: bytes  # 768 bytes, R G B per index


def header(data: bytes) -> Header:
    if data[:4] == kpw.MAGIC:  # a Remastered WebM in the SMK's place (formats/kpw.py)
        width, height, frames, rate, ring = kpw.header(data)
        return Header(kpw.MAGIC, width, height, frames, ring, rate)
    if len(data) < HEADER_SIZE or data[:4] not in (b"SMK2", b"SMK4"):
        raise SmkError("not a Smacker file")
    width, height, frames, interval, flags = struct.unpack_from("<IIIiI", data, 4)
    return Header(data[:4], width, height, frames, bool(flags & 1), interval)


def available() -> bool:
    return shutil.which("ffmpeg") is not None


def decode(data: bytes) -> tuple[Header, list[Frame]]:
    h = header(data)
    if h.signature == kpw.MAGIC:
        _, palette, stored = kpw.decode(data)
        return h, [Frame(f, palette) for f in stored]
    with tempfile.NamedTemporaryFile(suffix=".smk") as f:
        f.write(data)
        f.flush()
        run = subprocess.run(["ffmpeg", "-v", "error", "-i", f.name, "-f", "rawvideo", "-pix_fmt", "pal8", "-"],
                             capture_output=True, check=False)
    if run.returncode:
        raise SmkError(f"ffmpeg failed: {run.stderr.decode(errors='replace').strip()}")
    size = h.width * h.height
    step = size + PALETTE_BYTES
    raw = run.stdout
    if len(raw) % step or len(raw) // step != h.decoded_frames:
        raise SmkError(f"decoded {len(raw)} bytes, expected {h.decoded_frames} frames of {step}")
    frames = []
    for at in range(0, len(raw), step):
        pal = raw[at + size : at + step]
        rgb = b"".join(bytes((pal[4 * i + 2], pal[4 * i + 1], pal[4 * i])) for i in range(256))
        frames.append(Frame(raw[at : at + size], rgb))
    return h, frames


def rgb24(frames: list[Frame]) -> bytes:
    """Frames as the equivalence evidence hashes them: palette applied, R G B per pixel."""
    out = bytearray()
    for f in frames:
        for i in f.indices:
            out += f.palette[3 * i : 3 * i + 3]
    return bytes(out)
