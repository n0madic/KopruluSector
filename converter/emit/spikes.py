"""Generate the inputs of phase-0 spikes 07/08/09/11 under generated/spikes/.

Usage (repository root):
  python3 -m converter.emit.spikes --data DIR atlas|packs|tilemap|sound|all

Nothing generated here is committed (generated/ is gitignored); originals stay private.
"""
from __future__ import annotations

import argparse
import json
import logging
import sys
import time
from pathlib import Path

from ..formats import grp as grp_format
from ..formats import tbl
from ..mpq.profile import Resolver
from . import atlas, sound, tilemap
from .defold import ROOT, project_path, write_if_changed

log = logging.getLogger("converter.emit.spikes")

OUT = ROOT / "generated" / "spikes"
RACES = ("terran", "zerg", "protoss")


def race_grps(r: Resolver, race: str) -> list[str]:
    """Distinct images.dat GRP paths under unit\\<race>\\ in images.dat order."""
    paths = tbl.image_grp_paths(r.read("arr\\images.dat")[1], r.read("arr\\images.tbl")[1])
    seen: dict[str, None] = {}
    prefix = f"unit\\{race}\\"
    for p in paths:
        if p and p.lower().startswith(prefix) and p.lower() not in seen:
            seen[p.lower()] = None
    return list(seen)


Variant = tuple[atlas.Layout, atlas.Encoding, int, str]  # layout, encoding, max page size, trim mode


def build_race_atlases(r: Resolver, race: str, out: Path, variants: dict[str, Variant]) -> dict:
    """Decode all GRPs of a race and write frame PNGs plus one .atlas per variant."""
    started = time.perf_counter()
    grps = {name: grp_format.decode(r.read(name)[1]) for name in race_grps(r, race)}
    decode_s = time.perf_counter() - started
    manifest = {"race": race, "grps": [], "decode_seconds": round(decode_s, 3)}
    images: dict[tuple[atlas.Layout, atlas.Encoding], list[atlas.Animation]] = {}
    started = time.perf_counter()
    for layout, encoding in {v[:2] for v in variants.values()}:
        anims = []
        for name, g in grps.items():
            anim_id = atlas.animation_id(name)
            frames_dir = out / "frames" / f"{layout.value}_{encoding.value}" / anim_id
            anims.append(atlas.Animation(anim_id, atlas.write_grp_frames(g, frames_dir, layout, encoding)))
        images[layout, encoding] = anims
    manifest["png_seconds"] = round(time.perf_counter() - started, 3)
    for name, g in grps.items():
        # A probe pixel per GRP: the first opaque pixel of frame 0 (for render checks).
        f = g.frames[0]
        probe = next(((i % f.width, i // f.width, f.indices[i]) for i, m in enumerate(f.mask) if m), None)
        manifest["grps"].append({
            "id": atlas.animation_id(name), "path": name, "frames": len(g.frames),
            "canvas": [g.width, g.height], "frame0": [f.x, f.y, f.width, f.height], "probe": probe,
            "opaque_pixels": sum(fr.opaque_pixels for fr in g.frames),
            "crop_pixels": sum(fr.width * fr.height for fr in g.frames),
            "canvas_pixels": len(g.frames) * g.width * g.height,
        })
    for variant, (layout, encoding, page, trim) in variants.items():
        spec = atlas.AtlasSpec(images[layout, encoding], max_page_size=page, trim_mode=trim)
        atlas.write_atlas(out / f"{race}_{variant}.atlas", spec)
    write_if_changed(out / f"{race}.json", json.dumps(manifest, indent=1))
    return manifest


_CROP, _CANVAS = atlas.Layout.CROP, atlas.Layout.CANVAS
_LA, _INDEX, _MASK = atlas.Encoding.LA, atlas.Encoding.INDEX, atlas.Encoding.MASK
_NO_TRIM = "SPRITE_TRIM_MODE_OFF"
ATLAS_VARIANTS: dict[str, Variant] = {
    "la_crop": (_CROP, _LA, 0, _NO_TRIM),
    "index_crop": (_CROP, _INDEX, 0, _NO_TRIM),
    "mask_crop": (_CROP, _MASK, 0, _NO_TRIM),
    "index_crop_paged1024": (_CROP, _INDEX, 1024, _NO_TRIM),
    "mask_crop_paged1024": (_CROP, _MASK, 1024, _NO_TRIM),
    "index_canvas_trim8_paged2048": (_CANVAS, _INDEX, 2048, "SPRITE_TRIM_MODE_8"),
}


def write_profiles(out: Path) -> None:
    base = project_path(out)
    write_if_changed(out / "indexed.texture_profiles",
                     atlas.texture_profiles([(f"{base}/*_la_*", "la"), (f"{base}/**", "l8")]))


def gen_atlas(r: Resolver) -> None:
    out = OUT / "07-atlas"
    m = build_race_atlases(r, "terran", out, ATLAS_VARIANTS)
    write_profiles(out)
    log.info("terran: %d GRPs, %d frames, %d opaque px, %d crop px, %d canvas px",
             len(m["grps"]), sum(g["frames"] for g in m["grps"]), sum(g["opaque_pixels"] for g in m["grps"]),
             sum(g["crop_pixels"] for g in m["grps"]), sum(g["canvas_pixels"] for g in m["grps"]))


def gen_packs(r: Resolver) -> None:
    out = OUT / "08-packs"
    for race in RACES:
        build_race_atlases(r, race, out, {"pack": (_CROP, _INDEX, 0, _NO_TRIM)})
    write_profiles(out)


def gen_tilemap(r: Resolver) -> None:
    tilemap.generate(r, OUT / "09-tilemap")


def gen_sound(r: Resolver) -> None:
    sound.generate(r, OUT / "11-sound")


COMMANDS = {"atlas": gen_atlas, "packs": gen_packs, "tilemap": gen_tilemap, "sound": gen_sound}


def main(argv: list[str] | None = None) -> int:
    logging.basicConfig(level=logging.INFO, format="%(message)s")
    p = argparse.ArgumentParser(prog="converter.emit.spikes")
    p.add_argument("--data", required=True, help="directory with StarCraft.exe and the profile MPQs")
    p.add_argument("what", choices=[*COMMANDS, "all"])
    a = p.parse_args(argv)
    with Resolver(a.data, full_hash=False) as r:
        for name, fn in COMMANDS.items():
            if a.what in (name, "all"):
                started = time.perf_counter()
                fn(r)
                log.info("%s generated in %.1f s", name, time.perf_counter() - started)
    return 0


if __name__ == "__main__":
    sys.exit(main())
