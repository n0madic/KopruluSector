"""Convert the global game data into `generated/data/`: dat, strings, iscript, aiscript."""
from __future__ import annotations

import logging
from pathlib import Path

from .emit.defold import write_if_changed
from .formats import dat
from .kpf import kpd
from .mpq.profile import Resolver

log = logging.getLogger(__name__)


def convert(resolver: Resolver, out: Path) -> list[Path]:
    profile = resolver.profile
    data_set = profile.data_set_hash()
    layouts = dat.load_layouts(profile.id)
    dat_files = {f.path: resolver.read(f.path)[1] for f in layouts}
    tbl_files = {p: resolver.read(p)[1] for p in kpd.TBL_FILES.values()}
    outputs = {
        out / "data" / "dat.kpd": kpd.build_dat(dat_files, layouts, profile_id=profile.id, data_set_hash=data_set),
        out / "data" / "strings.kpd": kpd.build_strings(tbl_files, profile_id=profile.id, data_set_hash=data_set),
        out / "data" / "iscript.kpd": kpd.build_iscript(resolver.read(kpd.ISCRIPT_FILE)[1], profile_id=profile.id,
                                                        data_set_hash=data_set),
        out / "data" / "aiscript.kpd": kpd.build_aiscript(resolver.read(kpd.AISCRIPT_FILE)[1],
                                                          resolver.read(kpd.BWSCRIPT_FILE)[1],
                                                          profile_id=profile.id, data_set_hash=data_set),
    }
    for path, container in outputs.items():
        data = container.to_bytes()
        changed = write_if_changed(path, data)
        log.info("%s %s: %d bytes, %d sections", "wrote" if changed else "same ", path, len(data),
                 len(container.sections))
    return list(outputs)
