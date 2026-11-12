"""The game's global data as the runtime builds it from the original files (ADR 0018), in memory: dat, strings,
iscript, aiscript, fonts, palettes, portraits, sounds, tilesets, ui, grpmeta and campaign containers.

`build` makes every one; `scr-manifest` runs it over the reference install so that the input manifest holds each file they
read. Nothing is written: the runtime reads the original files (stage D5).
"""
from __future__ import annotations

from . import campaign
from .analyze import iscript as iscript_table
from .formats import dat, requirements
from .formats.palette import TILESETS
from .kpf import kpd
from .kpf.container import Container
from .source import Source


def _read(source: Source):
    return lambda path: source.read(path)[1]


def _ident(source: Source) -> dict:
    return {"profile_id": source.profile.id, "data_set_hash": source.data_set_hash()}


def strings(source: Source) -> Container:
    return kpd.build_strings({p: source.read(p)[1] for p in kpd.TBL_FILES.values()}, **_ident(source))


def ui(source: Source) -> Container:
    return kpd.build_ui(_read(source), has=source.has, **_ident(source))


def fonts(source: Source) -> Container:
    return kpd.build_fonts(_read(source), **_ident(source))


def sounds(source: Source) -> Container:
    return kpd.build_sounds(_read(source), source.has, **_ident(source))


def build(source: Source) -> dict[str, Container]:
    """Every container by its name (a tileset's `tilesets/<name>`)."""
    profile = source.profile
    ident = _ident(source)
    read = _read(source)
    layouts = dat.load_layouts(profile.id)
    dat_files = {f.path: source.read(f.path)[1] for f in layouts}
    reqs = {t.fourcc: requirements.section(list(t.words)) for t in requirements.load_tables(profile.id)}
    out = {
        "dat": kpd.build_dat(dat_files, layouts, requirements=reqs, **ident),
        "strings": strings(source),
        "iscript": kpd.build_iscript(read(kpd.ISCRIPT_FILE), iscript_table.Table.load(profile.id).header_animations,
                                     **ident),
        "aiscript": kpd.build_aiscript(read(kpd.AISCRIPT_FILE), read(kpd.BWSCRIPT_FILE), **ident),
        "fonts": fonts(source),
        "ui": ui(source),
        "sounds": sounds(source),
        "palettes": kpd.build_palettes(read, **ident),
    }
    for name in TILESETS:
        files = {ext: read(f"tileset\\{name}.{ext}") for ext in kpd.TILESET_EXTENSIONS}
        out[f"tilesets/{name}"] = kpd.build_tileset(name, files, **ident)
    out["portraits"] = kpd.build_portraits(read, source.has, **ident)
    gs = kpd.grp_set(*kpd.grp_references(read("arr\\images.dat"), read("arr\\images.tbl")), read)
    out["grpmeta"] = kpd.build_grpmeta(gs, **ident)
    out["campaign"] = campaign.build_from(source)
    return out
