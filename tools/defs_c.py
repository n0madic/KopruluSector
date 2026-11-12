#!/usr/bin/env python3
"""Writes the tables of defs/ the game reads at run time as C (ADR 0018, phase D): native/kp_core/src/defs/kp_defs.h
and kp_defs_*.c. The game reads an install directly and checks every file it reads against the profile's input
manifest, so the profile (defs/profile/<id>.json: the executable, the archives in their order) and the manifest
(defs/scr/inputs-<id>.json: each file's size and SHA-256, the names absent from the reference install) are compiled
in. --check fails when a written file is out of date (tools/ci.sh)."""
from __future__ import annotations

import argparse
import base64
import json
import logging
import struct
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "native/kp_core/src/defs"
PROFILE = "bw-1.17.0.1"

log = logging.getLogger("defs_c")
PER_LINE = 24
NONE16 = 0xFFFF


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
    out += scr_tables(PROFILE)
    return sorted(out)


def names_table(names: list[str]) -> bytes:
    """A list of names: u16 count; per name u8 length, its bytes (latin-1)."""
    return struct.pack("<H", len(names)) + b"".join(name_bytes(n) for n in names)


def name_bytes(name: str) -> bytes:
    """u8 length, the name's bytes (latin-1)."""
    raw = name.encode("latin-1")
    if len(raw) > 255:
        raise ValueError(f"{name}: longer than 255 bytes")
    return bytes([len(raw)]) + raw


def built_tables() -> list[tuple[str, bytes]]:
    """What the runtime builds its structures from the original files with (stage D3): the interface's lists of files
    (defs/ui: the dialogs' BINs UBIN, the themes UTHM and their files UTHF, the dialog GRPs UDGR, the GRPs UGRP, the
    PCXs UPCX, the glue's files UGLF, the credits' scripts UCRD), our own dialogs built as the original's BINs (KBIN,
    files sections as the converter's), the records we append to an original's (KEXT: each a BIN with a root container
    the runtime leaves out), the texts we give records that have none (KLAB: u16 count; per text the dialog's label, u16
    record, u16 length, its UTF-8), the fonts' plaintext files (FNTP) and each tileset's colour cycles (CYCn, the
    container's CYCL). Stage D4: the CHK loader tables (CHKL, chk.loader_section) and each campaign mission's packs
    (MPAK: u16 count; per mission by its lowercased directory u8 length, the directory, its PACK payload). Stage D7: the
    cinematics an install's movies are played as (CINM, cinematics_catalog). Stage C1 (ADR 0019): each test
    scenario's packs for kp_convert scenarios' index (SPAK: as MPAK, by the scenario's name). Stage C2: the languages
    (LANG, defs/l10n/locales.json's in its order, the base's first: u8 count; per language u8 length and its ID, u8
    length and its own name in UTF-8) and the missions whose language's triggers may differ from the base's in more than
    TIME (SPEX, its `speech`: a names table of "<locale> <campaign>/<mission>"). Stage C3: the languages' rules
    (l10n_tables).
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
    packs = sorted((n.lower().encode("latin-1"), p) for n, p in missions.load_scenario_packs(PROFILE).items())
    out.append(("SPAK", struct.pack("<H", len(packs)) + b"".join(bytes([len(n)]) + n + kpm.packs_payload(p)
                                                               for n, p in packs)))
    out.append(("CINM", cinematics_catalog()))
    l10n = json.loads((ROOT / "defs/l10n/locales.json").read_text(encoding="utf-8"))
    out.append(("LANG", bytes([len(l10n["locales"])]) + b"".join(
        bytes([len(i)]) + i + bytes([len(n)]) + n for i, n in ((r["id"].encode(), r["name"].encode()) for r in l10n["locales"]))))
    out.append(("SPEX", names_table([f"{e['locale']} {e['mission']}" for e in l10n.get("speech", ())])))
    out += l10n_tables(l10n)
    return out


def cinematics_catalog() -> bytes:
    """CINM, the movies the runtime plays (ADR 0018, stage D7; native kp_cine_catalog): u16 movies, u16 preludes; per
    movie of defs/campaign/cinematics.json's `names` in its order u8 length, its name, u32 size and SHA-256 of its
    Smacker copy in the reference 1.17 install (`smk`); per prelude u8 cinematic, u8 prelude."""
    raw = json.loads((ROOT / "defs/campaign/cinematics.json").read_text(encoding="utf-8"))
    out = bytearray(struct.pack("<HH", len(raw["names"]), len(raw["preludes"])))
    for name in raw["names"]:
        size, digest = raw["smk"][name]
        encoded = name.encode("ascii")
        out += bytes([len(encoded)]) + encoded + struct.pack("<I", size) + bytes.fromhex(digest)
    for cinematic, prelude in raw["preludes"]:
        out += struct.pack("<BB", cinematic, prelude)
    return bytes(out)


def text16(text: str) -> bytes:
    raw = text.encode("utf-8")
    if len(raw) > 0xFFFF:
        raise ValueError(f"defs/l10n: a text of {len(raw)} bytes is longer than 65535")
    return struct.pack("<H", len(raw)) + raw


def once_each(entries: list[dict], name: str, key) -> list[dict]:
    """locales.json's list `name`, refused when two entries of a language name the same key (the later would silently
    replace the earlier)."""
    seen = set()
    for e in entries:
        k = (e["locale"], *key(e))
        if k in seen:
            raise ValueError(f"defs/l10n/locales.json's {name} names {' '.join(map(str, k))} twice")
        seen.add(k)
    return entries


def counted(entries: list[bytes]) -> bytes:
    return struct.pack("<H", len(entries)) + b"".join(entries)


def l10n_tables(l10n: dict) -> list[tuple[str, bytes]]:
    """defs/l10n's rules for kp_locale.c (stage C3, ADR 0019), each u16 count of entries; a name is u8 length and its
    bytes, a text u16 length and its UTF-8, the locale the entry's first name (LTPL's none):
    - LROW (`rows`): the section ("STAT"), u16 row (1-based), the text;
    - LEXC (`exceptions`): the section, the row's id;
    - LTPL (`templates`): the name with its braces ("{CurrentYear}"), the text;
    - LKEP (`keep`): the dialog, u16 record;
    - LTXT (`texts`): the dialog, u16 record, the text;
    - LLAY (`layout`): the dialog, u16 record, u8 flags (1: rect, 2: text), 4 i16 rect, 4 i16 text rect (0 where none);
    - LSTR (`strings`): the mission ("expterran/terran03"), u16 string, the text;
    - LOWN (defs/l10n/koprulu-<locale>.json's `dialogs`, the locales' order): the dialog, the base's text, the text;
    - LCPG: u16 code points of windows-1250, -1251 and -1252's bytes 0x80..0xff (one a code page leaves undefined
      none), sorted, each once: the glyph set's start."""
    def loc(e: dict) -> bytes:
        return name_bytes(e["locale"])

    rows = once_each(l10n.get("rows", []), "rows", lambda e: (e["table"], int(e["row"])))
    exceptions = once_each(l10n.get("exceptions", []), "exceptions", lambda e: (e["table"], e["row"]))
    keep = once_each(l10n.get("keep", []), "keep", lambda e: (e["dialog"], int(e["record"])))
    texts = once_each(l10n.get("texts", []), "texts", lambda e: (e["dialog"], int(e["record"])))
    layout = once_each(l10n.get("layout", []), "layout", lambda e: (e["dialog"], int(e["record"])))
    strings = once_each(l10n.get("strings", []), "strings", lambda e: (e["mission"], int(e["string"])))
    for e in rows:
        if len(e["table"].encode("ascii")) != 4:
            raise ValueError(f"defs/l10n/locales.json's rows: {e['table']!r} is no section")
    lrow = [loc(e) + e["table"].encode("ascii") + struct.pack("<H", int(e["row"])) + text16(e["text"]) for e in rows]
    lexc = [loc(e) + name_bytes(e["table"]) + name_bytes(e["row"]) for e in exceptions]
    ltpl = [name_bytes("{%s}" % t["name"]) + text16(t["text"]) for t in l10n.get("templates", [])]
    lkep = [loc(e) + name_bytes(e["dialog"]) + struct.pack("<H", int(e["record"])) for e in keep]
    ltxt = [loc(e) + name_bytes(e["dialog"]) + struct.pack("<H", int(e["record"])) + text16(e["text"]) for e in texts]
    llay = []
    for e in layout:
        rect, text = e.get("rect"), e.get("text")
        flags = (1 if rect else 0) | (2 if text else 0)
        llay.append(loc(e) + name_bytes(e["dialog"]) + struct.pack("<HB4h4h", int(e["record"]), flags,
                                                                  *(rect or (0, 0, 0, 0)), *(text or (0, 0, 0, 0))))
    lstr = [loc(e) + name_bytes(e["mission"]) + struct.pack("<H", int(e["string"])) + text16(e["text"]) for e in strings]
    lown = []
    for row in l10n["locales"]:
        path = ROOT / "defs/l10n" / f"koprulu-{row['id']}.json"
        if not path.exists():
            continue
        for dialog, pairs in json.loads(path.read_text(encoding="utf-8"))["dialogs"].items():
            for base, text in pairs.items():
                lown.append(name_bytes(row["id"]) + name_bytes(dialog) + text16(base) + text16(text))
    codes = set()
    for codepage in ("cp1250", "cp1251", "cp1252"):
        for byte in range(0x80, 0x100):
            try:
                codes.add(ord(bytes([byte]).decode(codepage)))
            except UnicodeDecodeError:
                pass
    lcpg = struct.pack(f"<H{len(codes)}H", len(codes), *sorted(codes))
    return [("LROW", counted(lrow)), ("LEXC", counted(lexc)), ("LTPL", counted(ltpl)), ("LKEP", counted(lkep)),
            ("LTXT", counted(ltxt)), ("LLAY", counted(llay)), ("LSTR", counted(lstr)), ("LOWN", counted(lown)),
            ("LCPG", lcpg)]


def scr_tables(profile_id: str) -> list[tuple[str, bytes]]:
    """How a StarCraft: Remastered install's own formats are brought back to the profile's files (stage D6; converter/casc
    source.py _remake and rebuild.py; native/kp_app/kp_res_scr.c reads them), from the manifest's extras and
    defs/scr/texts-<profile>.json. Names are normalized,
    each list in the order the converter walks it: by the manifest name in lower case (its '\\' separators).
    - SPAL: u16 palettes; per palette (by its hash) u16 donors and their names (u8 length, bytes): the PCX files of it;
    - SPCX: u16 count; per PCX its name, u16 width, u16 height, 6 header bytes (12-15, 68-69), u16 palette (0xFFFF: none),
      its pixels' and colours' SHA-256 (rebuild.plane_hash);
    - SGRP: u16 count; per GRP with extras its name, u16 palette, u16 frames (0: not a GRP of plain rows); with frames u16
      box width, height and per frame u8 x, y, width, height, u32 offset;
    - SSMK: u16 count; per SMK its name, u16 frames, u8 ring, i32 frame rate (the header's), u16 palette;
    - STBL: u16 count; per string table its name, the XML's and the base TBL's names (length 0: none), u16 the base TBL's
      rows (0: none; a language's XML holds them by position), u16 ids (each u8 length, 0xFF: none) and u16 entries of
      NULs inside a string, each u16 string, u16 count, u16 positions;
    - SBIN: u16 count; per dialog its name, u32 skeleton size, u32 its zlib size and bytes, u16 texts; per text u32
      offset, u16 length, u8 hotkey, u8 pointers and their u32 fields, u16 source length and bytes ("id:<xml>#<id>",
      "name:<text>" in latin-1, empty: none)."""
    raw = json.loads((ROOT / "defs/scr" / f"inputs-{profile_id}.json").read_text())
    texts = json.loads((ROOT / "defs/scr" / f"texts-{profile_id}.json").read_text())
    rows = sorted(raw["files"], key=lambda r: r[0].lower())
    groups = sorted(raw["palettes"])
    group_of = {h: i for i, h in enumerate(groups)}

    def group(extras: dict) -> int:
        return group_of.get(extras.get("palette", ""), NONE16)

    spal = bytearray(struct.pack("<H", len(groups)))
    for h in groups:
        donors = sorted(raw["palettes"][h], key=str.lower)
        spal += struct.pack("<H", len(donors)) + b"".join(name_bytes(normalize(d)) for d in donors)
    spcx, sgrp, ssmk = bytearray(), bytearray(), bytearray()
    counts = {"pcx": 0, "grp": 0, "smk": 0}
    for row in rows:
        if len(row) < 4:
            continue
        name, extras = row[0], row[3]
        ext = name.lower().rsplit(".", 1)[-1]
        if ext == "pcx" and "pixels" in extras:
            w, h = extras["size"]
            spcx += name_bytes(normalize(name)) + struct.pack("<HH", w, h) + bytes.fromhex(extras["header"])
            spcx += struct.pack("<H", group(extras)) + bytes.fromhex(extras["pixels"]) + bytes.fromhex(extras["rgb"])
        elif ext == "grp":
            frames = extras.get("frames", [])
            sgrp += name_bytes(normalize(name)) + struct.pack("<HH", group(extras), len(frames))
            if frames:
                sgrp += struct.pack("<HH", *extras["box"])
                sgrp += b"".join(struct.pack("<4BI", *f) for f in frames)
        elif ext == "smk" and "smk" in extras:
            _, _, frames, ring, rate = extras["smk"]  # its size is its WebM's (converter/formats/kpw.py)
            ssmk += name_bytes(normalize(name)) + struct.pack("<HBiH", frames, ring, rate, group(extras))
        else:
            continue
        counts[ext] += 1
    stbl = bytearray(struct.pack("<H", len(texts["tbl"])))
    for name in sorted(texts["tbl"], key=str.lower):
        t = texts["tbl"][name]
        stbl += name_bytes(normalize(name)) + name_bytes(t["xml"]) + name_bytes(t["base"] or "")
        stbl += struct.pack("<HH", t.get("base_rows", 0), len(t["ids"]))
        stbl += b"".join(b"\xff" if i is None else name_bytes(i) for i in t["ids"])
        nuls = sorted((int(k), v) for k, v in t["nuls"].items())
        stbl += struct.pack("<H", len(nuls))
        for k, positions in nuls:
            stbl += struct.pack(f"<HH{len(positions)}H", k, len(positions), *positions)
    sbin = bytearray(struct.pack("<H", len(texts["bin"])))
    for name in sorted(texts["bin"], key=str.lower):
        t = texts["bin"][name]
        packed = base64.b64decode(t["skeleton"])
        sbin += name_bytes(normalize(name)) + struct.pack("<II", len(zlib.decompress(packed)), len(packed)) + packed
        sbin += struct.pack("<H", len(t["texts"]))
        for off, length, pointers, hotkey, source in t["texts"]:
            src = source.encode("latin-1")
            sbin += struct.pack(f"<IHBB{len(pointers)}IH", off, length, int(hotkey), len(pointers), *pointers, len(src))
            sbin += src
    return [("SPAL", bytes(spal)), ("SPCX", struct.pack("<H", counts["pcx"]) + bytes(spcx)),
            ("SGRP", struct.pack("<H", counts["grp"]) + bytes(sgrp)),
            ("SSMK", struct.pack("<H", counts["smk"]) + bytes(ssmk)), ("STBL", bytes(stbl)), ("SBIN", bytes(sbin))]


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


def inputs(profile_id: str) -> tuple[list[tuple[str, int, str]], list[str]]:
    """The manifest's files (normalized name, size, SHA-256) and its absent names, each sorted by their bytes
    (strcmp). Spellings that normalize alike must name the same bytes."""
    raw = json.loads((ROOT / "defs/scr" / f"inputs-{profile_id}.json").read_text())
    if raw["profile"] != profile_id:
        raise ValueError(f"inputs-{profile_id}.json is profile {raw['profile']}'s")
    files: dict[str, tuple[int, str]] = {}
    for row in raw["files"]:
        key, size, sha = normalize(row[0]), row[1], row[2]
        if files.setdefault(key, (size, sha)) != (size, sha):
            raise ValueError(f"{row[0]}: spellings of {key} with other bytes")
    absent = sorted({normalize(n) for n in raw["absent"]} - set(files))
    return [(k, *files[k]) for k in sorted(files)], absent


def render_header() -> str:
    return "\n".join([
        "/* Generated by tools/defs_c.py from defs/: do not edit. The tables of defs/ the game reads at run time (ADR",
        " * 0018): the data profiles, their archives and their input manifests. */",
        "#ifndef KP_DEFS_H",
        "#define KP_DEFS_H",
        "",
        "#include <stddef.h>",
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
        " * tables: COLS, RQ**, BTNS, STLY, MUSI, VOLT, ACCL, ESTB, TABL, CHEA, SLOT), or one the runtime builds its structures",
        " * from the original files with (built_tables, scr_tables: the interface's lists, CHKL, MPAK, SPAK, CINM, the SCR rebuilds). */",
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
        "\tconst char *executable_sha256; /* lower-case hexadecimal (kp_convert verify, icon) */",
        "\tconst KpDefsArchive *archives;",
        "\tint archive_count;",
        "\t/* sorted by name (strcmp) */",
        "\tconst KpDefsInput *files;",
        "\tint file_count;",
        "\tconst char *const *absent; /* looked for by the conversion, not in the reference install */",
        "\tint absent_count;",
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
        "/* A compiled names table (tools/defs_c.py names_table: u16 count; per name u8 length, its bytes): its count, or -1 when t",
        " * is NULL or broken. */",
        "int kp_defs_names_count(const KpDefsTable *t);",
        "/* Its index-th name (index below its count), *size bytes, not NUL-terminated. */",
        "const uint8_t *kp_defs_name_at(const KpDefsTable *t, int index, size_t *size);",
        "/* Whether the profile's names table of that fourcc holds name (its bytes exactly). */",
        "int kp_defs_names_has(const KpDefsProfile *p, const char *fourcc, const char *name);",
        "",
        "#endif",
        "",
    ])


def render_profile(profile_id: str) -> str:
    profile = json.loads((ROOT / "defs/profile" / f"{profile_id}.json").read_text())
    files, absent = inputs(profile_id)
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
        f"\t{c_string(profile['id'])}, {c_string(exe['path'])}, {exe['size']}u, {c_string(exe['sha256'])},",
        "\tARCHIVES, (int)(sizeof ARCHIVES / sizeof ARCHIVES[0]),",
        "\tFILES, (int)(sizeof FILES / sizeof FILES[0]),",
        "\tABSENT, (int)(sizeof ABSENT / sizeof ABSENT[0]),",
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
