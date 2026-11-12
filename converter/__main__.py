"""Converter CLI. Run from the repository root: python3 -m converter <command> ...

kp_convert (native/kp_convert, ADR 0019) has verify, extract, the repack, the test scenarios, the terrain checks and
the icon; what is left here goes to it by stage C7 (docs/phaseC.md).

Commands:
  console --scr DIR [--out generated]
                               write the console's pieces generated/data/console.kpd from StarCraft:
                               Remastered's art (the only SD container the converter still writes; `hd`
                               writes it too)
  scr-manifest --data DIR [--scr DIR] [--out generated]
                               read everything the conversion reads from the profile's install (the
                               game's data, the campaign's scenarios and sounds, the test scenarios and
                               the terrain, all in memory) and write defs/scr/inputs-<profile>.json, the
                               hashes --scr is checked against, and defs/campaign/packs-<profile>.json,
                               the missions' resource packs; the scenarios' CHKs and the terrain checks
                               go to --out; with --scr also defs/scr/texts-<profile>.json
  hd --scr DIR [--variant hd2|hd|all] [--out generated]
                               write StarCraft: Remastered's HD art (x2, x4) as generated/hd/<variant>/
                               packs/<pack>.kpd and tilesets/<name>.kpd for the HD renderer (--graphics)
  cinematics --data DIR|--scr DIR [--variant sd|hd2|all] [--subtitles auto|none|LOC] [--optional]
             [--out generated]
                               write the optional player's generated/video/cinematics-<variant>.kpd: with
                               --scr Remastered's movies and subtitles as they are; with --data the 1.17
                               install's Smacker movies transcoded into the SD set (FFmpeg with libvpx-vp9
                               and oggenc; reused from generated/cache/cinematics/); --optional: without
                               those tools a warning instead of an error
  dump FILE [--section XXXX]   print a .kpd container in readable form
  scr-compare --data DIR --scr DIR
                               compare Remastered's DAT tables and scripts with the profile's
                               effective copies (exit status 1 if any differ)
  scr-digests --scr DIR --out FILE
                               write each manifest file's normalized name, status and the SHA-256 of
                               the bytes read from a StarCraft: Remastered install (empty when none), a
                               tab between, in the manifest's order: the twin of kp_convert digests
                               (ADR 0018, stage D6; tools/ci.sh compares them)
"""
from __future__ import annotations

import argparse
import contextlib
import logging
import sys
from pathlib import Path

from .errors import ConverterError
from .mpq.profile import DEFAULT_PROFILE, Profile, Resolver
from .mpq.stormlib import StormLibError

log = logging.getLogger("converter")


def _open_source(args: argparse.Namespace):
    """The 1.17 install (`--data`) or a StarCraft: Remastered install (`--scr`) as the profile's files."""
    profile = Profile.load(args.profile)
    if args.scr:
        from .casc.source import ScrSource

        return ScrSource(args.scr, profile)
    return Resolver(args.data, profile)


def _console(args: argparse.Namespace) -> int:
    """The console's pieces (generated/data/console.kpd) from StarCraft: Remastered's art."""
    from .casc.source import ScrSource
    from .emit import hd_ui

    with ScrSource(args.scr, Profile.load(args.profile)) as source:
        hd_ui.write_console(source, Path(args.out))
    return 0


def _scr_manifest(args: argparse.Namespace) -> int:
    """Read everything the conversion reads from the 1.17 install, recording the files read, and write the manifest and
    the missions' packs (defs/campaign/packs-<profile>.json); with --scr also the text tables made from both installs.
    The game's data and the campaign's scenarios are built in memory (the runtime reads the original files); the test
    scenarios' CHKs and the terrain checks go to --out."""
    from . import gamedata, missions, scenarios
    from .casc import rebuild
    from .casc.source import build_texts, write_manifest, write_texts
    from .inputs import damaged_entries, manifest_path, texts_path
    from .emit import terrain
    from .source import RecordingSource

    profile = Profile.load(args.profile)
    out = Path(args.out)
    with RecordingSource(Resolver(args.data, profile, reference=True), rebuild.describe) as source:
        survey = missions.survey(source)
        gamedata.build(source)
        scenario_packs = scenarios.convert(source, out)
        terrain.generate(source, out)
        missions.write_packs(missions.packs_path(profile.id), profile.id, survey.packs, scenario_packs)
        path = manifest_path(profile.id)
        files = {**source.files, **damaged_entries(profile.id, source.damaged)}
        for name in sorted(source.damaged, key=str.lower):
            log.warning("%s: the reference install's copy cannot be read whole: the manifest's entry is kept", name)
        write_manifest(path, profile.id, files, source.absent)
        log.info("%s: %d files (%d damaged in the install), %d absent", path, len(files), len(source.damaged),
                 len(source.absent - set(files)))
        if args.scr:
            from .casc.storage import CascStorage

            with CascStorage(args.scr) as storage:
                texts = build_texts(lambda n: source.inner.read(n)[1], list(source.files), storage)
            write_texts(texts_path(profile.id), profile.id, texts)
            log.info("%s: %d string tables, %d dialogs", texts_path(profile.id), len(texts["tbl"]), len(texts["bin"]))
    return 0


def _hd(args: argparse.Namespace) -> int:
    """StarCraft: Remastered's HD art for the HD renderer (generated/hd/<variant>/; ADR 0013)."""
    from .casc.source import ScrSource
    from .emit import hd, hd_ui

    with ScrSource(args.scr, Profile.load(args.profile)) as source:
        portraits = None  # written once, linked into the other variant (both take HD2's clips)
        for variant in (hd.VARIANTS if args.variant == "all" else (args.variant,)):
            portraits = hd.generate(source, source.storage, Path(args.out), variant, portraits)["portraits"]
        hd_ui.write_console(source, Path(args.out))
    return 0


def _cinematics(args: argparse.Namespace) -> int:
    """The cinematics for the optional player (generated/video/cinematics-<variant>.kpd; ADR 0015): StarCraft:
    Remastered's as they are, or the 1.17 install's Smacker movies transcoded into the SD set. --optional: without
    the tools they need, a warning and nothing written."""
    from .emit import cinematics, cinematics_smk

    out = Path(args.out)
    if args.data:
        if args.variant not in ("sd", "all"):
            raise cinematics.CinematicsError("the 1.17 install has only the SD movies; --variant hd2 needs --scr")
        if args.subtitles not in (cinematics.SUBTITLES_AUTO, cinematics.SUBTITLES_NONE):
            raise cinematics.CinematicsError("the 1.17 install has no subtitles; --subtitles needs --scr")
        # StarCraft: Remastered's SD set wins over the 1.17 movies whichever was converted first
        kept = cinematics.remastered_pack(out, "sd", Profile.load(args.profile).data_set_hash())
        if kept is not None:
            log.info("cinematics sd: %s is StarCraft: Remastered's set, kept (remove it to convert the 1.17 movies)",
                     kept)
            return 0
    try:
        (cinematics_smk.require if args.data else cinematics.require)()
    except cinematics.CinematicsToolsError as e:
        if not args.optional:
            raise
        log.warning("cinematics: none written: %s", e)
        return 0
    with _open_source(args) as source:
        if args.data:
            # closed here, not when collected: the encodings stop before the archives close
            with contextlib.closing(cinematics_smk.movies(source, out / cinematics_smk.CACHE)) as movies:
                cinematics.generate(movies, out, "sd", source.profile.id, source.data_set_hash())
            return 0
        subs = cinematics.subtitles(source.storage, cinematics.table()[0], args.subtitles)
        for variant in (cinematics.VARIANTS if args.variant == "all" else (args.variant,)):
            cinematics.generate(cinematics.remastered(source.storage, variant), out, variant, source.profile.id,
                                source.data_set_hash(), subs)
    return 0


def _dump(args: argparse.Namespace) -> int:
    from .kpf.dump import dump

    print(dump(Path(args.file).read_bytes(), args.section))
    return 0


def _scr_compare(args: argparse.Namespace) -> int:
    from .casc.compare import compare, rules_files
    from .casc.storage import CascStorage

    with Resolver(args.data, Profile.load(args.profile)) as resolver, \
            CascStorage(args.scr) as storage:
        result = compare(resolver, storage, rules_files())
        version = storage.version
    for c in result:
        log.info("%s %s: %s %d bytes, CASC %d bytes", "same" if c.equal else "DIFF", c.name, c.provider,
                 c.profile_size, c.casc_size)
    differ = sum(not c.equal for c in result)
    log.info("CASC %s against profile %s: %d of %d files differ", version, args.profile, differ, len(result))
    return 1 if differ else 0


def _scr_digests(args: argparse.Namespace) -> int:
    import hashlib

    from .casc.source import ScrSource
    from .source import SourceError

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


def _source_arguments(p: argparse.ArgumentParser) -> None:
    group = p.add_mutually_exclusive_group(required=True)
    group.add_argument("--data", help="the profile's install (StarCraft.exe and the MPQs)")
    group.add_argument("--scr", help="a StarCraft: Remastered install (.build.info and Data/) instead")


def main(argv: list[str] | None = None) -> int:
    logging.basicConfig(level=logging.INFO, format="%(message)s")
    parser = argparse.ArgumentParser(prog="converter")
    parser.add_argument("--profile", default=DEFAULT_PROFILE)
    sub = parser.add_subparsers(dest="command", required=True)
    p = sub.add_parser("console", help="write the console's pieces from StarCraft: Remastered's art")
    p.add_argument("--scr", required=True, help="a StarCraft: Remastered install (.build.info and Data/)")
    p.add_argument("--out", default="generated")
    p.set_defaults(func=_console)
    p = sub.add_parser("scr-manifest", help="read everything from the profile's install and record the Remastered "
                                            "manifest and the missions' packs")
    p.add_argument("--data", required=True)
    p.add_argument("--scr", help="a StarCraft: Remastered install: also write the text tables")
    p.add_argument("--out", default="generated")
    p.set_defaults(func=_scr_manifest)
    p = sub.add_parser("hd", help="write StarCraft: Remastered's HD art for the HD renderer")
    p.add_argument("--scr", required=True, help="a StarCraft: Remastered install (.build.info and Data/)")
    p.add_argument("--variant", choices=("hd2", "hd", "all"), default="all")
    p.add_argument("--out", default="generated")
    p.set_defaults(func=_hd)
    p = sub.add_parser("cinematics", help="write the cinematics for the optional player: Remastered's, or the 1.17 "
                                          "install's transcoded into the SD set")
    _source_arguments(p)
    p.add_argument("--variant", choices=("sd", "hd2", "all"), default="all",
                   help="with --scr the sets to write; the 1.17 install has only the SD set")
    p.add_argument("--subtitles", default="auto",
                   help="with --scr the movies' subtitles: auto (the install's text locale, else enUS), none, or a "
                        "locale; the 1.17 install has none")
    p.add_argument("--optional", action="store_true",
                   help="without the tools the cinematics need (FFmpeg, its libvpx-vp9 encoder for --data, oggenc), "
                        "warn and write nothing instead of failing")
    p.add_argument("--out", default="generated")
    p.set_defaults(func=_cinematics)
    p = sub.add_parser("dump", help="print a Koprulu container")
    p.add_argument("file")
    p.add_argument("--section")
    p.set_defaults(func=_dump)
    p = sub.add_parser("scr-compare", help="compare Remastered's DAT tables and scripts with the profile")
    p.add_argument("--data", required=True)
    p.add_argument("--scr", required=True, help="install directory holding .build.info and Data/")
    p.set_defaults(func=_scr_compare)
    p = sub.add_parser("scr-digests", help="write the digests of the manifest's files read from Remastered")
    p.add_argument("--scr", required=True, help="install directory holding .build.info and Data/")
    p.add_argument("--out", required=True)
    p.set_defaults(func=_scr_digests)
    args = parser.parse_args(argv)
    try:
        return args.func(args)
    except (ConverterError, StormLibError) as exc:
        log.error("%s", exc)
        return 1


if __name__ == "__main__":
    sys.exit(main())
