"""Portrait atlases: one L8 image per SMK frame (palette indices of that movie, every
texel opaque, index 0 included), one animation per SMK, one atlas per portrait group
(first letter of the portrait directory: t, u, z, p, n) so each stays one page <= 4096^2
(ADR 0003). Palettes and timing are in portraits.kpd.

Outputs: generated/atlases/portraits/<animation>/<frame>.png, generated/atlases/portraits_<g>.atlas.
"""
from __future__ import annotations

import logging
from pathlib import Path

from PIL import Image

from ..formats import smk
from . import atlas, profiles
from .defold import project_path, write_if_changed

log = logging.getLogger(__name__)


def animation_id(path: str) -> str:
    """'portrait\\tmarine\\TMaFid00.smk' -> 'tmarine_tmafid00'."""
    return path.lower().removeprefix("portrait\\").removesuffix(".smk").replace("\\", "_")


def group(path: str) -> str:
    return path.lower().removeprefix("portrait\\")[0]


def write_atlases(movies: dict[str, tuple[smk.Header, list[smk.Frame]]], out: Path) -> list[Path]:
    groups: dict[str, list[atlas.Animation]] = {}
    for path, (header, frames) in movies.items():
        anim = animation_id(path)
        w, h = header.width, header.height
        images = []
        for i, f in enumerate(frames):
            png = out / "atlases" / "portraits" / anim / f"{i:03d}.png"
            write_if_changed(png, atlas.png_bytes(Image.frombytes("L", (w, h), f.indices)))
            images.append(project_path(png))
        groups.setdefault(group(path), []).append(atlas.Animation(anim, images))
    written = []
    for g, animations in sorted(groups.items()):
        path = out / "atlases" / f"portraits_{g}.atlas"
        atlas.write_atlas(path, atlas.AtlasSpec(animations))
        written.append(path)
    return written + [profiles.write(out)]
