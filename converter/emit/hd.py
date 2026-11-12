"""StarCraft: Remastered's HD interface art for the HD renderer (ADR 0013, stage R2): `generated/hd/<variant>/`.

`hd2` (×2) and `hd` (×4) are written alike from the user's install, never shipped:
- `ui.kpd` (`HDUI`): the interface's HD art (converter/emit/hd_ui.py);
- `portraits.kpd` (`HDPV`): the portraits' clips (converter/emit/hd_video.py), written once and linked into the other
  variant (both take HD2's clips).

The HD world (the packs, the tilesets and the water's normal maps: `HDPK`, `HDTS`, `HDWA`) is the runtime's, laid out
from the install's anims at a mission's start, or kp_convert repack's sets (ADR 0019, stage C5).

TODO: the interface's art and the portraits from the install and the repack too, this module gone (ADR 0019, C6).
"""
from __future__ import annotations

import logging
from pathlib import Path

from ..casc.source import ScrSource
from ..errors import ConverterError
from ..formats import dds
from .files import link_file

log = logging.getLogger(__name__)

VARIANTS = {"hd2": ("HD2/", 2), "hd": ("", 4)}
SCHEMA = 2  # portraits.kpd's (hd_video)
DIFFUSE_BLOCK, TEAM_BLOCK = 16, 8  # bytes of a BC3 and a BC1 block


class HdError(ConverterError, ValueError):
    """Remastered's HD art does not match the original's images."""


def blocks(sheet: bytes) -> tuple[bytes, bytes]:
    """(FourCC, block data) of a DDS sheet: its first level without the header."""
    h = dds.header(sheet)
    if h.fourcc not in (b"DXT1", b"DXT5"):
        raise HdError(f"HD sheet in {h.fourcc!r}, BC1 or BC3 expected")
    step = TEAM_BLOCK if h.fourcc == b"DXT1" else DIFFUSE_BLOCK
    size = ((h.width + 3) // 4) * ((h.height + 3) // 4) * step
    data = sheet[dds.HEADER_SIZE:dds.HEADER_SIZE + size]
    if len(data) != size:
        raise HdError("HD sheet truncated")
    return h.fourcc, data


def generate(source: ScrSource, out: Path, variant: str, portraits: dict | None = None) -> dict:
    """Write generated/hd/<variant>/'s interface art; return counts. portraits: an earlier variant's counts of
    portraits.kpd in this run, linked instead of written again (every variant takes HD2's clips: hd_video)."""
    from . import hd_ui, hd_video
    prefix, scale = VARIANTS[variant]
    root = out / "hd" / variant
    profile_id, source_hash = source.profile.id, source.data_set_hash()
    read = lambda path: source.read(path)[1]  # noqa: E731
    ui = hd_ui.generate(read, source.storage, root, prefix, scale, profile_id, source_hash)
    if portraits is None:
        portraits = hd_video.generate(read, source.has, source.storage, root, profile_id, source_hash)
    else:
        link_file(Path(portraits["path"]), root / "portraits.kpd")
    log.info("hd %s: ui %s; portraits %s", variant, ui, portraits)
    return {"ui": ui, "portraits": portraits}
