"""The project's generated texture profiles: every indexed resource is uncompressed L8.

All emitters write the same file (generated/textures.texture_profiles) so the settings
overlay that builds generated data needs one `graphics.texture_profiles` entry.
"""
from __future__ import annotations

from pathlib import Path

from . import atlas
from .defold import project_path, write_if_changed

INDEXED_DIRECTORIES = ("tilesources", "fonts", "atlases")


def write(out: Path) -> Path:
    path = out / "textures.texture_profiles"
    rules = [(project_path(out / d) + "/**", "l8") for d in INDEXED_DIRECTORIES]
    write_if_changed(path, atlas.texture_profiles(rules))
    return path
