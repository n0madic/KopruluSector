"""RIFF WAVE metadata reader following the native walker: chunks are not padded to even sizes.

Plan §5.1/§4.2: sounds.kpd keeps the data length and nAvgBytesPerSec of every source WAV
for the native duration formula, so the converter must read them as the original does.
"""
from __future__ import annotations

import struct
from dataclasses import dataclass


class WavError(ValueError):
    """Malformed WAV data."""


@dataclass(frozen=True)
class WavInfo:
    format_tag: int
    channels: int
    sample_rate: int
    avg_bytes_per_sec: int
    block_align: int
    bits_per_sample: int
    data_offset: int
    data_length: int

    @property
    def duration_ms(self) -> int:
        """`0x49da70` arithmetic: u32(bytes * 1000) / nAvgBytesPerSec (the product wraps)."""
        return ((self.data_length * 1000) & 0xFFFFFFFF) // self.avg_bytes_per_sec if self.avg_bytes_per_sec else 0


def _find(data: bytes, pos: int, remaining: int, tag: bytes) -> tuple[int, int, int] | None:
    """`FindRiffChunk` 0x49d7c0: (payload offset, chunk size, remaining after the header) or None."""
    while True:
        if remaining < 8:
            return None
        remaining -= 8
        chunk, size = struct.unpack_from("<4sI", data, pos)
        if chunk == tag:
            return pos + 8, size, remaining
        if size > remaining:
            return None
        remaining -= size
        pos += 8 + size  # no even-size padding


def read_info(data: bytes) -> WavInfo:
    """`ParseWaveFile` 0x49d830: `fmt ` from byte 12 (>= 16 bytes, inside the file), then
    `data` after it, whose size must fit; any format tag is accepted."""
    if len(data) < 12 or data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        raise WavError("not a RIFF WAVE file")
    found = _find(data, 12, len(data) - 12, b"fmt ")
    if not found:
        raise WavError("no fmt chunk")
    fmt_at, fmt_size, remaining = found
    if fmt_size < 16 or fmt_size > remaining:
        raise WavError(f"fmt chunk of {fmt_size} bytes")
    fmt = struct.unpack_from("<HHIIHH", data, fmt_at)
    found = _find(data, fmt_at + fmt_size, remaining - fmt_size, b"data")
    if not found:
        raise WavError("no data chunk after fmt")
    data_at, size, remaining = found
    if size > remaining:
        raise WavError(f"data chunk of {size} bytes exceeds the file")
    return WavInfo(*fmt, data_offset=data_at, data_length=size)
