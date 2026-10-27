"""Converter CLI. Run from the repository root: python3 -m converter <command> ...

Commands:
  verify --data DIR            check StarCraft.exe and the profile MPQs (sizes and SHA-256)
  extract --data DIR PATH OUT  write the effective copy of an MPQ path to OUT
  missions --data DIR|--scr DIR [--out generated]
                               convert every catalog scenario to .kpm and write reports/missions.json
  data --data DIR|--scr DIR [--out generated]
                               write data/dat.kpd, strings.kpd, iscript.kpd, aiscript.kpd
  scenarios --data DIR|--scr DIR [--out generated]
                               write the test scenarios of tests/scenarios/ as scenarios/<name>.kpm
  resources --data DIR|--scr DIR [--out generated]
                               write the pack containers packs/<pack>.kpd and packs/music.kpd, the
                               mission sounds missions/<campaign>/<mission>_sounds.kpd and the
                               terrain checks' viewer/<tileset>.mcrc (run `missions` and `data` first)
  scr-manifest --data DIR [--scr DIR] [--out generated]
                               convert everything from the profile's install and write
                               defs/scr/inputs-<profile>.json, the hashes --scr is checked against;
                               with --scr also defs/scr/texts-<profile>.json
  hd --scr DIR [--variant hd2|hd|all] [--out generated]
                               write StarCraft: Remastered's HD art (x2, x4) as generated/hd/<variant>/
                               packs/<pack>.kpd and tilesets/<name>.kpd for the HD renderer (--graphics)
  dump FILE [--section XXXX]   print a .kpd/.kpm container in readable form
  --scr DIR (missions, data, scenarios, resources) reads the profile's files from a StarCraft:
                               Remastered install instead (converter/casc/source.py) and writes
                               reports/scr-import.json
  scr-extract --scr DIR PATH OUT
                               write a file of a local StarCraft: Remastered CASC install to OUT
  scr-compare --data DIR --scr DIR
                               compare Remastered's DAT tables and scripts with the profile's
                               effective copies (exit status 1 if any differ)
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


def _open_source(args: argparse.Namespace):
    """The 1.17 install (`--data`) or a StarCraft: Remastered install (`--scr`) as the profile's files."""
    profile = Profile.load(args.profile)
    if args.scr:
        from .casc.source import CACHE, ScrSource

        return ScrSource(args.scr, profile, cache=Path(args.out) / CACHE)
    return Resolver(args.data, profile, full_hash=False)


def _run(args: argparse.Namespace, *steps) -> list:
    """Run conversion steps on one source; a Remastered source also reports each file's status."""
    with _open_source(args) as source:
        results = [step(source, Path(args.out)) for step in steps]
        if args.scr:
            counts = source.write_report(Path(args.out))
            log.info("StarCraft: Remastered %s: %s", source.version,
                     ", ".join(f"{n} {status}" for status, n in sorted(counts.items())))
    return results


def _missions(args: argparse.Namespace) -> int:
    from .missions import convert

    report, = _run(args, convert)
    log.info("%d missions converted, %d scenarios rejected by the native preflight",
             len(report["missions"]), len(report["rejected"]))
    return 0


def _data(args: argparse.Namespace) -> int:
    from .gamedata import convert

    _run(args, convert)
    return 0


def _scenarios(args: argparse.Namespace) -> int:
    from .scenarios import convert

    _run(args, convert)
    return 0


def _resources(args: argparse.Namespace) -> int:
    from .emit import packs, terrain

    _run(args, terrain.generate, packs.generate)
    return 0


def _scr_manifest(args: argparse.Namespace) -> int:
    """Convert everything from the 1.17 install, recording the files read, and write the manifest; with --scr also
    the text tables made from both installs."""
    from . import gamedata, missions, scenarios
    from .casc import rebuild
    from .casc.source import build_texts, manifest_path, texts_path, write_manifest, write_texts
    from .emit import packs, terrain
    from .source import RecordingSource

    profile = Profile.load(args.profile)
    out = Path(args.out)
    with RecordingSource(Resolver(args.data, profile, full_hash=False), rebuild.describe) as source:
        for step in (missions.convert, gamedata.convert, scenarios.convert, terrain.generate, packs.generate):
            step(source, out)
        path = manifest_path(profile.id)
        write_manifest(path, profile.id, source.files, source.absent, source.unreadable)
        log.info("%s: %d files, %d absent, %d unreadable", path, len(source.files),
                 len(source.absent - set(source.files)), len(source.unreadable))
        if args.scr:
            from .casc.storage import CascStorage

            with CascStorage(args.scr) as storage:
                texts = build_texts(lambda n: source.inner.read(n)[1], list(source.files), storage)
            write_texts(texts_path(profile.id), profile.id, texts)
            log.info("%s: %d string tables, %d dialogs", texts_path(profile.id), len(texts["tbl"]), len(texts["bin"]))
    return 0


def _hd(args: argparse.Namespace) -> int:
    """StarCraft: Remastered's HD art for the HD renderer (generated/hd/<variant>/; ADR 0013)."""
    from .casc.source import CACHE, ScrSource
    from .emit import hd

    with ScrSource(args.scr, Profile.load(args.profile), cache=Path(args.out) / CACHE) as source:
        portraits = None  # written once, linked into the other variant (both take HD2's clips)
        for variant in (hd.VARIANTS if args.variant == "all" else (args.variant,)):
            portraits = hd.generate(source, source.storage, Path(args.out), variant, portraits)["portraits"]
    return 0


def _dump(args: argparse.Namespace) -> int:
    from .kpf.dump import dump

    print(dump(Path(args.file).read_bytes(), args.section))
    return 0


def _scr_extract(args: argparse.Namespace) -> int:
    from .casc.storage import CascStorage

    with CascStorage(args.scr) as storage:
        data = storage.read(args.path)
        version = storage.version
    Path(args.out).write_bytes(data)
    log.info("%s: %d bytes from CASC %s", args.path, len(data), version)
    return 0


def _scr_compare(args: argparse.Namespace) -> int:
    from .casc.compare import compare, rules_files
    from .casc.storage import CascStorage

    with Resolver(args.data, Profile.load(args.profile), full_hash=False) as resolver, \
            CascStorage(args.scr) as storage:
        result = compare(resolver, storage, rules_files())
        version = storage.version
    for c in result:
        log.info("%s %s: %s %d bytes, CASC %d bytes", "same" if c.equal else "DIFF", c.name, c.provider,
                 c.profile_size, c.casc_size)
    differ = sum(not c.equal for c in result)
    log.info("CASC %s against profile %s: %d of %d files differ", version, args.profile, differ, len(result))
    return 1 if differ else 0


def _source_arguments(p: argparse.ArgumentParser) -> None:
    group = p.add_mutually_exclusive_group(required=True)
    group.add_argument("--data", help="the profile's install (StarCraft.exe and the MPQs)")
    group.add_argument("--scr", help="a StarCraft: Remastered install (.build.info and Data/) instead")


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
    _source_arguments(p)
    p.add_argument("--out", default="generated")
    p.set_defaults(func=_missions)
    p = sub.add_parser("data", help="convert DAT tables and string tables to .kpd")
    _source_arguments(p)
    p.add_argument("--out", default="generated")
    p.set_defaults(func=_data)
    p = sub.add_parser("scenarios", help="convert the test scenarios to .kpm")
    _source_arguments(p)
    p.add_argument("--out", default="generated")
    p.set_defaults(func=_scenarios)
    p = sub.add_parser("resources", help="write the pack and mission sound containers and the terrain checks' CRCs")
    _source_arguments(p)
    p.add_argument("--out", default="generated")
    p.set_defaults(func=_resources)
    p = sub.add_parser("scr-manifest", help="convert from the profile's install and record the Remastered manifest")
    p.add_argument("--data", required=True)
    p.add_argument("--scr", help="a StarCraft: Remastered install: also write the text tables")
    p.add_argument("--out", default="generated")
    p.set_defaults(func=_scr_manifest)
    p = sub.add_parser("hd", help="write StarCraft: Remastered's HD art for the HD renderer")
    p.add_argument("--scr", required=True, help="a StarCraft: Remastered install (.build.info and Data/)")
    p.add_argument("--variant", choices=("hd2", "hd", "all"), default="all")
    p.add_argument("--out", default="generated")
    p.set_defaults(func=_hd)
    p = sub.add_parser("dump", help="print a Koprulu container")
    p.add_argument("file")
    p.add_argument("--section")
    p.set_defaults(func=_dump)
    p = sub.add_parser("scr-extract", help="extract a file of a local StarCraft: Remastered CASC install")
    p.add_argument("--scr", required=True, help="install directory holding .build.info and Data/")
    p.add_argument("path", help="CASC name, e.g. 'arr/units.dat' (MPQ separators accepted)")
    p.add_argument("out")
    p.set_defaults(func=_scr_extract)
    p = sub.add_parser("scr-compare", help="compare Remastered's DAT tables and scripts with the profile")
    p.add_argument("--data", required=True)
    p.add_argument("--scr", required=True, help="install directory holding .build.info and Data/")
    p.set_defaults(func=_scr_compare)
    args = parser.parse_args(argv)
    try:
        return args.func(args)
    except (ConverterError, StormLibError) as exc:
        log.error("%s", exc)
        return 1


if __name__ == "__main__":
    sys.exit(main())
