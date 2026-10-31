"""IVF, the plain container of VP9 packets that `ffmpeg -c:v copy -f ivf` writes (the cinematics' video, ADR 0015).

A 32-byte file header (`DKIF`, version, header size, FourCC, width, height, time base denominator and numerator,
frame count), then per frame a 12-byte header (u32 size, u64 presentation time in the time base) and its packet.
"""
from __future__ import annotations

import struct
from dataclasses import dataclass
from fractions import Fraction

from ..errors import ConverterError

MAGIC = b"DKIF"
HEADER = struct.Struct("<4sHH4sHHIII4x")
FRAME = struct.Struct("<IQ")
assert HEADER.size == 32


class IvfError(ConverterError, ValueError):
    """Malformed IVF data."""


@dataclass(frozen=True)
class Frame:
    pts: int
    data: bytes
    key: bool


@dataclass(frozen=True)
class Ivf:
    fourcc: bytes
    width: int
    height: int
    timebase: Fraction  # seconds per pts unit
    frames: list[Frame]


def is_key(packet: bytes) -> bool:
    """Whether a VP9 packet's (first) frame is a key frame: frame marker 2, the profile's two bits (and a reserved
    bit in profile 3), show_existing_frame, then frame_type (0: key)."""
    if not packet:
        return False
    b = packet[0]
    if b >> 6 != 2:
        return False
    profile = ((b >> 5) & 1) | (((b >> 4) & 1) << 1)
    bits = 4 + (1 if profile == 3 else 0)  # the bits used so far, from the top
    if (b >> (7 - bits)) & 1:  # show_existing_frame: shows an earlier frame, not a key frame
        return False
    return not (b >> (6 - bits)) & 1


def parse(data: bytes) -> Ivf:
    """The frames of an IVF file; IvfError when it is cut or is not one. The header's frame count is not trusted
    (FFmpeg writes it only after the file): the frames are read to the end."""
    if len(data) < HEADER.size:
        raise IvfError("IVF header cut short")
    magic, version, size, fourcc, width, height, rate, scale, _ = HEADER.unpack_from(data)
    if magic != MAGIC or version != 0 or size != HEADER.size:
        raise IvfError("not an IVF file")
    if not rate or not scale:
        raise IvfError(f"time base {scale}/{rate}")
    frames, at = [], HEADER.size
    while at < len(data):
        if at + FRAME.size > len(data):
            raise IvfError("frame header cut short")
        n, pts = FRAME.unpack_from(data, at)
        at += FRAME.size
        if at + n > len(data):
            raise IvfError("frame data cut short")
        packet = data[at:at + n]
        frames.append(Frame(pts, packet, is_key(packet)))
        at += n
    return Ivf(fourcc, width, height, Fraction(scale, rate), frames)
