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
        """Duration from data length and nAvgBytesPerSec (integer milliseconds)."""
        return self.data_length * 1000 // self.avg_bytes_per_sec if self.avg_bytes_per_sec else 0


def read_info(data: bytes) -> WavInfo:
    if len(data) < 12 or data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        raise WavError("not a RIFF WAVE file")
    pos = 12
    fmt = None
    while pos + 8 <= len(data):
        tag, size = struct.unpack_from("<4sI", data, pos)
        body = pos + 8
        if tag == b"fmt ":
            if size < 16:
                raise WavError("fmt chunk shorter than 16 bytes")
            fmt = struct.unpack_from("<HHIIHH", data, body)
        elif tag == b"data":
            if fmt is None:
                raise WavError("data chunk before fmt chunk")
            length = min(size, len(data) - body)
            return WavInfo(*fmt, data_offset=body, data_length=length)
        pos = body + size  # no even-size padding, as the native walker
    raise WavError("no data chunk")
