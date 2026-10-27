"""StarCraft: Remastered's HD portraits (ADR 0013, stage R4e): `generated/hd/<variant>/portraits.kpd` (`HDPV`).

Each portrait movie of portraits.kpd (`portrait\\<unit>\\<name><variant>.smk`) whose HD2 WebM exists
(`HD2/portrait/<unit>/<name><variant>.webm`, else the HD one), as KPV1 (formats/kpv.py: one palette of the clip, no
dithering) with the SD movie's frame count, rate and ring frame, so the SD movie's frame n is the HD clip's frame n.
Both variants take HD2's clips: Remastered's HD console shows a portrait about 200 x 240 pixels wide at 4 x, which
HD2's 224 x 240 already fill (HD's 448 x 480 would be four times the bytes, about 1 GB, for nothing seen). Sections:
- HEAD: u16 clips;
- CLIP: per clip a 64-byte NUL-padded lowercase SD path, u32 offset into DATA, u32 size;
- DATA: the KPV1 clips, each from a 4-byte boundary.
"""
from __future__ import annotations

import hashlib
import logging
import struct
from pathlib import Path

from .. import CONVERTER_VERSION
from ..casc.storage import CascStorage
from ..formats import kpv, smk
from ..kpf import kpd
from ..kpf.container import Container
from .files import write_if_changed
from .hd import SCHEMA, HdError

log = logging.getLogger(__name__)

VIDEO_KIND = b"HDPV"
PATH_SIZE = 64
CLIP = struct.Struct("<64sII")
SOURCES = ("HD2/", "")  # the clip's WebM: HD2's, else HD's
RATE_OFFSET = 16  # the SMK's and KPV1's i32 frame interval
KPV_RULE = b"hd portrait kpv1 v1"  # a change of the conversion invalidates the cache


def webm_name(path: str) -> str:
    """The WebM's name (without the variant's prefix) of an SD portrait movie's path."""
    return path.replace("\\", "/").lower()[:-4] + ".webm"


def clip(storage: CascStorage, sd: bytes, name: str, cache: Path | None) -> bytes | None:
    """The KPV1 of an SD movie's HD WebM with the SD movie's frames, rate and ring frame, or None."""
    found = next((p + name for p in SOURCES if storage.has(p + name)), None)
    if found is None:
        return None
    webm = storage.read(found)
    h = smk.header(sd)
    rate = struct.unpack_from("<i", sd, RATE_OFFSET)[0]
    cached = None
    if cache is not None:
        key = hashlib.sha256(webm + struct.pack("<IiB", h.frames, rate, h.ring_frame) + KPV_RULE).hexdigest()
        cached = cache / f"{key}.kpv"
        if cached.is_file():
            return cached.read_bytes()
    data = kpv.from_webm(webm, rate, h.frames, h.ring_frame)
    if cached is not None:
        cached.parent.mkdir(parents=True, exist_ok=True)
        cached.write_bytes(data)
    return data


def generate(read, has, storage: CascStorage, root: Path, profile_id: str, source_hash: bytes,
             cache: Path | None = None) -> dict:
    """portraits.kpd of one variant; counts."""
    paths = kpd.portrait_set(read, has).paths
    clips: list[tuple[str, bytes]] = []
    for path in paths:
        data = clip(storage, read(path), webm_name(path), cache)
        if data is not None:
            clips.append((path, data))
    index = bytearray()
    body = bytearray()
    for path, data in clips:
        name = path.lower().encode("latin-1")
        if len(name) >= PATH_SIZE:
            raise HdError(f"{path}: path too long for CLIP")
        index += CLIP.pack(name, len(body), len(data))
        body += data
        body += bytes(-len(body) % 4)
    c = Container(VIDEO_KIND, SCHEMA, CONVERTER_VERSION, profile_id, source_hash=source_hash)
    c.add("HEAD", struct.pack("<H", len(clips)))
    c.add("CLIP", bytes(index))
    c.add("DATA", bytes(body))
    write_if_changed(root / "portraits.kpd", c.to_bytes())
    log.info("hd portraits: %d of %d movies, %d bytes", len(clips), len(paths), len(body))
    return {"clips": len(clips), "movies": len(paths)}
