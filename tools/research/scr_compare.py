#!/usr/bin/env python3
"""StarCraft: Remastered's DAT tables and scripts against profile bw-1.17.0.1's effective copies (docs/research/
remastered.md; the Python converter's `scr-compare` until ADR 0019's stage C7). Exit status 1 when any differ.

    python3 tools/research/scr_compare.py --data DIR --scr DIR
"""
from __future__ import annotations

import argparse
import logging
import sys

from scdata.casc.compare import compare, rules_files
from scdata.casc.storage import CascStorage
from scdata.mpq.profile import DEFAULT_PROFILE, Profile, Resolver

log = logging.getLogger("scr_compare")


def main(argv: list[str] | None = None) -> int:
    logging.basicConfig(level=logging.INFO, format="%(message)s")
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--data", required=True, help="the 1.17 install (the profile's archives)")
    parser.add_argument("--scr", required=True, help="install directory holding .build.info and Data/")
    parser.add_argument("--profile", default=DEFAULT_PROFILE)
    args = parser.parse_args(argv)
    with Resolver(args.data, Profile.load(args.profile)) as resolver, CascStorage(args.scr) as storage:
        result = compare(resolver, storage, rules_files())
        version = storage.version
    for c in result:
        log.info("%s %s: %s %d bytes, CASC %d bytes", "same" if c.equal else "DIFF", c.name, c.provider,
                 c.profile_size, c.casc_size)
    differ = sum(not c.equal for c in result)
    log.info("CASC %s against profile %s: %d of %d files differ", version, args.profile, differ, len(result))
    return 1 if differ else 0


if __name__ == "__main__":
    sys.exit(main())
