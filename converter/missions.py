"""Convert every catalog scenario to `generated/missions/<campaign>/<mission>.kpm`.

Also writes:
- `generated/missions/index.txt`: one line per mission in catalog order,
  `<name> <tileset> <width> <height>` (read by the map viewer);
- `generated/reports/missions.json`: sections, ignored tags and mission sounds per map,
  and the physically present scenarios the native preflight rejects.
"""
from __future__ import annotations

import json
import logging
from pathlib import Path

from .catalog import EXTRA_SCENARIOS, MAPDATA_DAT, MAPDATA_TBL, SCENARIO_SUFFIX, decode_catalog
from .emit.defold import write_if_changed
from .formats import chk
from .kpf import kpm
from .mpq.profile import Resolver
from .pe import PeImage

log = logging.getLogger(__name__)


def convert(resolver: Resolver, out: Path) -> dict:
    tables = chk.LoaderTables.load(resolver.profile.id)
    vcod = PeImage.load(resolver.root / resolver.profile.executable.name).resource(*tables.vcod_resource)
    data_set = resolver.profile.data_set_hash()
    catalog = decode_catalog(resolver.read(MAPDATA_DAT)[1], resolver.read(MAPDATA_TBL)[1])
    exists = resolver.has
    report = {"profile": resolver.profile.id, "missions": [], "rejected": []}
    index = []
    for entry in catalog:
        scenario = chk.load(resolver.read(entry.chk_path)[1], tables, vcod=vcod)
        sounds = kpm.mission_sounds(scenario, entry.directory, exists)
        container = kpm.build(scenario, sounds, profile_id=resolver.profile.id, data_set_hash=data_set)
        path = out / "missions" / f"{entry.name}.kpm"
        changed = write_if_changed(path, container.to_bytes())
        index.append(f"{entry.name} {scenario.u16('ERA ') & 7} {scenario.u16('DIM ')} {scenario.u16('DIM ', 1)}")
        report["missions"].append({"catalog_index": entry.index, **kpm.manifest_entry(entry.name, scenario, sounds)})
        missing = sum(not s.exists for s in sounds)
        log.info("%s %s: v%d, %d sounds%s", "wrote" if changed else "same ", entry.name, scenario.version,
                 len(sounds), f" ({missing} missing)" if missing else "")
    for directory in EXTRA_SCENARIOS:
        try:
            chk.load(resolver.read(directory + SCENARIO_SUFFIX)[1], tables, vcod=vcod)
        except chk.ChkError as exc:
            report["rejected"].append({"directory": directory, "reason": str(exc)})
        else:
            raise chk.ChkError(f"{directory}: expected the native preflight to reject it")
    write_if_changed(out / "missions" / "index.txt", "\n".join(index) + "\n")
    write_if_changed(out / "reports" / "missions.json", json.dumps(report, indent=1, sort_keys=True) + "\n")
    return report
