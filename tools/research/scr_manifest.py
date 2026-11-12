#!/usr/bin/env python3
"""The committed tables of what profile bw-1.17.0.1's game reads from the 1.17 install (ADR 0018; ADR 0019, stage C7:
the Python converter's `scr-manifest`, kept here as the generator of committed tables it is, like tools/defs_c.py).

    python3 tools/research/scr_manifest.py --data DIR [--scr DIR]

Reads everything the game reads from the profile's install through scdata's models of the runtime's readers (the
game's data, the campaign's scenarios and sounds, the test scenarios and the terrain, built in memory; the scenarios'
CHKs and the terrain checks only into a temporary folder, kp_convert scenarios and terrain write the game's), recording
the files read, and writes defs/scr/inputs-<profile>.json (the hashes kp_res checks a StarCraft: Remastered install
against) and defs/campaign/packs-<profile>.json (the missions' and test scenarios' resource packs); with --scr also
defs/scr/texts-<profile>.json (how the string tables and dialogs are rebuilt from Remastered's). tools/defs_c.py
compiles all three.

TODO: kp_convert manifest (C, ADR 0019): the reads recorded by kp_res itself, so that a file the runtime starts to read
needs no twin here.
"""
from __future__ import annotations

import argparse
import logging
import sys
import tempfile
from pathlib import Path

from scdata import gamedata, missions, scenarios
from scdata.casc import rebuild
from scdata.casc.source import build_texts, write_manifest, write_texts
from scdata.casc.storage import CascStorage
from scdata.emit import terrain
from scdata.errors import ConverterError
from scdata.inputs import damaged_entries, manifest_path, texts_path
from scdata.mpq.profile import DEFAULT_PROFILE, Profile, Resolver
from scdata.mpq.stormlib import StormLibError
from scdata.source import RecordingSource

log = logging.getLogger("scr_manifest")


def main(argv: list[str] | None = None) -> int:
    logging.basicConfig(level=logging.INFO, format="%(message)s")
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--data", required=True, help="the 1.17 install (the profile's archives)")
    parser.add_argument("--scr", help="a StarCraft: Remastered install: also write the text tables")
    parser.add_argument("--profile", default=DEFAULT_PROFILE)
    args = parser.parse_args(argv)
    profile = Profile.load(args.profile)
    try:
        with RecordingSource(Resolver(args.data, profile, reference=True), rebuild.describe) as source, \
                tempfile.TemporaryDirectory() as out:
            survey = missions.survey(source)
            gamedata.build(source)
            scenario_packs = scenarios.convert(source, Path(out))
            terrain.generate(source, Path(out))
            missions.write_packs(missions.packs_path(profile.id), profile.id, survey.packs, scenario_packs)
            path = manifest_path(profile.id)
            files = {**source.files, **damaged_entries(profile.id, source.damaged)}
            for name in sorted(source.damaged, key=str.lower):
                log.warning("%s: the reference install's copy cannot be read whole: the manifest's entry is kept", name)
            write_manifest(path, profile.id, files, source.absent)
            log.info("%s: %d files (%d damaged in the install), %d absent", path, len(files), len(source.damaged),
                     len(source.absent - set(files)))
            if args.scr:
                with CascStorage(args.scr) as storage:
                    texts = build_texts(lambda n: source.inner.read(n)[1], list(source.files), storage)
                write_texts(texts_path(profile.id), profile.id, texts)
                log.info("%s: %d string tables, %d dialogs", texts_path(profile.id), len(texts["tbl"]),
                         len(texts["bin"]))
    except (ConverterError, StormLibError) as e:
        log.error("scr_manifest: %s", e)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
