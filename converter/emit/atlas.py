"""Indexed GRP atlases: frame PNGs + .atlas files.

Plan §5.1 / D10: transparency comes from the RLE packet type and index 0 may be opaque.
Three encodings are emitted (spike 07 compares them):
  LA     one texture, luminance = index, alpha = opacity (TEXTURE_FORMAT_LUMINANCE_ALPHA);
  INDEX  one texture, luminance = index with 0 meaning transparent; only valid when the
         GRP has no opaque index-0 pixel (checked here, see grp-opaque-zero evidence);
  MASK   one texture, luminance = opacity (255/0); paired with INDEX-like data when a GRP
         does need opaque index 0. Single-channel textures (TEXTURE_FORMAT_LUMINANCE).
"""
from __future__ import annotations

import io
import logging
from dataclasses import dataclass, field
from enum import Enum
from pathlib import Path

from PIL import Image

from ..formats.grp import Frame, Grp
from .defold import project_path, quote, write_if_changed

log = logging.getLogger(__name__)


class Encoding(Enum):
    LA = "la"
    INDEX = "index"
    MASK = "mask"


class OpaqueZeroError(ValueError):
    """An INDEX encoding was requested for a GRP that draws opaque index 0."""


class Layout(Enum):
    CROP = "crop"  # each image is the frame rectangle; offsets live in grpmeta
    CANVAS = "canvas"  # each image is the full GRP canvas; the atlas trims it


@dataclass
class Animation:
    id: str
    images: list[str] = field(default_factory=list)  # Defold resource paths


@dataclass
class AtlasSpec:
    animations: list[Animation]
    max_page_size: int = 0  # 0: single texture; >0: paged atlas (texture array)
    margin: int = 0
    extrude_borders: int = 0
    inner_padding: int = 0
    trim_mode: str = "SPRITE_TRIM_MODE_OFF"

    def to_text(self) -> str:
        out = []
        for a in self.animations:
            out.append("animations {")
            out.append(f"  id: {quote(a.id)}")
            for img in a.images:
                out.append(f"  images {{\n    image: {quote(img)}\n    sprite_trim_mode: {self.trim_mode}\n  }}")
            out.append("  playback: PLAYBACK_NONE\n  fps: 24\n  flip_horizontal: 0\n  flip_vertical: 0\n}")
        out.append(f"margin: {self.margin}")
        out.append(f"extrude_borders: {self.extrude_borders}")
        out.append(f"inner_padding: {self.inner_padding}")
        out.append(f"max_page_width: {self.max_page_size}")
        out.append(f"max_page_height: {self.max_page_size}")
        return "\n".join(out) + "\n"


_MODES = {Encoding.LA: ("LA", (0, 0)), Encoding.INDEX: ("L", 0), Encoding.MASK: ("L", 0)}


def _crop_bytes(frame: Frame, encoding: Encoding, opaque_zero: bool) -> bytes:
    if encoding is Encoding.LA:
        return bytes(b for pair in zip(frame.indices, frame.mask) for b in pair)
    if encoding is Encoding.MASK:
        return frame.mask
    if not opaque_zero and frame.indices.count(0) > frame.mask.count(0):
        raise OpaqueZeroError("frame draws opaque index 0; use LA or INDEX+MASK")
    return frame.indices


def frame_image(frame: Frame, grp: Grp, layout: Layout, encoding: Encoding, opaque_zero: bool = False) -> Image.Image:
    """Image of one frame. Empty frames become one transparent pixel (Defold rejects 0x0).

    `opaque_zero` accepts an opaque index 0 in INDEX images, for atlases paired with a MASK atlas.
    """
    mode, clear = _MODES[encoding]
    if layout is Layout.CROP:
        if frame.width == 0 or frame.height == 0:
            return Image.new(mode, (1, 1), clear)
        return Image.frombytes(mode, (frame.width, frame.height), _crop_bytes(frame, encoding, opaque_zero))
    canvas = Image.new(mode, (grp.width, grp.height), clear)
    if frame.width and frame.height:
        canvas.paste(frame_image(frame, grp, Layout.CROP, encoding, opaque_zero), (frame.x, frame.y))
    return canvas


def png_bytes(image: Image.Image) -> bytes:
    buf = io.BytesIO()
    image.save(buf, format="PNG", optimize=False)
    return buf.getvalue()


def write_grp_frames(grp: Grp, directory: Path, layout: Layout, encoding: Encoding,
                     opaque_zero: bool = False) -> list[str]:
    """Write one PNG per frame (all frames, in order) when changed; return their Defold paths."""
    paths = []
    for i, frame in enumerate(grp.frames):
        path = directory / f"{i:03d}.png"
        write_if_changed(path, png_bytes(frame_image(frame, grp, layout, encoding, opaque_zero)))
        paths.append(project_path(path))
    return paths


def write_atlas(path: Path, spec: AtlasSpec) -> None:
    write_if_changed(path, spec.to_text())
    log.info("%s: %d animations, %d images, page %s", path.name, len(spec.animations),
             sum(len(a.images) for a in spec.animations), spec.max_page_size or "single")


def animation_id(mpq_path: str) -> str:
    """'unit\\terran\\marine.grp' -> 'terran_marine' (lower case, unique per GRP path)."""
    stem = mpq_path.lower().removeprefix("unit\\").removesuffix(".grp")
    return stem.replace("\\", "_")


def _profile(name: str, fmt: str, premultiply: bool) -> str:
    return f"""profiles {{
  name: "{name}"
  platforms {{
    os: OS_ID_GENERIC
    formats {{
      format: {fmt}
      compression_level: FAST
      compression_type: COMPRESSION_TYPE_DEFAULT
    }}
    mipmaps: false
    max_texture_size: 0
    premultiply_alpha: {str(premultiply).lower()}
  }}
}}
"""


def texture_profiles(rules: list[tuple[str, str]]) -> str:
    """Texture profiles for indexed atlases. rules: (path glob, "la" | "l8"), first match wins.

    Both indexed profiles are uncompressed, without mipmaps and without premultiplied alpha.
    """
    out = "".join(f'path_settings {{\n  path: "{glob}"\n  profile: "{kind}"\n}}\n' for glob, kind in rules)
    out += 'path_settings {\n  path: "**"\n  profile: "Default"\n}\n'
    out += _profile("la", "TEXTURE_FORMAT_LUMINANCE_ALPHA", False)
    out += _profile("l8", "TEXTURE_FORMAT_LUMINANCE", False)
    out += _profile("Default", "TEXTURE_FORMAT_RGBA", True)
    return out
