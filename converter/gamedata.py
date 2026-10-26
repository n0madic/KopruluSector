"""Convert the global game data into `generated/data/`: dat, strings, iscript, aiscript,
fonts, palettes, portraits, sounds, tilesets, ui, grpmeta, campaign."""
from __future__ import annotations

import logging
from pathlib import Path

from . import campaign
from .emit.files import write_if_changed
from .analyze import iscript as iscript_table
from .formats import dat, requirements
from .formats.palette import TILESETS
from .kpf import kpd
from .source import Source

log = logging.getLogger(__name__)


def convert(resolver: Source, out: Path) -> list[Path]:
    profile = resolver.profile
    data_set = resolver.data_set_hash()
    layouts = dat.load_layouts(profile.id)
    dat_files = {f.path: resolver.read(f.path)[1] for f in layouts}
    tbl_files = {p: resolver.read(p)[1] for p in kpd.TBL_FILES.values()}
    reqs = {t.fourcc: requirements.section(list(t.words)) for t in requirements.load_tables(profile.id)}
    outputs = {
        out / "data" / "dat.kpd": kpd.build_dat(dat_files, layouts, profile_id=profile.id, data_set_hash=data_set,
                                                requirements=reqs),
        out / "data" / "strings.kpd": kpd.build_strings(tbl_files, profile_id=profile.id, data_set_hash=data_set),
        out / "data" / "iscript.kpd": kpd.build_iscript(resolver.read(kpd.ISCRIPT_FILE)[1],
                                                        iscript_table.Table.load(profile.id).header_animations,
                                                        profile_id=profile.id, data_set_hash=data_set),
        out / "data" / "aiscript.kpd": kpd.build_aiscript(resolver.read(kpd.AISCRIPT_FILE)[1],
                                                          resolver.read(kpd.BWSCRIPT_FILE)[1],
                                                          profile_id=profile.id, data_set_hash=data_set),
        out / "data" / "fonts.kpd": kpd.build_fonts(lambda p: resolver.read(p)[1], profile_id=profile.id,
                                                    data_set_hash=data_set),
        out / "data" / "ui.kpd": kpd.build_ui(lambda p: resolver.read(p)[1], profile_id=profile.id,
                                              data_set_hash=data_set, has=resolver.has),
        out / "data" / "sounds.kpd": kpd.build_sounds(lambda p: resolver.read(p)[1], resolver.has,
                                                      profile_id=profile.id, data_set_hash=data_set),
        out / "data" / "palettes.kpd": kpd.build_palettes(lambda p: resolver.read(p)[1], profile_id=profile.id,
                                                          data_set_hash=data_set),
    }
    for name in TILESETS:
        files = {ext: resolver.read(f"tileset\\{name}.{ext}")[1] for ext in kpd.TILESET_EXTENSIONS}
        outputs[out / "data" / "tilesets" / f"{name}.kpd"] = kpd.build_tileset(
            name, files, profile_id=profile.id, data_set_hash=data_set)
    outputs[out / "data" / "portraits.kpd"] = kpd.build_portraits(lambda p: resolver.read(p)[1], resolver.has,
                                                                  profile_id=profile.id, data_set_hash=data_set)
    gs = kpd.grp_set(*kpd.grp_references(resolver.read("arr\\images.dat")[1], resolver.read("arr\\images.tbl")[1]),
                     lambda p: resolver.read(p)[1])
    outputs[out / "data" / "grpmeta.kpd"] = kpd.build_grpmeta(gs, profile_id=profile.id, data_set_hash=data_set)
    path, container = campaign.convert(resolver, out)
    outputs[path] = container
    for path, container in outputs.items():
        data = container.to_bytes()
        changed = write_if_changed(path, data)
        log.info("%s %s: %d bytes, %d sections", "wrote" if changed else "same ", path, len(data),
                 len(container.sections))
    return list(outputs)

