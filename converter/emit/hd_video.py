"""StarCraft: Remastered's HD portraits (ADR 0013, stage R4e): `generated/hd/<variant>/portraits.kpd` (`HDPV`).

Each portrait movie of portraits.kpd (`portrait\\<unit>\\<name><variant>.smk`) whose HD2 WebM exists
(`HD2/portrait/<unit>/<name><variant>.webm`, else the HD one), as that WebM (ADR 0018, stage D6: the app decodes it
with libvpx to RGBA, kp_webm.h); the SD movie's frame n is the clip's frame n (its last past its end). A desktop
reading a Remastered install takes the same WebM from CASC instead (kp_app_hd_ui.c).
Both variants take HD2's clips: Remastered's HD console shows a portrait about 200 x 240 pixels wide at 4 x, which
HD2's 224 x 240 already fill (HD's 448 x 480 would be four times the bytes, about 1 GB, for nothing seen). Sections:
- HEAD: u16 clips;
- CLIP: per clip a 64-byte NUL-padded lowercase SD path, u32 offset into DATA, u32 size;
- DATA: the WebM clips, each from a 4-byte boundary.
"""
from __future__ import annotations

import logging
import struct
from pathlib import Path

from .. import CONVERTER_VERSION
from ..casc.storage import CascStorage
from ..kpf import kpd
from ..kpf.container import Container
from .files import write_if_changed
from .hd import SCHEMA, HdError

log = logging.getLogger(__name__)

VIDEO_KIND = b"HDPV"
PATH_SIZE = 64
CLIP = struct.Struct("<64sII")
SOURCES = ("HD2/", "")  # the clip's WebM: HD2's, else HD's


def webm_name(path: str) -> str:
    """The WebM's name (without the variant's prefix) of an SD portrait movie's path."""
    return path.replace("\\", "/").lower()[:-4] + ".webm"


def clip(storage: CascStorage, name: str) -> bytes | None:
    """An SD movie's HD WebM, or None."""
    found = next((p + name for p in SOURCES if storage.has(p + name)), None)
    return None if found is None else storage.read(found)


def generate(read, has, storage: CascStorage, root: Path, profile_id: str, source_hash: bytes) -> dict:
    """portraits.kpd of one variant; counts and its path."""
    paths = kpd.portrait_set(read, has).paths
    clips: list[tuple[str, bytes]] = []
    for path in paths:
        data = clip(storage, webm_name(path))
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
    return {"clips": len(clips), "movies": len(paths), "path": str(root / "portraits.kpd")}
