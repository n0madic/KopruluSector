"""Glyph atlas: one L8 image per glyph, texel = colour-row entry + 1 (0 = untouched),
one animation per font with a frame per byte code from low + 1 to high (code c is frame
c - low - 1; space has no glyph and is laid out by advance only).

Outputs: generated/fonts/<font>/<code>.png, generated/fonts/fonts.atlas.
"""
from __future__ import annotations

import logging
from pathlib import Path

from .. import raster
from ..formats import fnt
from ..kpf import kpd
from ..mpq.profile import Resolver
from . import atlas, profiles
from .defold import project_path, write_if_changed

log = logging.getLogger(__name__)


def glyph_image(data: bytes, glyph: fnt.Glyph | None) -> raster.Image:
    if glyph is None or glyph.width == 0 or glyph.height == 0:
        return raster.new("L", (1, 1), 0)
    masks, _ = fnt.glyph_masks(data, glyph)
    return raster.frombytes("L", (glyph.width, glyph.height), masks)


def generate(resolver: Resolver, out: Path) -> list[Path]:
    animations, written = [], []
    for f in kpd.font_defs()["fonts"]:
        data = kpd.decoded_font(resolver.read(f["plaintext"])[1], f["decoded_sha256"], f["name"])
        font = fnt.decode(data)
        images = []
        for code in range(font.low + 1, font.high + 1):
            path = out / "fonts" / f["name"] / f"{code:03d}.png"
            write_if_changed(path, atlas.png_bytes(glyph_image(data, font.glyphs.get(code))))
            images.append(project_path(path))
            written.append(path)
        animations.append(atlas.Animation(f["name"], images))
        log.info("%s: %d glyphs", f["name"], len(font.glyphs))
    path = out / "fonts" / "fonts.atlas"
    atlas.write_atlas(path, atlas.AtlasSpec(animations, margin=0))
    return written + [path, profiles.write(out)]
