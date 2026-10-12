"""Converter CLI. Run from the repository root: python3 -m converter <command> ...

Commands:
  verify --data DIR            check StarCraft.exe and the profile MPQs (sizes and SHA-256)
  extract --data DIR PATH OUT  write the effective copy of an MPQ path to OUT
  missions --data DIR [--out generated]
                               convert every catalog scenario to .kpm and write reports/missions.json
  data --data DIR [--out generated]
                               write data/dat.kpd, strings.kpd, iscript.kpd, aiscript.kpd
  resources --data DIR [--out generated]
                               write Defold resources: tilesources, tilemap templates, mission
                               template collections, glyph atlas, pack atlases, sounds and
                               collections, mission assets collections (run `missions` and
                               `data` first)
  dump FILE [--section XXXX]   print a .kpd/.kpm container in readable form
"""
from __future__ import annotations

import argparse
import logging
import sys
from pathlib import Path

from .errors import ConverterError
from .mpq.profile import DEFAULT_PROFILE, Profile, Resolver, verify_file
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


def _missions(args: argparse.Namespace) -> int:
    from .missions import convert

    with Resolver(args.data, Profile.load(args.profile), full_hash=False) as resolver:
        report = convert(resolver, Path(args.out))
    log.info("%d missions converted, %d scenarios rejected by the native preflight",
             len(report["missions"]), len(report["rejected"]))
    return 0


def _data(args: argparse.Namespace) -> int:
    from .gamedata import convert

    with Resolver(args.data, Profile.load(args.profile), full_hash=False) as resolver:
        convert(resolver, Path(args.out))
    return 0


def _resources(args: argparse.Namespace) -> int:
    from .emit import fonts, packs, terrain

    with Resolver(args.data, Profile.load(args.profile), full_hash=False) as resolver:
        terrain.generate(resolver, Path(args.out))
        fonts.generate(resolver, Path(args.out))
        packs.generate(resolver, Path(args.out))
    return 0


def _dump(args: argparse.Namespace) -> int:
    from .kpf.dump import dump

    print(dump(Path(args.file).read_bytes(), args.section))
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
    p = sub.add_parser("missions", help="convert the campaign scenarios to .kpm")
    p.add_argument("--data", required=True)
    p.add_argument("--out", default="generated")
    p.set_defaults(func=_missions)
    p = sub.add_parser("data", help="convert DAT tables and string tables to .kpd")
    p.add_argument("--data", required=True)
    p.add_argument("--out", default="generated")
    p.set_defaults(func=_data)
    p = sub.add_parser("resources", help="write Defold resources")
    p.add_argument("--data", required=True)
    p.add_argument("--out", default="generated")
    p.set_defaults(func=_resources)
    p = sub.add_parser("dump", help="print a Koprulu container")
    p.add_argument("file")
    p.add_argument("--section")
    p.set_defaults(func=_dump)
    args = parser.parse_args(argv)
    try:
        return args.func(args)
    except (ConverterError, StormLibError) as exc:
        log.error("%s", exc)
        return 1


if __name__ == "__main__":
    sys.exit(main())
