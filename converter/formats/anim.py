"""StarCraft: Remastered's HD sprite files (`anim/main_NNN.anim`, `HD2/anim/main_NNN.anim`; ADR 0013).

Header: "ANIM", u8 scale (4 HD, 2 HD2, 1 SD), u8 type (2: one image, 1: the SD table), u16 0, u16 layers,
u16 entries, ten 32-byte layer names. An entry: u16 frames, u16 0xffff, u16 width and height of the image's
box, u32 offset of its frame records, then per layer u32 offset, u32 size, u16 width, u16 height of its sheet
(a DDS; offset 0 for an empty layer). A frame record (16 bytes): u16 x, y of the frame in the sheet, i16 x, y of
the frame in the box, u16 width, height, u32 1. The box and the records are in HD (×4) pixels in both HD and
HD2 files; an HD2 sheet is half the size, so its coordinates are the records' halved.

The frame counts equal the GRPs' (docs/research/remastered.md); the images Remastered draws from another
image's frames have no file of their own (`SD/images.rel`).
"""
from __future__ import annotations

import struct
from dataclasses import dataclass

from ..errors import ConverterError
from . import dds

MAGIC = b"ANIM"
HEADER = struct.Struct("<4sBBHHH")
LAYER_NAME = 32
LAYER_NAMES = 10
ENTRY = struct.Struct("<HHHHI")
LAYER = struct.Struct("<IIHH")
FRAME = struct.Struct("<HHhhHHI")
RECORD_SCALE = 4  # the box and the frame records are in HD pixels
TYPE_IMAGE = 2
REL = struct.Struct("<Ii")


class AnimError(ConverterError, ValueError):
    """Malformed anim data."""


@dataclass(frozen=True)
class Frame:
    x: int  # in the sheet, in the file's pixels
    y: int
    left: int  # in the box
    top: int
    width: int
    height: int


@dataclass(frozen=True)
class Layer:
    name: str
    width: int
    height: int
    fourcc: bytes
    dds: bytes


@dataclass(frozen=True)
class Image:
    scale: int
    box: tuple[int, int]  # in the file's pixels
    frames: list[Frame]
    layers: dict[str, Layer]


def parse(data: bytes) -> Image:
    """One image's anim file (type 2)."""
    if len(data) < HEADER.size + LAYER_NAME * LAYER_NAMES or data[:4] != MAGIC:
        raise AnimError("not an anim file")
    _, scale, kind, _, layer_count, entries = HEADER.unpack_from(data)
    if kind != TYPE_IMAGE or entries != 1 or scale not in (1, 2, 4) or layer_count > LAYER_NAMES:
        raise AnimError(f"unsupported anim: scale {scale}, type {kind}, {entries} entries, {layer_count} layers")
    names = [data[HEADER.size + LAYER_NAME * i:HEADER.size + LAYER_NAME * (i + 1)].split(b"\0")[0].decode("ascii")
             for i in range(layer_count)]
    at = HEADER.size + LAYER_NAME * LAYER_NAMES
    count, _, box_w, box_h, frames_at = ENTRY.unpack_from(data, at)
    at += ENTRY.size
    div = RECORD_SCALE // scale
    layers = {}
    for name in names:
        offset, size, width, height = LAYER.unpack_from(data, at)
        at += LAYER.size
        if not offset:
            continue
        if offset + size > len(data):
            raise AnimError(f"layer {name} runs past the end")
        sheet = data[offset:offset + size]
        h = dds.header(sheet)
        layers[name] = Layer(name, h.width, h.height, h.fourcc, sheet)
    if frames_at + FRAME.size * count > len(data):
        raise AnimError("frame records run past the end")
    frames = []
    for i in range(count):
        x, y, left, top, w, h, _ = FRAME.unpack_from(data, frames_at + FRAME.size * i)
        frames.append(Frame(x // div, y // div, left // div, top // div, w // div, h // div))
    return Image(scale, (box_w // div, box_h // div), frames, layers)


def relations(data: bytes) -> list[tuple[int, int]]:
    """`SD/images.rel`: per images.dat entry (u32, i32): the second is the image whose frames it draws, -1 for
    its own."""
    if len(data) % REL.size:
        raise AnimError("images.rel is not whole records")
    return [REL.unpack_from(data, at) for at in range(0, len(data), REL.size)]
