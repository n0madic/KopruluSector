#!/usr/bin/env python3
"""Writes the tables of defs/ the game reads at run time as C (ADR 0018, phase D): native/kp_core/src/defs/kp_defs.h
and kp_defs_*.c. The game reads an install directly and checks every file it reads against the profile's input
manifest, so the profile (defs/profile/<id>.json: the executable, the archives in their order) and the manifest
(defs/scr/inputs-<id>.json: each file's size and SHA-256, the names absent from the reference install, the unreadable
ones) are compiled in. --check fails when a written file is out of date (tools/ci.sh)."""
from __future__ import annotations

import argparse
import json
import logging
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "native/kp_core/src/defs"
PROFILE = "bw-1.17.0.1"

log = logging.getLogger("defs_c")
PER_LINE = 24


def tables() -> list[tuple[str, bytes]]:
    """The executable's tables the runtime reads, serialized as the converter writes them into the containers (the same
    functions): the DAT layouts, the requirement bytecode, the interface's tables, the campaign's, the iscript header
    types' animation slots. A container's section of the same name, where there is one, is read instead (the tests'
    fixtures)."""
    sys.path.insert(0, str(ROOT))
    from converter import campaign, ui_tables
    from converter.analyze import iscript
    from converter.formats import dat, requirements
    from converter.kpf import kpd

    out = [("COLS", kpd.columns_section(dat.load_layouts(PROFILE)))]
    out += [(t.fourcc, requirements.section(list(t.words))) for t in requirements.load_tables(PROFILE)]
    ui = dict(kpd.ui_table_sections(ui_tables.load(ui_tables.PROFILE)))
    out += [(k, ui[k]) for k in ("BTNS", "STLY", "MUSI", "VOLT", "ACCL", "ESTB")]
    m = campaign.load(PROFILE)
    out += [("TABL", campaign.tables_section(m)), ("CHEA", campaign.cheats_section(m))]
    out += [("SLOT", kpd.slots_section(iscript.Table.load(PROFILE).header_animations))]
    out += built_tables()
    return sorted(out)


def names_table(names: list[str]) -> bytes:
    """A list of names: u16 count; per name u8 length, its bytes (latin-1)."""
    out = bytearray(struct.pack("<H", len(names)))
    for n in names:
        raw = n.encode("latin-1")
        out += bytes([len(raw)]) + raw
    return bytes(out)


def built_tables() -> list[tuple[str, bytes]]:
    """What the runtime builds its structures from the original files with (stage D3): the interface's lists of files
    (defs/ui: the dialogs' BINs UBIN, the themes UTHM and their files UTHF, the dialog GRPs UDGR, the GRPs UGRP, the
    PCXs UPCX, the glue's files UGLF, the credits' scripts UCRD), our own dialogs built as the original's BINs (KBIN,
    files sections as the converter's), the records we append to an original's (KEXT: each a BIN with a root container
    the runtime leaves out), the texts we give records that have none (KLAB: u16 count; per text the dialog's label, u16
    record, u16 length, its UTF-8), the fonts' plaintext files (FNTP) and each tileset's colour cycles (CYCn, the
    container's CYCL). Stage D4: the CHK loader tables (CHKL, chk.loader_section) and each campaign mission's packs
    (MPAK: u16 count; per mission by its lowercased directory u8 length, the directory, its PACK payload).
    """
    from converter import missions
    from converter.formats import chk, dialog, palette
    from converter.kpf import kpd, kpm

    ui = kpd.ui_defs()
    out = [("UBIN", names_table(ui["bins"])), ("UTHM", names_table(ui["themes"])),
           ("UTHF", names_table(ui["theme_files"])), ("UDGR", names_table(ui["dialog_grps"])),
           ("UGRP", names_table(ui["grps"])), ("UPCX", names_table(ui["pcx"])),
           ("UGLF", names_table(ui["glue_files"])), ("UCRD", names_table(list(kpd.CREDITS_SCRIPTS)))]
    k = kpd.koprulu_defs()
    root = {"type": dialog.TYPE_CONTAINER, "id": 0, "flags": 0, "rect": [0, 0, 640, 480]}
    out.append(("KBIN", kpd.files_section(lambda n: dialog.build(k["dialogs"][n]), list(k["dialogs"]))))
    out.append(("KEXT", kpd.files_section(lambda n: dialog.build([root, *k["extend"][n]]), list(k["extend"]))))
    labels = bytearray(struct.pack("<H", sum(len(v) for v in k["labels"].values())))
    for name, records in k["labels"].items():
        for index, text in records.items():
            raw = text.encode("utf-8")
            labels += kpd._label(name) + struct.pack("<HH", int(index), len(raw)) + raw
    out.append(("KLAB", bytes(labels)))
    out.append(("FNTP", names_table([f["plaintext"] for f in kpd.font_defs()["fonts"]])))
    for i, name in enumerate(palette.TILESETS):
        ranges = palette.CYCLE_RANGES[name]
        out.append((f"CYC{i}", bytes([len(ranges), palette.CYCLE_SPEED]) + b"".join(bytes(r) for r in ranges)))
    out.append(("CHKL", chk.loader_section(chk.LoaderTables.load(PROFILE))))
    packs = sorted((d.lower().encode("latin-1"), p) for d, p in missions.load_packs(PROFILE).items())
    out.append(("MPAK", struct.pack("<H", len(packs)) + b"".join(bytes([len(d)]) + d + kpm.packs_payload(p)
                                                               for d, p in packs)))
    return out


def c_bytes(data: bytes) -> list[str]:
    return ["\t" + " ".join(f"0x{b:02x}," for b in data[k:k + PER_LINE]) for k in range(0, len(data), PER_LINE)]


def normalize(name: str) -> str:
    """converter/casc/storage.py normalize: `/` separators, lower case."""
    return name.replace("\\", "/").lower()


def c_string(s: str) -> str:
    if not s.isascii() or any(c in s for c in '"\\\n') or any(ord(c) < 0x20 for c in s):
        raise ValueError(f"not a plain ASCII string: {s!r}")
    return f'"{s}"'


def c_ident(profile_id: str) -> str:
    return "kp_defs_" + "".join(c if c.isalnum() else "_" for c in profile_id)


def inputs(profile_id: str) -> tuple[list[tuple[str, int, str]], list[str], list[str]]:
    """The manifest's files (normalized name, size, SHA-256) and its absent and unreadable names, each sorted by
    their bytes (strcmp). Spellings that normalize alike must name the same bytes."""
    raw = json.loads((ROOT / "defs/scr" / f"inputs-{profile_id}.json").read_text())
    if raw["profile"] != profile_id:
        raise ValueError(f"inputs-{profile_id}.json is profile {raw['profile']}'s")
    files: dict[str, tuple[int, str]] = {}
    for row in raw["files"]:
        key, size, sha = normalize(row[0]), row[1], row[2]
        if files.setdefault(key, (size, sha)) != (size, sha):
            raise ValueError(f"{row[0]}: spellings of {key} with other bytes")
    absent = sorted({normalize(n) for n in raw["absent"]} - set(files))
    unreadable = sorted({normalize(n) for n in raw["unreadable"]})
    return [(k, *files[k]) for k in sorted(files)], absent, unreadable


def render_header() -> str:
    return "\n".join([
        "/* Generated by tools/defs_c.py from defs/: do not edit. The tables of defs/ the game reads at run time (ADR",
        " * 0018): the data profiles, their archives and their input manifests. */",
        "#ifndef KP_DEFS_H",
        "#define KP_DEFS_H",
        "",
        "#include <stdint.h>",
        "",
        "/* A file of the input manifest: its name normalized (lower case, '/' separators), its size and SHA-256 (hex). */",
        "typedef struct {",
        "\tconst char *name;",
        "\tuint32_t size;",
        "\tconst char *sha256;",
        "} KpDefsInput;",
        "",
        "/* An archive of the profile's install, in its order (the first copy of a name wins). */",
        "typedef struct {",
        "\tconst char *name;",
        "\tuint32_t size;",
        "\tconst char *role; /* \"core\" or \"campaign\" (the CDs' archives: music, movies, the missions' speech) */",
        "} KpDefsArchive;",
        "",
        "/* A table of the executable's, serialized as the converter's container section of the same name (tools/defs_c.py",
        " * tables): COLS, RQ**, BTNS, STLY, MUSI, VOLT, ACCL, ESTB, TABL, CHEA, SLOT. */",
        "typedef struct {",
        "\tchar fourcc[5];",
        "\tconst uint8_t *data;",
        "\tuint32_t size;",
        "} KpDefsTable;",
        "",
        "typedef struct {",
        "\tconst char *id;",
        "\tconst char *executable;",
        "\tuint32_t executable_size;",
        "\tconst KpDefsArchive *archives;",
        "\tint archive_count;",
        "\t/* sorted by name (strcmp) */",
        "\tconst KpDefsInput *files;",
        "\tint file_count;",
        "\tconst char *const *absent; /* looked for by the conversion, not in the reference install */",
        "\tint absent_count;",
        "\tconst char *const *unreadable; /* in the reference install's archives, not readable there */",
        "\tint unreadable_count;",
        "\tconst KpDefsTable *tables; /* sorted by fourcc */",
        "\tint table_count;",
        "} KpDefsProfile;",
        "",
        f"extern const KpDefsProfile {c_ident(PROFILE)};",
        "/* every compiled profile, an initializer */",
        f"#define KP_DEFS_PROFILES {{ &{c_ident(PROFILE)} }}",
        "",
        "/* The compiled profile of that id, or NULL. */",
        "const KpDefsProfile *kp_defs_profile(const char *id);",
        "/* A file of a profile's manifest by its normalized name, or NULL. */",
        "const KpDefsInput *kp_defs_input(const KpDefsProfile *p, const char *normalized);",
        "/* The profile's table of that fourcc, or NULL. */",
        "const KpDefsTable *kp_defs_table(const KpDefsProfile *p, const char *fourcc);",
        "/* Whether a sorted list of normalized names holds a name. */",
        "int kp_defs_listed(const char *const *names, int count, const char *normalized);",
        "",
        "#endif",
        "",
    ])


def render_profile(profile_id: str) -> str:
    profile = json.loads((ROOT / "defs/profile" / f"{profile_id}.json").read_text())
    files, absent, unreadable = inputs(profile_id)
    ident = c_ident(profile_id)
    lines = [
        f"/* Generated by tools/defs_c.py from defs/profile/{profile_id}.json and defs/scr/inputs-{profile_id}.json: do",
        " * not edit. */",
        '#include "kp_defs.h"',
        "",
        f"static const KpDefsArchive ARCHIVES[] = {{",
    ]
    for a in profile["archives"]:
        lines.append(f"\t{{ {c_string(a['name'])}, {a['size']}u, {c_string(a['role'])} }},")
    lines += ["};", "", "static const KpDefsInput FILES[] = {"]
    lines += [f"\t{{ {c_string(n)}, {size}u, {c_string(sha)} }}," for n, size, sha in files]
    lines += ["};", "", "static const char *const ABSENT[] = {"]
    lines += [f"\t{c_string(n)}," for n in absent]
    lines += ["};", "", "static const char *const UNREADABLE[] = {"]
    lines += [f"\t{c_string(n)}," for n in unreadable]
    exe = profile["executable"]
    lines += ["};", ""]
    blobs = tables()
    for fourcc, data in blobs:
        lines += [f"static const uint8_t T_{fourcc.strip()}[] = {{"] + c_bytes(data) + ["};", ""]
    lines += ["static const KpDefsTable TABLES[] = {"]
    lines += [f"\t{{ {c_string(fourcc)}, T_{fourcc.strip()}, {len(data)}u }}," for fourcc, data in blobs]
    lines += [
        "};",
        "",
        f"const KpDefsProfile {ident} = {{",
        f"\t{c_string(profile['id'])}, {c_string(exe['path'])}, {exe['size']}u,",
        "\tARCHIVES, (int)(sizeof ARCHIVES / sizeof ARCHIVES[0]),",
        "\tFILES, (int)(sizeof FILES / sizeof FILES[0]),",
        "\tABSENT, (int)(sizeof ABSENT / sizeof ABSENT[0]),",
        "\tUNREADABLE, (int)(sizeof UNREADABLE / sizeof UNREADABLE[0]),",
        "\tTABLES, (int)(sizeof TABLES / sizeof TABLES[0]),",
        "};",
        "",
    ]
    return "\n".join(lines)


def outputs() -> dict[Path, str]:
    return {
        OUT / "kp_defs.h": render_header(),
        OUT / f"{c_ident(PROFILE)}.c": render_profile(PROFILE),
    }


def main(argv: list[str] | None = None) -> int:
    logging.basicConfig(level=logging.INFO, format="%(message)s")
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="fail when a written file is out of date")
    args = parser.parse_args(argv)
    stale = []
    for path, text in outputs().items():
        if path.is_file() and path.read_text() == text:
            continue
        if args.check:
            stale.append(path)
            continue
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)
        log.info("wrote %s", path.relative_to(ROOT))
    if stale:
        log.error("out of date (python3 tools/defs_c.py): %s", ", ".join(str(p.relative_to(ROOT)) for p in stale))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
