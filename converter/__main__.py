"""Converter CLI. Run from the repository root: python3 -m converter <command> ...

Commands:
  verify --data DIR            check StarCraft.exe and the profile MPQs (sizes and SHA-256)
  extract --data DIR PATH OUT  write the effective copy of an MPQ path to OUT
"""
from __future__ import annotations

import argparse
import logging
import sys
from pathlib import Path

from .mpq.profile import DEFAULT_PROFILE, Profile, ProfileError, Resolver, verify_file
from .mpq.stormlib import StormLibError

log = logging.getLogger("converter")


def _verify(args: argparse.Namespace) -> int:
    profile = Profile.load(args.profile)
    for spec in (profile.executable, *profile.archives):
        verify_file(Path(args.data), spec)
        log.info("ok %s", spec.name)
    log.info("profile %s verified", profile.id)
    return 0


def _extract(args: argparse.Namespace) -> int:
    with Resolver(args.data, Profile.load(args.profile), full_hash=False) as resolver:
        archive, data = resolver.read(args.path)
    Path(args.out).write_bytes(data)
    log.info("%s: %d bytes from %s", args.path, len(data), archive)
    return 0


def main(argv: list[str] | None = None) -> int:
    logging.basicConfig(level=logging.INFO, format="%(message)s")
    parser = argparse.ArgumentParser(prog="converter")
    parser.add_argument("--profile", default=DEFAULT_PROFILE)
    sub = parser.add_subparsers(dest="command", required=True)
    p = sub.add_parser("verify", help="verify original files against the profile")
    p.add_argument("--data", required=True)
    p.set_defaults(func=_verify)
    p = sub.add_parser("extract", help="extract the effective copy of an MPQ path")
    p.add_argument("--data", required=True)
    p.add_argument("path", help="MPQ path, e.g. 'arr\\\\units.dat'")
    p.add_argument("out")
    p.set_defaults(func=_extract)
    args = parser.parse_args(argv)
    try:
        return args.func(args)
    except (ProfileError, StormLibError) as exc:
        log.error("%s", exc)
        return 1


if __name__ == "__main__":
    sys.exit(main())
