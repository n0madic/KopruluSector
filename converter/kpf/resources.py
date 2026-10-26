"""Resource containers of the SDL runtime (docs/formats/packs.md): `generated/packs/<pack>.kpd`
(`PACK`), `generated/packs/music.kpd` (`MUSC`) and `generated/missions/<campaign>/<mission>_sounds.kpd`
(`MSND`).

A pack holds the atlases of its GRPs as 8-bit index pages (ATLS, FRMS, PAGE; packed by
converter/emit/pages.py) and the OGG files of its sfxdata sounds (SNDS). Frame f of grpmeta
GRPS entry e is FRMS[ATLS.first(atlas of e) + AFRM[e] + f] of the entry's pack (grpmeta.kpd
AFRM).
"""
from __future__ import annotations

import hashlib
import struct
from dataclasses import dataclass

from .. import CONVERTER_VERSION
from ..emit import pages
from ..emit.atlas import OpaqueZeroError
from . import kpd
from .container import Container

PACK_KIND = b"PACK"
PACK_SCHEMA = 1
MUSIC_KIND = b"MUSC"
MUSIC_SCHEMA = 1
MISSION_SOUNDS_KIND = b"MSND"
MISSION_SOUNDS_SCHEMA = 1
FRAME = struct.Struct("<5H")  # FRMS: page, x, y, width, height
TRACK_LOOPS = 0x01  # TRKS flags bit 0: the track loops (MUSI kind 1)


@dataclass(frozen=True)
class PackStats:
    atlases: int
    frames: int
    unique_frames: int  # distinct nonempty frame images, each stored once
    pages: int
    pixels: int  # texels covered by frames
    fill: float  # pixels / (pages * PAGE_SIZE^2)
    sounds: int
    ogg_bytes: int


def sounds_section(sounds: list[tuple[int, bytes]]) -> bytes:
    """SNDS: u16 count; per sound sorted by id: u16 id, u32 size, the OGG bytes."""
    out = bytearray(struct.pack("<H", len(sounds)))
    for sid, ogg in sorted(sounds, key=lambda s: s[0]):
        out += struct.pack("<HI", sid, len(ogg)) + ogg
    return bytes(out)


def pack_entries(gs: kpd.GrpSet, pack: str) -> list[tuple[str, list[kpd.GrpEntry]]]:
    """The pack's atlases in grpmeta ATLS order, each with its GRPS entries in order."""
    return [(name, [e for e in gs.grps if e.atlas == name]) for name, owner, _ in gs.atlases if owner == pack]


def build_pack(gs: kpd.GrpSet, pack: str, sounds: list[tuple[int, bytes]], *, profile_id: str,
               data_set_hash: bytes) -> tuple[Container, PackStats]:
    """generated/packs/<pack>.kpd. `sounds` lists (sfx id, OGG bytes). Frames with identical
    pixels share one page rectangle; empty frames are all zero records."""
    atlases = pack_entries(gs, pack)
    atls = bytearray([len(atlases)])
    images: list[tuple[int, int, bytes]] = []  # per FRMS record
    inputs = hashlib.sha256()
    for name, entries in atlases:
        atls += kpd._label(name) + struct.pack("<II", len(images), sum(len(e.grp.frames) for e in entries))
        for e in entries:
            if e.opaque_zero:
                raise OpaqueZeroError(f"{e.path} draws opaque index 0; pack pages have no opacity mask")
            inputs.update(e.data)
            images += [(f.width, f.height, f.indices) if f.width and f.height else (0, 0, b"") for f in e.grp.frames]
    unique: dict[tuple[int, int, bytes], int] = {}
    for image in images:
        if image[0]:
            unique.setdefault(image, len(unique))
    distinct = list(unique)
    placements, page_count = pages.pack([(w, h) for w, h, _ in distinct])
    frms = bytearray(struct.pack("<I", len(images)))
    for w, h, pixels in images:
        at = placements[unique[(w, h, pixels)]] if w else pages.EMPTY
        frms += FRAME.pack(at.page, at.x, at.y, w, h)
    page = bytearray(struct.pack("<HHH", page_count, pages.PAGE_SIZE, pages.PAGE_SIZE))
    for texels in pages.render(distinct, placements, page_count):
        png = pages.png(texels)
        page += struct.pack("<I", len(png)) + png
    derived = dict(algorithm_id=pages.ALGORITHM, algorithm_version=pages.ALGORITHM_VERSION, inputs_hash=inputs.digest())
    c = Container(PACK_KIND, PACK_SCHEMA, CONVERTER_VERSION, profile_id, source_hash=data_set_hash)
    c.add("ATLS", bytes(atls))
    c.add_derived("FRMS", bytes(frms), **derived)
    c.add_derived("PAGE", bytes(page), **derived)
    c.add("SNDS", sounds_section(sounds))
    pixels = sum(w * h for w, h, _ in distinct)
    stats = PackStats(len(atlases), len(images), len(distinct), page_count, pixels,
                      pixels / (page_count * pages.PAGE_SIZE ** 2) if page_count else 0.0, len(sounds),
                      sum(len(o) for _, o in sounds))
    return c, stats


def build_music(tracks: list[tuple[bool, bytes]], *, profile_id: str, data_set_hash: bytes) -> Container:
    """generated/packs/music.kpd. TRKS: u8 count; per ui.kpd MUSI track: u8 flags (TRACK_LOOPS),
    u32 size, the OGG bytes (size 0 without a file). Tracks naming one file each carry its bytes."""
    trks = bytearray([len(tracks)])
    for loops, ogg in tracks:
        trks += struct.pack("<BI", TRACK_LOOPS if loops else 0, len(ogg)) + ogg
    c = Container(MUSIC_KIND, MUSIC_SCHEMA, CONVERTER_VERSION, profile_id, source_hash=data_set_hash)
    c.add("TRKS", bytes(trks))
    return c


def build_mission_sounds(sounds: list[tuple[int, bytes]], *, profile_id: str, data_set_hash: bytes) -> Container:
    """generated/missions/<campaign>/<mission>_sounds.kpd. SNDS keyed by the mission's WAVS sound index."""
    c = Container(MISSION_SOUNDS_KIND, MISSION_SOUNDS_SCHEMA, CONVERTER_VERSION, profile_id, source_hash=data_set_hash)
    c.add("SNDS", sounds_section(sounds))
    return c
