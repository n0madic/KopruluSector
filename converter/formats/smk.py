"""Smacker (SMK) videos: header fields and indexed frame decoding.

Frames are decoded with FFmpeg's smackvideo decoder to 8-bit indices plus each frame's
palette (`-pix_fmt pal8`): for all 621 SMKs of the archives its RGB output equals the
original smackw32.dll frame for frame (docs/research/evidence/smk-decoder-equivalence.json).
FFmpeg is a converter-time tool only (`brew install ffmpeg`).
"""
from __future__ import annotations

import shutil
import struct
import subprocess
import tempfile
from dataclasses import dataclass

from ..errors import ConverterError

PALETTE_BYTES = 1024  # FFmpeg pal8 rawvideo: 256 x (B, G, R, A) after each frame


class SmkError(ConverterError, ValueError):
    """Malformed SMK or a decoder failure."""


@dataclass(frozen=True)
class Header:
    signature: bytes
    width: int
    height: int
    frames: int  # header frame count
    ring_frame: bool  # flag 1: an extra ring frame follows the stored frames
    ms_per_frame: float

    @property
    def decoded_frames(self) -> int:
        return self.frames + self.ring_frame


@dataclass(frozen=True)
class Frame:
    indices: bytes
    palette: bytes  # 768 bytes, R G B per index


def header(data: bytes) -> Header:
    if len(data) < 104 or data[:4] not in (b"SMK2", b"SMK4"):
        raise SmkError("not a Smacker file")
    width, height, frames, interval, flags = struct.unpack_from("<IIIiI", data, 4)
    # interval > 0: milliseconds per frame; < 0: units of 10 microseconds.
    ms = interval if interval > 0 else (-interval / 100 if interval < 0 else 100)
    return Header(data[:4], width, height, frames, bool(flags & 1), float(ms))


def available() -> bool:
    return shutil.which("ffmpeg") is not None


def decode(data: bytes) -> tuple[Header, list[Frame]]:
    h = header(data)
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
