"""Converter CLI. Run from the repository root: python3 -m converter <command> ...

Commands:
  verify --data DIR            check StarCraft.exe (SHA-256) and the files the conversion reads from the
                               profile MPQs (SHA-256 against defs/scr/inputs-<profile>.json)
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
  cinematics --data DIR|--scr DIR [--variant sd|hd2|all] [--subtitles auto|none|LOC] [--optional]
             [--out generated]
                               write the optional player's generated/video/cinematics-<variant>.kpd: with
                               --scr Remastered's movies and subtitles as they are; with --data the 1.17
                               install's Smacker movies transcoded into the SD set (FFmpeg with libvpx-vp9
                               and oggenc; reused from generated/cache/cinematics/); --optional: without
                               those tools a warning instead of an error
  locale --scr DIR --locale LOC|all [--out generated]
                               write a language's overlay packs generated/locale/<LOC>/ (all: each the
                               install holds, others are told in the log) and the glyph
                               supplement generated/locale/glyphs.kpd (run `data` and `missions` first,
                               build the kp_fnt_make target; reports/locale-<LOC>.json has the checks)
  repack --data DIR|--scr DIR [--out DIR] [--single] [--res-log FILE] [--wav]
         [--cache generated/cache/repack-ogg]
                               write the koprulu*.mpq repack (ADR 0018, stage D8; converter/repack.py):
                               the files the game reads in their original formats, Remastered's
                               rebuilt 1.17 files from --scr, a WAV as Ogg Vorbis when that is smaller
                               (oggenc, each encoded once into --cache; --wav keeps the WAVs), the
                               names --res-log read unlisted; its groups (the core, a group per
                               mission, the music, the cinematics) into generated/repack, or with
                               --single one koprulu.mpq (the cinematics' group beside it) into
                               generated/repack-single
  dump FILE [--section XXXX]   print a .kpd/.kpm container in readable form
  --scr DIR (missions, data, scenarios, resources) reads the profile's files from a StarCraft:
                               Remastered install instead (converter/casc/source.py) and writes
                               reports/scr-import.json
  scr-extract --scr DIR PATH OUT
                               write a file of a local StarCraft: Remastered CASC install to OUT
  scr-compare --data DIR --scr DIR
                               compare Remastered's DAT tables and scripts with the profile's
                               effective copies (exit status 1 if any differ)
  scr-digests --scr DIR --out FILE
                               write each manifest file's normalized name, status and the SHA-256 of
                               the bytes read from a StarCraft: Remastered install (empty when none), a
                               tab between, in the manifest's order: the twin of kp_data_probe --digests
                               (ADR 0018, stage D6; tools/ci.sh compares them)
"""
from __future__ import annotations

import argparse
import contextlib
import logging
import sys
from pathlib import Path

from .errors import ConverterError
from .mpq.profile import DEFAULT_PROFILE, Profile, ProfileError, Resolver, verify_file
from .mpq.stormlib import StormLibError

log = logging.getLogger("converter")


def _verify(args: argparse.Namespace) -> int:
    """StarCraft.exe by its SHA-256, the archives by the files the conversion reads (their effective copies'
    SHA-256 against the input manifest), so that archives packed differently from the reference copies pass."""
    profile = Profile.load(args.profile)
    root = Path(args.data)
    verify_file(root, profile.executable)
    log.info("ok %s", profile.executable.name)
    with Resolver(root, profile) as resolver:
        problems = resolver.verify_contents()
        count = len(resolver.manifest.files)
    for spec in profile.archives:
        size = (root / spec.name).stat().st_size
        log.info("%s: %d bytes%s", spec.name, size, "" if size == spec.size else
                 f" (the reference copy has {spec.size}; checked by its files)")
    for line in problems:
        log.error("%s", line)
    if problems:
        raise ProfileError(f"{len(problems)} of the {count} files profile {profile.id} reads differ or are missing")
    log.info("profile %s verified: %d files match", profile.id, count)
    return 0


def _extract(args: argparse.Namespace) -> int:
    with Resolver(args.data, Profile.load(args.profile)) as resolver:
        archive, data = resolver.read(args.path)
    Path(args.out).write_bytes(data)
    log.info("%s: %d bytes from %s", args.path, len(data), archive)
    return 0


def _open_source(args: argparse.Namespace):
    """The 1.17 install (`--data`) or a StarCraft: Remastered install (`--scr`) as the profile's files."""
    profile = Profile.load(args.profile)
    if args.scr:
        from .casc.source import ScrSource

        return ScrSource(args.scr, profile)
    return Resolver(args.data, profile)


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
    """Convert everything from the 1.17 install, recording the files read, and write the manifest and the missions'
    packs (defs/campaign/packs-<profile>.json); with --scr also the text tables made from both installs."""
    from . import gamedata, missions, scenarios
    from .casc import rebuild
    from .casc.source import build_texts, write_manifest, write_texts
    from .inputs import manifest_path, texts_path
    from .emit import packs, terrain
    from .source import RecordingSource

    profile = Profile.load(args.profile)
    out = Path(args.out)
    with RecordingSource(Resolver(args.data, profile, reference=True), rebuild.describe) as source:
        report = missions.convert(source, out)
        for step in (gamedata.convert, scenarios.convert, terrain.generate, packs.generate):
            step(source, out)
        missions.write_packs(missions.packs_path(profile.id), profile.id, report)
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
    from .casc.source import ScrSource
    from .emit import hd, hd_ui

    with ScrSource(args.scr, Profile.load(args.profile)) as source:
        portraits = None  # written once, linked into the other variant (both take HD2's clips)
        for variant in (hd.VARIANTS if args.variant == "all" else (args.variant,)):
            portraits = hd.generate(source, source.storage, Path(args.out), variant, portraits)["portraits"]
        hd_ui.write_console(lambda path: source.read(path)[1], source.storage, Path(args.out), source.profile.id,
                            source.data_set_hash())
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


def _locale(args: argparse.Namespace) -> int:
    """A language's overlay packs and the glyph supplement (generated/locale/; ADR 0016)."""
    from .casc.storage import CascStorage
    from .emit import locale

    with CascStorage(args.scr) as storage:
        if args.locale == locale.ALL:
            locale.generate_all(storage, Path(args.out))
        else:
            locale.generate(storage, args.locale, Path(args.out))
    return 0


def _speech(args: argparse.Namespace) -> int:
    """A language's speech: its missions' TIME, spoken lines and unit voices (generated/speech/; ADR 0016)."""
    from .casc.storage import CascStorage
    from .emit import speech

    with CascStorage(args.scr) as storage:
        speech.generate(storage, args.locale, Path(args.out))
    return 0


def _icon(args: argparse.Namespace) -> int:
    """The game's icon in each platform's format (generated/icon/), from the profile's StarCraft.exe."""
    from . import icon

    icon.convert(args.data, Profile.load(args.profile), Path(args.out))
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


def _repack(args: argparse.Namespace) -> int:
    from . import repack

    extra = []
    if args.res_log:  # kp_res's log: "name<TAB>status<TAB>source", the names read that the manifest does not hold
        rows = (line.split("\t") for line in Path(args.res_log).read_text().splitlines())
        extra = [row[0].replace("/", "\\") for row in rows if len(row) > 1 and row[1] == "unlisted"]
    oggs = None if args.wav else repack.OggStore(Path(args.cache))
    with _open_source(args) as source:
        out = Path(args.out or ("generated/repack-single" if args.single else "generated/repack"))
        report = repack.write(source, out, kind="remastered" if args.scr else "1.17", single=args.single, extra=extra,
                              oggs=oggs)
    for name in report.missing[:10]:
        log.warning("not in the source, left out: %s", name)
    if len(report.missing) > 10:
        log.warning("... %d files left out in all", len(report.missing))
    for group in sorted(report.files):
        log.info("%s: %d files, %.1f MB", group, report.files[group], report.sizes[group] / 1e6)
    log.info("%s: %d files (%s), %d WAVs as Ogg Vorbis (%.1f MB for %.1f MB)%s", out, sum(report.files.values()),
             ", ".join(f"{n} {s}" for s, n in sorted(report.statuses.items())), report.ogg, report.ogg_bytes / 1e6,
             report.wav_bytes / 1e6, f", {oggs.encoded} encoded" if oggs else "")
    return 0


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
    p = sub.add_parser("locale", help="write a language's overlay packs and the glyph supplement")
    p.add_argument("--scr", required=True, help="a StarCraft: Remastered install holding the language")
    p.add_argument("--locale", required=True,
                   help="the language, such as ruRU (defs/l10n/locales.json), or all: each the install holds")
    p.add_argument("--out", default="generated")
    p.set_defaults(func=_locale)
    p = sub.add_parser("speech", help="write a language's speech: its missions' TIME, spoken lines and unit voices")
    p.add_argument("--scr", required=True, help="a StarCraft: Remastered install holding the language")
    p.add_argument("--locale", required=True, help="the language, such as ruRU (defs/l10n/locales.json)")
    p.add_argument("--out", default="generated")
    p.set_defaults(func=_speech)
    p = sub.add_parser("icon", help="write the executable's icon for the product's bundles")
    p.add_argument("--data", required=True, help="the profile's install (StarCraft.exe)")
    p.add_argument("--out", default="generated")
    p.set_defaults(func=_icon)
    p = sub.add_parser("repack", help="write the koprulu*.mpq repack: the files the game reads, in their formats")
    _source_arguments(p)
    p.add_argument("--out", help="the folder (default generated/repack, with --single generated/repack-single)")
    p.add_argument("--single", action="store_true",
                   help="one koprulu.mpq instead of the groups, the cinematics' beside it (the desktops)")
    p.add_argument("--res-log", help="kp_res's --res-log of a run: the names it read unlisted go in too")
    p.add_argument("--wav", action="store_true", help="keep the WAVs as they are (no oggenc)")
    p.add_argument("--cache", default="generated/cache/repack-ogg", help="where each WAV's Ogg Vorbis is kept")
    p.set_defaults(func=_repack)
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
