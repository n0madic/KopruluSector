#!/usr/bin/env python3
"""Each input manifest file's normalized name, status and the SHA-256 of the bytes read from a StarCraft: Remastered
install (empty when none), a tab between, in the manifest's order: the twin of `kp_convert digests` (ADR 0018, stage
D6), which tools/ci.sh compares with it; the reads and rebuilds are scdata/casc's (source.py, rebuild.py).

    python3 tools/research/scr_digests.py --scr DIR --out FILE
"""
from __future__ import annotations

import argparse
import hashlib
import logging
import sys
from pathlib import Path

from scdata.casc.source import ScrSource
from scdata.mpq.profile import DEFAULT_PROFILE, Profile
from scdata.source import SourceError

log = logging.getLogger("scr_digests")


def main(argv: list[str] | None = None) -> int:
    logging.basicConfig(level=logging.INFO, format="%(message)s")
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--scr", required=True, help="install directory holding .build.info and Data/")
    parser.add_argument("--out", required=True, help="the digests' file")
    parser.add_argument("--profile", default=DEFAULT_PROFILE)
    args = parser.parse_args(argv)
    lines = []
    with ScrSource(args.scr, Profile.load(args.profile)) as source:
        # the order kp_res reads them in (the compiled manifest's): a rebuild's palette is kept per group and folder
        # for those after it, so the same order gives the same files
        for key in sorted(source.manifest.files):
            name = source.manifest.files[key].name
            try:
                _, data = source.read(name)
                digest = hashlib.sha256(data).hexdigest()
            except SourceError:
                digest = ""
            lines.append(f"{key}\t{source.status.get(name, ('missing', ''))[0]}\t{digest}\n")
    Path(args.out).write_text("".join(lines))
    log.info("%s: %d files", args.out, len(lines))
    return 0


if __name__ == "__main__":
    sys.exit(main())
