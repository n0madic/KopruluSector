"""A language's overlay packs from a StarCraft: Remastered install (`converter locale`, ADR 0016).

Run after `data` and `missions`: the base's packs under `--out` are what a language is measured against. Writes, under
`generated/locale/`:
- `<locale>/strings.kpd` (`STRS`): the base's TBLs in the language, sections of schema 2: u32 count, u32 offsets from the
  section's start, the texts (`stat_txt` past u16's reach in UTF-8);
- `<locale>/ui.kpd` (`UI  `, partial): DLGS and POOL with the language's texts, GFIL with its `rez\\*.txt`;
- `<locale>/cinematics.kpd` (`CINE`, only SUBS): one row per movie of the original's table;
- `<locale>/missions/<campaign>/<mission>.kpm` (`MISN`, only STRS): the language's map strings, the same count;
- `glyphs.kpd` (`FONT`): VFNT (Remastered's Eurostile pair) and U8, U10, U14, U16, U16X, each FNT font's glyph supplement
  (`tools/kp_fnt_make`), one set for every language; `kp_ui_text.h` has the form.

A row the language lacks (its XML has no such id, a `<<PLACEHOLDER>>`, an empty text) keeps the base's; so does a map
string the simulation or the sound looks up by its text (WAV paths, Set Next Scenario's name). Every check runs before
anything is written and all its failures are listed in `reports/locale-<locale>.json`; what stops a conversion before a
report (a base pack not there, a file the install lacks) is the install's or the steps' before it, not the language's data.
A pack of the language this conversion does not write is removed.
"""
from __future__ import annotations

import json
import logging
import os
import re
import struct
import subprocess
import tempfile
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable

from .. import CONVERTER_VERSION
from ..casc import rebuild
from ..casc.storage import CascError
from ..errors import ConverterError
from ..formats import chk, dialog
from ..kpf import kpd, kpm
from ..kpf.container import Container, parse
from ..provenance import DEFS_ROOT
from . import cinematics
from .files import write_if_changed

log = logging.getLogger(__name__)

LOCALES = DEFS_ROOT / "l10n" / "locales.json"
TEXTS = DEFS_ROOT / "scr" / "texts-bw-1.17.0.1.json"
OUT_DIR = "locale"
ASSETS = "locales/{locale}/Assets/"
REZ = ASSETS + "rez/"
MISSION = ASSETS + "campaign/{name}/staredit/scenario.chk"
PLACEHOLDER = b"<<PLACEHOLDER>>"
# Printf's conversions: the row goes through a format string in the engine, or is the same text with a bare `%`.
SPECIFIER = re.compile(rb"%[-+ #0]*\d*(?:\.\d+)?[a-zA-Z%]?")
TEMPLATE = re.compile(rb"\{[A-Za-z_]+\}")  # Remastered's own substitutions, which the engine does not make
CONTROL_END = 0x20  # text bytes under it are colours and layout, never a character to draw
NUL = 0
NUL_IN_XML = rebuild.TBL_NUL_IN_XML[0]  # a span's NUL in Remastered's XML, also a colour code
SPACE = 0x20  # the FNT fonts have no glyph for it, kp_ui_text gives it its width
ASCII_END = 0x80
PRINTABLE_ASCII = range(0x21, 0x7F)
NEXT_SCENARIO = 41  # KP_ACT_SET_NEXT_SCENARIO (converter/analyze/triggers.py ACTION_IDS)
ACTION_STRING = 0x04  # KP_ACTION_STRING
STR_LIMIT = 0xFFFF
NO_STRING = 0xFFFF  # a row no text is at: past the end of a section under STR_LIMIT
GLYPH_TTF = ("font/EUROSTILE-REG.TTF", "font/EUROSTILEEXT-REG.TTF")  # the text's, then font16x's wide one
VFNT_NAMES = ("fonts\\Eurostile.ttf", "fonts\\EurostileExtended.ttf")
WIDE_FONT_SECTION = "F16X"
GLYPH_CODEPAGES = ("cp1250", "cp1251")  # the set's start: every single-byte language of ADR 0016 fits them
FNT_HEADER = 8
SUPPLEMENT_ENTRY = struct.Struct("<II")
FNT_MAKE_ENV = "KOPRULU_FNT_MAKE"
FNT_MAKE_DEFAULT = Path(__file__).resolve().parents[2] / "build" / "cmake" / "kp_fnt_make"
MAX_LISTED = 12  # problems the error's message names; the report has them all

Rasterizer = Callable[[bytes, bytes, list[int]], tuple[bytes, list[int]]]


class LocaleError(ConverterError, ValueError):
    """A language's data cannot be converted, or does not fit the base's."""


class Install:
    """A CASC install's files by case-insensitive name (Remastered's locale directories mix the cases)."""

    def __init__(self, storage):
        self.storage = storage
        self.names = {name.lower(): name for name in storage.names()}

    def has(self, path: str) -> bool:
        return path.lower() in self.names

    def read(self, path: str) -> bytes:
        name = self.names.get(path.lower())
        if name is None:
            raise LocaleError(f"the install names no {path}")
        return self.storage.read(name)


def load_locales() -> dict:
    return json.loads(LOCALES.read_text(encoding="utf-8"))


def check_locale(locale: str, defs: dict) -> None:
    """LocaleError unless the locale is one defs/l10n/locales.json lists, other than the base."""
    known = [row["id"] for row in defs["locales"]]
    if locale not in known:
        raise LocaleError(f"{locale} is none of defs/l10n/locales.json's: {', '.join(known)}")
    if locale == defs["base"]:
        raise LocaleError(f"{locale} is the base: it has no overlay")


@dataclass
class Findings:
    """What the checks found; `used` keeps where each code point of the language's text was first seen."""
    exceptions: set[tuple[str, str]] = field(default_factory=set)
    problems: list[str] = field(default_factory=list)
    used: dict[int, str] = field(default_factory=dict)
    exceptions_taken: list[str] = field(default_factory=list)

    def note_text(self, where: str, text: bytes) -> str | None:
        """The text decoded with its code points noted, or None and a problem when it is not UTF-8."""
        try:
            decoded = text.decode("utf-8")
        except UnicodeDecodeError as e:
            self.problems.append(f"{where}: not UTF-8 ({e.reason} at byte {e.start})")
            return None
        for ch in decoded:
            if ord(ch) >= CONTROL_END:
                self.used.setdefault(ord(ch), where)
        return decoded

    def check_row(self, table: str, row: str, base: bytes, text: bytes, controls: set[int]) -> None:
        """A row of a table or a dialog against the base's: UTF-8, the same printf conversions, the same NULs, no control
        byte the base's table does not use and none of Remastered's substitutions.

        Colours may move or go: the translators dropped the highlight of a button's letter (254 rows of ruRU)."""
        where = f"{table} {row}"
        self.note_text(where, text)
        if SPECIFIER.findall(text) != SPECIFIER.findall(base):
            if (table, row) in self.exceptions:
                self.exceptions_taken.append(where)
            else:
                self.problems.append(f"{where}: conversions {SPECIFIER.findall(base)} became {SPECIFIER.findall(text)}")
        if text.count(NUL) != base.count(NUL):
            self.problems.append(f"{where}: {base.count(NUL)} NULs became {text.count(NUL)}")
        self.check_marks(where, text, controls)

    def check_marks(self, where: str, text: bytes, controls: set[int]) -> None:
        """No control byte the base does not use and none of Remastered's substitutions."""
        strange = {c for c in text if c < CONTROL_END} - controls
        if strange:
            self.problems.append(f"{where}: control bytes {sorted(strange)} the base does not use")
        if TEMPLATE.search(text):
            self.problems.append(f"{where}: a substitution the engine does not make: {TEMPLATE.search(text).group(0)!r}")


def xml_rows(install: Install, path: str, found: Findings) -> list[tuple[str, bytes]]:
    """(id, text in UTF-8 with its escapes undone) of a language's string XML; a span's NUL may still be 0x07 (with_nuls).
    A row whose escapes cannot be undone is a problem, and stays in its place."""
    rows = []
    for key, value in rebuild.xml_values(install.read(path)):
        try:
            rows.append((key, rebuild.unescape_utf8(value)))
        except rebuild.EscapeError as e:
            found.problems.append(f"{path} {key}: {e}")
            rows.append((key, value))
    return rows


def locale_xml(locale: str, xml: str) -> str:
    """The language's copy of a manifest's `SD/rez/<name>.xml`."""
    return REZ.format(locale=locale) + xml.rsplit("/", 1)[-1]


def row_key(key: str) -> str:
    """The XML's id of a manifest's row id: it numbers a run's rows with a dash."""
    return re.sub(r"_(\d+)$", r"-\1", key)


def table_rows(install: Install, locale: str, table: dict, found: Findings) -> list[tuple[str, bytes | None]]:
    """(id, the language's text or None) of each row of a base table, in order. A table Remastered keeps as a TBL too
    (stat_txt) is positional, its XML holds the TBL's rows and then new ones; the others go by the manifest's ids."""
    rows = xml_rows(install, locale_xml(locale, table["xml"]), found)
    if table["base"]:
        count = len(rebuild.tbl_spans(install.read(table["base"])))
        return [(rows[k][0], rows[k][1]) if k < min(count, len(rows)) else ("", None) for k in range(len(table["ids"]))]
    by_id = dict(rows)
    out: list[tuple[str, bytes | None]] = []
    for key in table["ids"]:
        out.append((key, by_id.get(key, by_id.get(row_key(key)))) if key else ("", None))
    return out


def wide_table(spans: list[bytes]) -> bytes:
    """A TBL of schema 2: u32 count, u32 offsets from its start, each text with its final NUL."""
    at = 4 + 4 * len(spans)
    offsets = []
    for span in spans:
        offsets.append(at)
        at += len(span) + 1
    return struct.pack(f"<I{len(spans)}I", len(spans), *offsets) + b"".join(s + b"\0" for s in spans)


def nul_marks(text: bytes) -> list[int]:
    """The bytes that may be a span's NUL in a language's XML, in order."""
    return [c for c in text if c in (NUL, NUL_IN_XML)]


def with_nuls(base: bytes, text: bytes) -> bytes:
    """The language's text with the NULs its XML writes as 0x07 (a colour code as well): the marks of both, in order,
    are the same, and each of the base's says which it is (convert_tables refuses a row with a NUL where they differ)."""
    marks = nul_marks(base)
    out = bytearray(text)
    seen = 0
    for k, c in enumerate(out):
        if c in (NUL, NUL_IN_XML):
            if seen < len(marks) and marks[seen] == NUL:
                out[k] = NUL
            seen += 1
    return bytes(out)


def button_form(base: bytes, text: bytes) -> bool:
    """A TBL's button text (a hotkey, then a mode byte) keeps its form: an ASCII hotkey, the base's mode."""
    if len(base) < 2 or base[0] not in PRINTABLE_ASCII or base[1] >= CONTROL_END:
        return True
    return len(text) >= 2 and text[0] in PRINTABLE_ASCII and text[1] == base[1]


def merged_row(base: bytes, text: bytes | None) -> bytes | None:
    """The language's text to use for a base row, None to keep the base's: a row empty in the base stays so."""
    return text if text and text != PLACEHOLDER and base else None


def convert_tables(install: Install, locale: str, base: Container, texts: dict, found: Findings) -> tuple[dict, dict]:
    """(section -> wide TBL, section -> counts) of the base's tables."""
    sections, report = {}, {}
    for section, path in kpd.TBL_FILES.items():
        spans = rebuild.tbl_spans(base.section(section).payload)
        controls = {c for span in spans for c in span if c < CONTROL_END}
        rows = table_rows(install, locale, texts["tbl"][path], found)
        if len(rows) != len(spans):
            found.problems.append(f"{section}: the manifest has {len(rows)} rows for the base's {len(spans)}")
        merged, translated = [], 0
        for k, (span, (key, text)) in enumerate(zip(spans, rows)):
            use = merged_row(span, text)
            if use is None:
                merged.append(span)
                continue
            if NUL in span and len(nul_marks(use)) != len(nul_marks(span)):
                found.problems.append(f"{section} {key or k}: {len(nul_marks(span))} marks that may be NULs became {len(nul_marks(use))}")
            use = with_nuls(span, use)
            found.check_row(section, key or str(k), span, use, controls)
            if not button_form(span, use):
                found.problems.append(f"{section} {key}: the button form {span[:2]!r} became {use[:2]!r}")
            merged.append(use)
            translated += 1
        sections[section] = wide_table(merged)
        report[section] = {"rows": len(spans), "translated": translated, "bytes": len(sections[section])}
    return sections, report


@dataclass(frozen=True)
class BaseDialog:
    name: str
    widgets: list[tuple]  # UI_WIDGET's fields
    smks: list[tuple]  # UI_SMK's fields


def read_dialogs(dlgs: bytes) -> list[BaseDialog]:
    """DLGS's records (kpd.dialogs_section)."""
    out = []
    count = struct.unpack_from("<H", dlgs)[0]
    at = 2
    for _ in range(count):
        size = dlgs[at]
        name = dlgs[at + 1:at + 1 + size].decode("ascii")
        at += 1 + size
        widgets, smks = struct.unpack_from("<HH", dlgs, at)
        at += 4
        records = [kpd.UI_WIDGET.unpack_from(dlgs, at + k * kpd.UI_WIDGET.size) for k in range(widgets)]
        at += widgets * kpd.UI_WIDGET.size
        nodes = [kpd.UI_SMK.unpack_from(dlgs, at + k * kpd.UI_SMK.size) for k in range(smks)]
        at += smks * kpd.UI_SMK.size
        out.append(BaseDialog(name, records, nodes))
    return out


WIDGET_FLAGS, WIDGET_TEXT_AT, WIDGET_TEXT_LEN = 3, 10, 11
SMK_NAME_AT, SMK_NAME_LEN = 1, 2


def bare_record(record: tuple, text_at: int, text_len: int) -> tuple:
    """A record with the fields that say where its text is cleared: what must be equal in a language."""
    return tuple(0 if k in (text_at, text_len) else v for k, v in enumerate(record))


def pool_text(pool: bytes, at: int, size: int) -> bytes | None:
    return None if at == kpd.NONE32 else pool[at:at + size]


def dialog_source(install: Install, locale: str, found: Findings):
    """A manifest's text source to the language's text: `id:SD/rez/<file>.xml#<id>`, b"" when it has none."""
    cache: dict[str, dict[str, bytes]] = {}

    def text_of(source: str) -> bytes:
        if not source.startswith("id:"):
            return b""
        path, key = source[3:].split("#", 1)
        if path not in cache:
            cache[path] = dict(xml_rows(install, locale_xml(locale, path), found))
        rows = cache[path]
        text = rows.get(key, rows.get(row_key(key), b""))
        return b"" if text == PLACEHOLDER else text

    return text_of


def convert_dialogs(install: Install, locale: str, base: Container, texts: dict, found: Findings) -> tuple[bytes, bytes, dict]:
    """(DLGS, POOL, counts): the base's records with the language's texts. Every number must be the base's; our own
    dialogs (L4 translates them) and a text the language lacks stay the base's."""
    dlgs, pool = base.section("DLGS").payload, base.section("POOL").payload
    ours = read_dialogs(dlgs)
    controls = {c for c in pool if c < CONTROL_END}
    text_of = dialog_source(install, locale, found)
    bins = {d.name: rebuild.dialog_from_table(texts["bin"][d.name], text_of) if d.name in texts["bin"] else None
            for d in ours}
    new_pool = bytearray()

    def place(text: bytes | None) -> tuple[int, int]:
        if text is None:
            return kpd.NONE32, 0
        new_pool.extend(text + b"\0")
        return len(new_pool) - len(text) - 1, len(text)

    out = bytearray(struct.pack("<H", len(ours)))
    translated = kept = 0
    for d in ours:
        widgets = dialog.parse(bins[d.name]) if bins[d.name] is not None else None
        if widgets is not None:
            their = read_dialogs(kpd.dialogs_section({d.name: bins[d.name]})[0])[0]
            if len(their.widgets) != len(d.widgets) or len(their.smks) != len(d.smks):
                found.problems.append(f"{d.name}: {len(d.widgets)} records and {len(d.smks)} movies became "
                                      f"{len(their.widgets)} and {len(their.smks)}")
                widgets = None
            else:
                for k, (a, b) in enumerate(zip(d.widgets, their.widgets)):
                    if bare_record(a, WIDGET_TEXT_AT, WIDGET_TEXT_LEN) != bare_record(b, WIDGET_TEXT_AT, WIDGET_TEXT_LEN):
                        found.problems.append(f"{d.name} record {k}: not the base's")
                for k, (a, b) in enumerate(zip(d.smks, their.smks)):
                    if bare_record(a, SMK_NAME_AT, SMK_NAME_LEN) != bare_record(b, SMK_NAME_AT, SMK_NAME_LEN):
                        found.problems.append(f"{d.name} movie {k}: not the base's")
        records, nodes = bytearray(), bytearray()
        for k, record in enumerate(d.widgets):
            text = pool_text(pool, record[WIDGET_TEXT_AT], record[WIDGET_TEXT_LEN])
            theirs = widgets[k] if widgets is not None else None
            if theirs is not None:
                name_like = theirs.type == dialog.TYPE_IMAGE or k == 0
                if not name_like and text and theirs.text:  # a name stays the base's: it is a file or the dialog's
                    if record[WIDGET_FLAGS] & dialog.FLAG_HOTKEY and theirs.text[0] >= ASCII_END:
                        found.problems.append(f"{d.name} record {k}: the hotkey {theirs.text[:2]!r} is not ASCII")
                    found.check_row(d.name, str(k), text, theirs.text, controls)
                    text = theirs.text
                    translated += 1
                elif text and not name_like:
                    kept += 1
            at, size = place(text)
            fields = list(record)
            fields[WIDGET_TEXT_AT], fields[WIDGET_TEXT_LEN] = at, size
            records += kpd.UI_WIDGET.pack(*fields)
        for node in d.smks:
            at, size = place(pool_text(pool, node[SMK_NAME_AT], node[SMK_NAME_LEN]))
            fields = list(node)
            fields[SMK_NAME_AT], fields[SMK_NAME_LEN] = at, size
            nodes += kpd.UI_SMK.pack(*fields)
        label = d.name.encode("ascii")
        out += bytes([len(label)]) + label + struct.pack("<HH", len(d.widgets), len(d.smks)) + records + nodes
    return bytes(out), bytes(new_pool), {"translated": translated, "kept": kept, "bytes": len(new_pool)}


def convert_scripts(install: Install, locale: str, base: Container, found: Findings) -> tuple[dict[str, bytes], list[str]]:
    """(path -> the language's `rez\\*.txt`, the paths left to the base): each must name no file the base lacks."""
    files = kpd.read_files(base.section("GFIL").payload)
    known = {name.lower() for name, _ in files}
    scripts, left = {}, []
    for name, _ in files:
        if not name.lower().startswith("rez\\") or not name.lower().endswith(".txt"):
            continue
        path = REZ.format(locale=locale) + name[4:]
        if not install.has(path):
            left.append(name)
            continue
        data = install.read(path)
        if found.note_text(name, data) is None:
            continue
        for tag in kpd.CREDITS_TAG.finditer(data):
            if tag.group(1).decode("latin-1").lower() not in known:
                found.problems.append(f"{name}: names {tag.group(1)!r}, which the base's GFIL lacks")
        scripts[name] = data
    return scripts, left


def str_rows(strs: bytes) -> list[bytes | None]:
    """A CHK's STR rows from row 1: None where no text is (the engine's kp_map_string gives none too)."""
    count = struct.unpack_from("<H", strs)[0]
    rows: list[bytes | None] = []
    for k in range(1, count + 1):
        if 2 * k + 2 > len(strs):
            rows.append(None)
            continue
        at = struct.unpack_from("<H", strs, 2 * k)[0]
        end = strs.find(b"\0", at) if at < len(strs) else -1
        rows.append(strs[at:end] if end >= 0 else None)
    return rows


def str_encode(rows: list[bytes | None], name: str) -> bytes:
    """STR of the rows; LocaleError past its 16-bit offsets."""
    at = 2 + 2 * len(rows)
    offsets, body = [], bytearray()
    for row in rows:
        if row is None:
            offsets.append(NO_STRING)
            continue
        offsets.append(at + len(body))
        body += row + b"\0"
    if at + len(body) >= STR_LIMIT:
        raise LocaleError(f"{name}: the map's strings outgrew STR's 16-bit offsets")
    return struct.pack(f"<H{len(rows)}H", len(rows), *offsets) + bytes(body)


def kept_string_ids(sections: dict[str, bytes]) -> set[int]:
    """The STR rows looked up by their text: the sounds' paths and Set Next Scenario's map name."""
    kept = set(kpm.wav_string_ids(sections))
    data = sections.get("TRIG", b"")
    for base in range(0, len(data), kpm.TRIGGER_SIZE):
        for a in range(kpm.ACTION_COUNT):
            act = base + kpm.ACTIONS_OFFSET + a * kpm.ACTION_SIZE
            if data[act + kpm.ACTION_ID] == NEXT_SCENARIO:
                kept.add(struct.unpack_from("<I", data, act + ACTION_STRING)[0] & 0xFFFF)
    kept.discard(0)
    return kept


def merge_mission(name: str, base: Container, theirs: bytes, found: Findings) -> tuple[bytes, dict]:
    """A mission's STR: the base's count, each row the language's unless the base's is looked up by its text. What does
    not fit is a problem, with the base's STR returned."""
    strs = base.section("STRS").payload
    ours, rows = str_rows(strs), str_rows(theirs)
    if len(ours) != len(rows):
        found.problems.append(f"{name}: {len(ours)} map strings became {len(rows)}")
        return strs, {}
    keep = kept_string_ids({tag: base.section(tag).payload for tag in ("TRIG", "MBRF")})
    controls = {c for row in ours if row for c in row if c < CONTROL_END}
    merged: list[bytes | None] = []
    translated = kept = 0
    for k, (mine, text) in enumerate(zip(ours, rows), start=1):
        if k in keep or not mine or not text or text == PLACEHOLDER:
            merged.append(mine)
            kept += bool(mine)
            continue
        found.note_text(f"{name} string {k}", text)
        found.check_marks(f"{name} string {k}", text, controls)
        merged.append(text)
        translated += 1
    try:
        encoded = str_encode(merged, name)
    except LocaleError as e:
        found.problems.append(str(e))
        return strs, {}
    return encoded, {"translated": translated, "kept": kept, "looked_up": len(keep)}


def base_file(path: Path, step: str) -> bytes:
    """A file of the base's, which the converter's `step` writes."""
    if not path.exists():
        raise LocaleError(f"no {path}: run `{step}` first")
    return path.read_bytes()


def mission_names(out: Path) -> list[str]:
    """generated/missions/index.txt's mission names."""
    lines = base_file(out / "missions" / "index.txt", "missions").decode("ascii").splitlines()
    return [line.split()[0] for line in lines if line]


def convert_missions(install: Install, locale: str, out: Path, profile_id: str, found: Findings) -> tuple[dict[str, bytes], dict]:
    """(mission name -> kpm bytes, name -> counts); a mission the language lacks keeps the base's text."""
    tables = chk.LoaderTables.load(profile_id)
    packs, report = {}, {}
    for name in mission_names(out):
        path = MISSION.format(locale=locale, name=name)
        if not install.has(path):
            report[name] = {"translated": 0}
            continue
        base = parse(base_file(out / "missions" / f"{name}.kpm", "missions"))
        try:
            scenario = chk.load(install.read(path), tables, vcod_sha256=tables.vcod_sha256)
        except chk.ChkError as e:
            found.problems.append(f"{name}: {e}")
            report[name] = {}
            continue
        strs, counts = merge_mission(name, base, scenario.sections["STR "], found)
        c = Container(kpm.KIND, kpm.SCHEMA, CONVERTER_VERSION, base.profile_id, source_hash=base.source_hash)
        c.add("STRS", strs)
        packs[name] = c.to_bytes()
        report[name] = counts
    return packs, report


def convert_subtitles(install: Install, locale: str, found: Findings) -> tuple[bytes | None, int]:
    """(SUBS with a row for each movie of the original's table or None, the movies with subtitles)."""
    names = cinematics.table()[0]
    try:
        subs = cinematics.read_subtitles(install, locale, names, required=True)
    except cinematics.CinematicsError as e:
        found.problems.append(str(e))
        return None, 0
    if not subs:
        return None, 0
    for name, cues in subs.items():
        for k, cue in enumerate(cues):
            found.note_text(f"{name}.srt cue {k}", cue.text.encode("utf-8"))
    return cinematics.subtitles_section(locale, [subs.get(name, []) for name in names]), len(subs)


def slots() -> dict[int, int]:
    """Code point -> FNT byte slot: the engine's kp_text_byte (ASCII and windows-1252's upper half)."""
    out = {b: b for b in range(ASCII_END)}
    out.update({ord(kpd.windows_1252_char(b)): b for b in range(ASCII_END, 0x100)})
    return out


def slot_has_glyph(font: bytes, slot: int) -> bool:
    low, high = font[4], font[5]
    return low <= slot <= high and struct.unpack_from("<I", font, FNT_HEADER + 4 * (slot - low))[0] != 0


def supplement_codes(payload: bytes) -> list[int]:
    count = struct.unpack_from("<I", payload)[0]
    return [SUPPLEMENT_ENTRY.unpack_from(payload, 4 + k * SUPPLEMENT_ENTRY.size)[0] for k in range(count)]


def glyph_set(used: dict[int, str], existing: bytes | None) -> list[int]:
    """Every code point the glyph supplement holds: the languages' code pages, what this language uses, and what the
    pack already holds for the languages converted before it; none with a byte slot."""
    has_slot = slots()
    codes = set(used)
    for codepage in GLYPH_CODEPAGES:
        for byte in range(ASCII_END, 0x100):
            try:
                codes.add(ord(bytes([byte]).decode(codepage)))
            except UnicodeDecodeError:
                pass
    if existing is not None:
        codes.update(supplement_codes(parse(existing).section("U8  ").payload))
    return sorted(c for c in codes if c not in has_slot and c >= CONTROL_END)


def rasterize(font: bytes, ttf: bytes, codes: list[int]) -> tuple[bytes, list[int]]:
    """(the supplement, the code points the TrueType font lacks) through tools/kp_fnt_make."""
    tool = Path(os.environ.get(FNT_MAKE_ENV, FNT_MAKE_DEFAULT))
    if not tool.exists():
        raise LocaleError(f"no {tool}: build the kp_fnt_make target (cmake --build build/cmake --target kp_fnt_make)")
    with tempfile.TemporaryDirectory() as tmp:
        paths = {name: Path(tmp) / name for name in ("font.fnt", "font.ttf", "codes.txt", "out")}
        paths["font.fnt"].write_bytes(font)
        paths["font.ttf"].write_bytes(ttf)
        paths["codes.txt"].write_text("".join(f"{c:x}\n" for c in codes))
        run = subprocess.run([str(tool), *(str(paths[n]) for n in ("font.fnt", "font.ttf", "codes.txt", "out"))],
                             capture_output=True, text=True, check=False)
        if run.returncode:
            raise LocaleError(f"kp_fnt_make: {run.stderr.strip()}")
        missing = [int(line.split()[1], 16) for line in run.stdout.splitlines() if line.startswith("missing ")]
        return paths["out"].read_bytes(), missing


def build_glyphs(install: Install, base_fonts: Container, used: dict[int, str], existing: bytes | None,
                 rasterizer: Rasterizer, found: Findings) -> tuple[Container, dict]:
    """glyphs.kpd: every font holds the same code points, the ones both TrueType fonts have; a code point the language
    uses in neither a slot nor the set is a problem, as is a slot of an FNT font with no glyph for it."""
    regular, wide = (install.read(path) for path in GLYPH_TTF)
    fonts = [(f["section"], base_fonts.section(f["section"]).payload) for f in kpd.font_defs()["fonts"]]
    has_slot = slots()
    for section, font in fonts:
        for code, where in used.items():
            if code != SPACE and code in has_slot and not slot_has_glyph(font, has_slot[code]):
                found.problems.append(f"{where}: U+{code:04X} has no glyph in {section.strip()}'s slot")
    codes = glyph_set(used, existing)
    dropped: set[int] = set()
    payloads: dict[str, bytes] = {}
    while True:  # a code point one TrueType font lacks leaves all five
        payloads.clear()
        for section, font in fonts:
            ttf = wide if section == WIDE_FONT_SECTION else regular
            payload, missing = rasterizer(font, ttf, codes)
            if missing:
                dropped.update(missing)
                codes = [c for c in codes if c not in dropped]
                break
            payloads["U" + section[1:]] = payload
        else:
            break
    for code in sorted(dropped & set(used)):
        found.problems.append(f"{used[code]}: U+{code:04X} is in neither a slot nor the TrueType fonts")
    c = Container(kpd.FONTS_KIND, kpd.FONTS_SCHEMA, CONVERTER_VERSION, base_fonts.profile_id,
                  source_hash=base_fonts.source_hash)
    names = dict(zip(VFNT_NAMES, (regular, wide)))
    c.add("VFNT", kpd.files_section(lambda name: names[name], list(VFNT_NAMES)))
    for section, payload in payloads.items():
        c.add(section, payload)
    return c, {"codes": len(codes), "dropped": [f"U+{code:04X}" for code in sorted(dropped)],
               "bytes": {section.strip(): len(payload) for section, payload in payloads.items()}}


def base_pack(out: Path, name: str) -> Container:
    return parse(base_file(out / "data" / name, "data"))


def generate(storage, locale: str, out: Path, *, rasterizer: Rasterizer = rasterize) -> dict:
    """The language's packs and generated/locale/glyphs.kpd under `out`; the report. LocaleError when any check fails:
    then nothing but the report is written."""
    defs = load_locales()
    check_locale(locale, defs)
    install = Install(storage)
    probe = REZ.format(locale=locale) + "RC3.xml"
    if not install.has(probe):
        raise LocaleError(f"the install names no {probe}")
    texts = json.loads(TEXTS.read_text(encoding="utf-8"))
    found = Findings(exceptions={(e["table"], e["row"]) for e in defs["exceptions"] if e["locale"] == locale})
    strings, ui, fonts = (base_pack(out, name) for name in ("strings.kpd", "ui.kpd", "fonts.kpd"))
    try:
        tables, table_report = convert_tables(install, locale, strings, texts, found)
        dlgs, pool, dialog_report = convert_dialogs(install, locale, ui, texts, found)
        scripts, left = convert_scripts(install, locale, ui, found)
        missions, mission_report = convert_missions(install, locale, out, strings.profile_id, found)
        subs, sub_count = convert_subtitles(install, locale, found)
        root = out / OUT_DIR
        glyphs_path = root / "glyphs.kpd"
        glyphs, glyph_report = build_glyphs(install, fonts, found.used, glyphs_path.read_bytes() if glyphs_path.exists() else None,
                                            rasterizer, found)
    except CascError as e:
        raise LocaleError(f"{locale} is not in the local storage: {e}") from e
    report = {"locale": locale, "tables": table_report, "dialogs": dialog_report,
              "scripts": {"translated": sorted(scripts), "base": left},
              "missions": mission_report, "subtitles": sub_count, "glyphs": glyph_report,
              "exceptions": found.exceptions_taken, "problems": found.problems}
    write_if_changed(out / "reports" / f"locale-{locale}.json",
                     (json.dumps(report, indent=1, ensure_ascii=False) + "\n").encode("utf-8"))
    if found.problems:
        listed = "; ".join(found.problems[:MAX_LISTED])
        raise LocaleError(f"{locale}: {len(found.problems)} problems (reports/locale-{locale}.json): {listed}")

    def pack(kind: bytes, schema: int, base: Container) -> Container:
        return Container(kind, schema, CONVERTER_VERSION, base.profile_id, source_hash=base.source_hash)

    written: set[Path] = set()

    def emit(path: Path, data: bytes) -> None:
        write_if_changed(path, data)
        written.add(path)

    c = pack(kpd.STRINGS_KIND, kpd.STRINGS_SCHEMA, strings)
    for section, payload in tables.items():
        c.add(section, payload, schema_version=kpd.WIDE_TBL_SCHEMA)
    emit(root / locale / "strings.kpd", c.to_bytes())
    c = pack(kpd.UI_KIND, kpd.UI_SCHEMA, ui)
    c.add("DLGS", dlgs)
    c.add("POOL", pool)
    c.add("GFIL", kpd.files_section(lambda name: scripts[name], list(scripts)))
    emit(root / locale / "ui.kpd", c.to_bytes())
    if subs is not None:
        c = pack(cinematics.CINE_KIND, cinematics.SCHEMA, strings)
        c.add("SUBS", subs)
        emit(root / locale / "cinematics.kpd", c.to_bytes())
    for name, data in missions.items():
        emit(root / locale / "missions" / f"{name}.kpm", data)
    # a pack the language no longer has (its subtitles, a map) would be loaded as if it were current
    for path in (root / locale).rglob("*"):
        if path.is_file() and path not in written:
            log.info("locale %s: removing %s, which this conversion did not write", locale, path)
            path.unlink()
    write_if_changed(glyphs_path, glyphs.to_bytes())
    log.info("locale %s: %d tables, %d dialog texts, %d scripts, %d missions, %d movies' subtitles, %d glyphs per font",
             locale, len(tables), dialog_report["translated"], len(scripts), len(missions), sub_count, glyph_report["codes"])
    return report
