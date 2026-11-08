"""`converter locale` (converter/emit/locale.py, ADR 0016) over a synthetic install and base packs: what a language's packs
hold, what keeps the base's text, and each check failing on data prepared to fail it, with nothing but the report written.
The rasterizer is injected: the converter's tests start before the native build, and tests/c/test_locale.c covers the
real one. The real ruRU goes through tools/ci.sh's step and test_locale's checks of its packs."""
from __future__ import annotations

import json
import struct
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

from converter.casc import rebuild
from converter.casc.storage import CascError
from converter.emit import cinematics, locale
from converter.formats import dialog
from converter.kpf import kpd, kpm
from converter.kpf.container import Container, parse
from converter.kpf.dump import dump
from converter.kpf.fixtures import chk_trigger, dialog_bin

LOCALE = "ruRU"
REZ = f"locales/{LOCALE}/Assets/rez/"
MISSION = "terran/terran01"
MISSION_PATH = f"locales/{LOCALE}/Assets/campaign/{MISSION}/staredit/scenario.chk"
HASH = bytes(range(32))
PROFILE = "bw-1.17.0.1"
SCRIPT = "rez\\intro.txt"
PICTURE = "glue\\x.pcx"
WAV_ACTION = sorted(kpm.TRIG_WAV_ACTIONS)[0]
# the map's strings: 1 a text, 2 a WAV's path, 3 the next scenario's name, 4 none, 5 a text the language leaves out
MAP_ROWS = [b"Objective", b"sound\\a.wav", b"terran02", None, b"Other"]
MAP_OURS = [b"\xd0\x97\xd0\xb0\xd0\xb4\xd0\xb0\xd1\x87\xd0\xb0", b"sound\\ru.wav", b"terran02-ru", None, locale.PLACEHOLDER]
FIRST_SLOT, LAST_SLOT = 32, 255
NO_GLYPH_SLOT = ord("~")


def tbl(spans: list[bytes]) -> bytes:
    at = 2 + 2 * len(spans)
    offsets = []
    for span in spans:
        offsets.append(at)
        at += len(span) + 1
    return struct.pack(f"<{len(spans) + 1}H", len(spans), *offsets) + b"".join(s + b"\0" for s in spans)


def xml(rows: list[tuple[str, bytes]]) -> bytes:
    return b"<strings>" + b"".join(b"<string><id>" + k.encode() + b"</id><value>" + v + b"</value></string>" for k, v in rows) \
        + b"</strings>"


def font_image() -> bytes:
    """Every slot has the same glyph, but for space and NO_GLYPH_SLOT."""
    count = LAST_SLOT - FIRST_SLOT + 1
    glyph = locale.FNT_HEADER + 4 * count
    offsets = [0 if code in (locale.SPACE, NO_GLYPH_SLOT) else glyph for code in range(FIRST_SLOT, LAST_SLOT + 1)]
    return b"FONT" + bytes((FIRST_SLOT, LAST_SLOT, 8, 8)) + struct.pack(f"<{count}I", *offsets) + bytes((1, 1, 0, 0, 1))


def rasterized(codes: list[int]) -> bytes:
    """A supplement's form: one glyph for all."""
    head = 4 + 8 * len(codes)
    return struct.pack("<I", len(codes)) + b"".join(struct.pack("<II", c, head) for c in codes) + bytes((1, 1, 0, 0, 1))


class Rasterizer:
    """kp_fnt_make's stand-in: `lacks` are the code points a TrueType font has no glyph for, by its bytes."""

    def __init__(self, lacks: dict[bytes, set[int]] | None = None):
        self.lacks = lacks or {}
        self.calls: list[tuple[bytes, bytes, list[int]]] = []

    def __call__(self, font: bytes, ttf: bytes, codes: list[int]) -> tuple[bytes, list[int]]:
        self.calls.append((font, ttf, list(codes)))
        lacks = self.lacks.get(ttf, set())
        return rasterized([c for c in codes if c not in lacks]), [c for c in codes if c in lacks]


class Storage:
    def __init__(self, files: dict[str, bytes], missing: tuple[str, ...] = ()):
        self.files = files
        self.missing = missing

    def names(self):
        return list(self.files)

    def read(self, name: str) -> bytes:
        if name in self.missing:
            raise CascError(f"{name} has no local data")
        return self.files[name]


def str_section(rows: list[bytes | None]) -> bytes:
    return locale.str_encode(rows, "test")


def next_scenario_trigger(string: int) -> bytes:
    record = bytearray(kpm.TRIGGER_SIZE)
    struct.pack_into("<I", record, kpm.ACTIONS_OFFSET + locale.ACTION_STRING, string)
    record[kpm.ACTIONS_OFFSET + kpm.ACTION_ID] = locale.NEXT_SCENARIO
    return bytes(record)


class World:
    """A base under `out` and an install whose ruRU every check accepts; a test spoils one thing of it."""

    def __init__(self, out: Path):
        self.out = out
        self.fixes: dict = {}  # locales.json's rows and layout for this world (stage L3)
        self.own: dict[str, dict[bytes, bytes]] = {}  # koprulu-<locale>.json's texts of our dialogs (stage L4)
        self.extend: dict[str, list[dict]] = {}  # records the base appends to a dialog of its own
        self.stat = [b"m\0\x03M\x01ove", b"Done %d of %s", b"Plain", b"", b"Placeholder", b"Last row", b"% Complete"]
        self.stat_xml = [("STR_MOVE", b"m\\007\\003\xd0\x94\xd0\xb2\xd0\xb8\xd0\xb3"),
                         ("STR_DONE", b"\xd0\x93\xd0\xbe\xd1\x82 %d \xd0\xb8\xd0\xb7 %s"),
                         ("STR_PLAIN", b"\xd0\x9f\xd1\x80\xd0\xbe\xd1\x81\xd1\x82\xd0\xbe"),
                         ("STR_EMPTY", b"\xd0\xad\xd1\x82\xd0\xbe\xd1\x82"), ("STR_HOLDER", locale.PLACEHOLDER),
                         ("STR_NOT_IN_THE_TBL", b"\xd0\x9d\xd0\xb5\xd1\x82"), ("STR_PERCENT", b"% \xd0\x93\xd0\xbe\xd1\x82")]
        self.rc3 = [("x-1", b"q\xd0\x96\xd0\xb0")]
        self.script = "</BACKGROUND glue\\x.pcx>Привет".encode()
        self.map_ours = list(MAP_OURS)
        self.dialog_base = dialog_bin()
        self.dialog_manifest = dialog_bin()
        self.lacks: dict[bytes, set[int]] = {}
        self.missing: tuple[str, ...] = ()
        self.srt = "1\n00:00:01,000 --> 00:00:02,500\nПривет\n\n".encode()
        self.map_fault: Exception | None = None

    def install(self) -> Storage:
        names = cinematics.table()[0]
        files = {REZ + "RC3.xml": xml(self.rc3), REZ + "stat_txt.xml": xml(self.stat_xml),
                 "rez/stat_txt.tbl": tbl(self.stat[:6]), REZ + "network.xml": xml([("n-1", b"\xd0\xa1\xd0\xb5\xd1\x82\xd1\x8c")]),
                 REZ + "gluall.xml": xml([]), REZ + "gluhist.xml": xml([]), REZ + "tips.xml": xml([]),
                 REZ + "help_txt.xml": xml([]), REZ + "intro.txt": self.script, MISSION_PATH: str_section(self.map_ours),
                 f"locales/{LOCALE}/Assets/Smk/{names[1].lower()}.srt": self.srt,
                 locale.GLYPH_TTF[0]: b"REGULAR", locale.GLYPH_TTF[1]: b"WIDE"}
        return Storage(files, self.missing)

    def manifest(self) -> dict:
        tables = {path: {"xml": f"SD/rez/{path[4:-4].lower()}.xml", "base": None, "ids": ["t_1"]} for path in kpd.TBL_FILES.values()}
        tables["rez\\stat_txt.tbl"].update(base="rez/stat_txt.tbl", ids=[f"r{k}" for k in range(len(self.stat))])
        tables["rez\\network.tbl"]["ids"] = ["n_1"]
        return {"tbl": tables, "bin": {"rez\\x.bin": rebuild.dialog_table(self.dialog_manifest, [("x-1", b"quit")], {})}}

    def write_base(self) -> None:
        data = self.out / "data"
        data.mkdir(parents=True, exist_ok=True)

        def pack(kind: bytes, schema: int) -> Container:
            return Container(kind, schema, 1, PROFILE, source_hash=HASH)

        c = pack(kpd.STRINGS_KIND, kpd.STRINGS_SCHEMA)
        for section in kpd.TBL_FILES:
            c.add(section, tbl(self.stat if section == "STAT" else [b"Network"]))
        (data / "strings.kpd").write_bytes(c.to_bytes())
        c = pack(kpd.UI_KIND, kpd.UI_SCHEMA)
        dlgs, pool = kpd.dialogs_section({"rez\\x.bin": self.dialog_base}, self.extensions())
        c.add("DLGS", dlgs)
        c.add("POOL", pool)
        files = {PICTURE: b"abc", SCRIPT: b"</BACKGROUND glue\\x.pcx>Hello"}
        c.add("GFIL", kpd.files_section(lambda name: files[name], list(files)))
        (data / "ui.kpd").write_bytes(c.to_bytes())
        c = pack(kpd.FONTS_KIND, kpd.FONTS_SCHEMA)
        for f in kpd.font_defs()["fonts"]:
            c.add(f["section"], font_image())
        (data / "fonts.kpd").write_bytes(c.to_bytes())
        missions = self.out / "missions"
        (missions / "terran").mkdir(parents=True, exist_ok=True)
        (missions / "index.txt").write_text(f"{MISSION} 0 64 64\n")
        c = Container(kpm.KIND, kpm.SCHEMA, 1, PROFILE, source_hash=HASH)
        c.add("STRS", str_section(MAP_ROWS))
        c.add("TRIG", chk_trigger([(WAV_ACTION, 2)]) + next_scenario_trigger(3))
        c.add("MBRF", b"")
        (missions / f"{MISSION}.kpm").write_bytes(c.to_bytes())

    def extensions(self) -> dict:
        root = {"type": dialog.TYPE_CONTAINER, "id": 0, "flags": 0, "rect": [0, 0, 640, 480]}
        return {name: dialog.parse(dialog.build([root, *widgets]))[1:] for name, widgets in self.extend.items()}

    def load_map(self, data: bytes, tables, vcod_sha256=None) -> SimpleNamespace:
        if self.map_fault is not None:
            raise self.map_fault
        return SimpleNamespace(sections={"STR ": data})

    def run(self, rasterizer: Rasterizer | None = None) -> tuple[dict, Rasterizer]:
        self.write_base()
        manifest = self.out / "manifest.json"
        manifest.write_text(json.dumps(self.manifest()))
        raster = rasterizer or Rasterizer(self.lacks)
        defs = {**locale.load_locales(), "rows": [], "layout": [], "keep": [], "texts": [], "strings": [], "templates": [],
                **self.fixes}  # the repository's fixes name real rows
        with mock.patch.object(locale, "TEXTS", manifest), \
                mock.patch.object(locale, "load_locales", lambda: defs), \
                mock.patch.object(locale, "our_texts", lambda loc: self.own), \
                mock.patch.object(locale.kpd, "koprulu_extensions", self.extensions), \
                mock.patch.object(locale.chk, "load", self.load_map), \
                mock.patch.object(locale.chk.LoaderTables, "load", lambda profile: SimpleNamespace(vcod_sha256=None)):
            return locale.generate(self.install(), LOCALE, self.out, rasterizer=raster), raster

    def pack(self, *path: str) -> Container:
        return parse((self.out / "locale" / Path(*path)).read_bytes())


class WorldCase(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.world = World(Path(tmp.name))

    def assert_refused(self, *words: str) -> list[str]:
        """The language is refused naming the words: its report says why and no pack was written."""
        with self.assertRaises(locale.LocaleError):
            self.world.run()
        report = json.loads((self.world.out / "reports" / f"locale-{LOCALE}.json").read_text())
        joined = "\n".join(report["problems"])
        for word in words:
            self.assertIn(word, joined)
        self.assertFalse((self.world.out / "locale").exists(), "no pack is written")
        return report["problems"]


class PacksTest(WorldCase):
    def test_tables_hold_the_language_in_the_wide_form(self):
        report, _ = self.world.run()
        self.assertEqual(report["problems"], [])
        section = self.world.pack(LOCALE, "strings.kpd").section("STAT")
        self.assertEqual(section.schema_version, kpd.WIDE_TBL_SCHEMA)
        payload = section.payload
        count = struct.unpack_from("<I", payload)[0]
        self.assertEqual(count, len(self.world.stat))

        def row(k: int) -> bytes:
            at = struct.unpack_from("<I", payload, 4 + 4 * k)[0]
            end = struct.unpack_from("<I", payload, 8 + 4 * k)[0] if k + 1 < count else len(payload)
            return payload[at:end - 1]

        self.assertEqual(row(0), b"m\0\x03\xd0\x94\xd0\xb2\xd0\xb8\xd0\xb3", "a span's NUL from the XML's 0x07")
        self.assertEqual(row(1), self.world.stat_xml[1][1])
        self.assertEqual(row(3), b"", "a row empty in the base stays so")
        self.assertEqual(row(4), b"Placeholder", "a placeholder keeps the base's")
        self.assertEqual(row(5), "Нет".encode(), "the rows of Remastered's TBL go by place")
        self.assertEqual(row(6), b"% Complete", "past Remastered's TBL the base's")
        self.assertEqual(report["tables"]["STAT"]["translated"], 4)

    def test_a_table_by_the_manifests_ids(self):
        self.world.run()
        payload = self.world.pack(LOCALE, "strings.kpd").section("NETW").payload
        self.assertEqual(payload[8:-1], "Сеть".encode(), "the id n_1 is the XML's n-1")
        payload = self.world.pack(LOCALE, "strings.kpd").section("TIPS").payload
        self.assertEqual(payload[8:-1], b"Network", "a table whose ids the language lacks keeps the base's")

    def test_only_the_rows_listed_may_change_conversions(self):
        self.world.stat[5] = b"% Complete"
        self.world.stat_xml[5] = ("STR_PERCENT", "% Гот".encode())
        report, _ = self.world.run()
        self.assertEqual(report["exceptions"], ["STAT STR_PERCENT"])

    def test_dialogs_keep_their_records(self):
        report, _ = self.world.run()
        self.assertEqual(report["dialogs"]["translated"], 1)
        pack = self.world.pack(LOCALE, "ui.kpd")
        base = locale.read_dialogs(kpd.dialogs_section({"rez\\x.bin": dialog_bin()})[0])[0]
        ours = locale.read_dialogs(pack.section("DLGS").payload)[0]
        pool = pack.section("POOL").payload
        button = ours.widgets[1]
        self.assertEqual(pool[button[locale.WIDGET_TEXT_AT]:][:button[locale.WIDGET_TEXT_LEN]], "qЖа".encode())
        self.assertEqual(pool[ours.smks[0][locale.SMK_NAME_AT]:][:8], b"port.smk", "a movie's name stays")
        for a, b in zip(base.widgets, ours.widgets):
            self.assertEqual(locale.bare_record(a, locale.WIDGET_TEXT_AT, locale.WIDGET_TEXT_LEN),
                             locale.bare_record(b, locale.WIDGET_TEXT_AT, locale.WIDGET_TEXT_LEN))

    def test_scripts(self):
        report, _ = self.world.run()
        self.assertEqual(report["scripts"]["translated"], [SCRIPT])
        files = dict(kpd.read_files(self.world.pack(LOCALE, "ui.kpd").section("GFIL").payload))
        self.assertEqual(files, {SCRIPT: self.world.script}, "only the language's texts")

    def test_missions_keep_the_strings_looked_up_by_their_text(self):
        report, _ = self.world.run()
        rows = locale.str_rows(self.world.pack(LOCALE, "missions", f"{MISSION}.kpm").section("STRS").payload)
        self.assertEqual(rows[0], MAP_OURS[0], "a text")
        self.assertEqual(rows[1], MAP_ROWS[1], "a WAV's path")
        self.assertEqual(rows[2], MAP_ROWS[2], "the next scenario's name")
        self.assertIsNone(rows[3], "a row with no text")
        self.assertEqual(rows[4], MAP_ROWS[4], "a placeholder")
        self.assertEqual(report["missions"][MISSION]["translated"], 1)

    def test_a_mission_the_language_lacks_keeps_its_own(self):
        whole = self.world.install
        self.world.install = lambda: Storage({k: v for k, v in whole().files.items() if k != MISSION_PATH})
        report, _ = self.world.run()
        self.assertEqual(report["missions"][MISSION], {"translated": 0})
        self.assertFalse((self.world.out / "locale" / LOCALE / "missions" / f"{MISSION}.kpm").exists())

    def test_subtitles_have_a_row_for_each_movie(self):
        report, _ = self.world.run()
        self.assertEqual(report["subtitles"], 1)
        subs = self.world.pack(LOCALE, "cinematics.kpd").section("SUBS").payload
        names = cinematics.table()[0]
        self.assertEqual(subs[:4], LOCALE.encode())
        total, text = struct.unpack_from("<II", subs, 4)
        self.assertEqual((total, text), (1, len("Привет".encode())))
        rows = [struct.unpack_from("<II", subs, 12 + 8 * k) for k in range(len(names))]
        self.assertEqual(rows[1], (0, 1), "the second movie's")
        self.assertEqual(sum(count for _, count in rows), 1)
        self.assertEqual(subs[-text:], "Привет".encode())

    def test_a_pack_the_language_no_longer_has_goes(self):
        self.world.run()
        root = self.world.out / "locale" / LOCALE
        whole = self.world.install
        self.world.install = lambda: Storage({k: v for k, v in whole().files.items()
                                              if k != MISSION_PATH and not k.endswith(".srt")})
        self.world.run()
        self.assertFalse((root / "cinematics.kpd").exists(), "its subtitles")
        self.assertFalse((root / "missions" / f"{MISSION}.kpm").exists(), "a map")
        self.assertTrue((root / "strings.kpd").exists(), "what it still has")

    def test_the_same_data_gives_the_same_bytes(self):
        self.world.run()
        paths = sorted(p for p in (self.world.out / "locale").rglob("*") if p.is_file())
        first = {p: p.read_bytes() for p in paths}
        self.world.run()
        self.assertEqual(first, {p: p.read_bytes() for p in paths})


class GlyphsTest(WorldCase):
    def test_every_font_holds_the_same_codes_without_slots(self):
        report, raster = self.world.run()
        pack = self.world.pack("glyphs.kpd")
        self.assertEqual(pack.kind, kpd.FONTS_KIND)
        codes = [locale.supplement_codes(pack.section(s).payload) for s in ("U8  ", "U10 ", "U14 ", "U16 ", "U16X")]
        self.assertTrue(all(c == codes[0] for c in codes))
        self.assertEqual(codes[0], sorted(codes[0]), "ascending")
        self.assertIn(ord("Ж"), codes[0], "the language's")
        self.assertIn(ord("ł"), codes[0], "and the other languages' code pages")
        self.assertNotIn(ord("A"), codes[0], "ASCII has its slot")
        self.assertNotIn(0x2019, codes[0], "windows-1252's upper half too")
        self.assertEqual(report["glyphs"]["codes"], len(codes[0]))
        by_ttf = [ttf for _, ttf, _ in raster.calls]
        self.assertEqual(by_ttf, [b"REGULAR"] * 4 + [b"WIDE"], "font16x from the wide font")

    def test_the_vector_fonts_are_the_installs(self):
        self.world.run()
        files = dict(kpd.read_files(self.world.pack("glyphs.kpd").section("VFNT").payload))
        self.assertEqual(files, dict(zip(locale.VFNT_NAMES, (b"REGULAR", b"WIDE"))))

    def test_a_code_one_font_lacks_leaves_them_all(self):
        self.world.lacks = {b"WIDE": {ord("ł")}}
        report, _ = self.world.run()
        self.assertEqual(report["glyphs"]["dropped"], ["U+0142"])
        pack = self.world.pack("glyphs.kpd")
        for section in ("U8  ", "U16X"):
            self.assertNotIn(ord("ł"), locale.supplement_codes(pack.section(section).payload))

    def test_a_language_converted_before_keeps_its_glyphs(self):
        self.world.run()
        path = self.world.out / "locale" / "glyphs.kpd"
        c = parse(path.read_bytes())
        again = Container(c.kind, c.schema_version, 1, c.profile_id, source_hash=c.source_hash)
        for section in ("U8  ", "U10 ", "U14 ", "U16 ", "U16X"):
            again.add(section, rasterized([0x3042]))
        path.write_bytes(again.to_bytes())
        self.world.run()
        self.assertIn(0x3042, locale.supplement_codes(self.world.pack("glyphs.kpd").section("U8  ").payload))

    def test_a_character_of_the_language_with_no_glyph_fails(self):
        self.world.stat_xml[2] = ("STR_PLAIN", "一".encode())
        self.world.lacks = {b"REGULAR": {0x4e00}}
        self.assert_refused("U+4E00 is in neither a slot nor the TrueType fonts")

    def test_a_slot_with_no_glyph_fails(self):
        self.world.stat_xml[2] = ("STR_PLAIN", b"~")
        self.assert_refused("U+007E has no glyph in")

    def test_a_space_needs_no_glyph(self):
        self.world.stat_xml[2] = ("STR_PLAIN", b"a b")
        self.assertEqual(self.world.run()[0]["problems"], [])


class ChecksTest(WorldCase):
    def spoil(self, row: int, text: bytes) -> None:
        self.world.stat_xml[row] = (self.world.stat_xml[row][0], text)

    def test_not_utf8(self):
        self.spoil(2, b"\xff\xfe")
        self.assert_refused("not UTF-8")

    def test_another_conversion(self):
        self.spoil(1, b"\xd0\x93 %d \xd0\xb8\xd0\xb7 %d")
        self.assert_refused("STAT STR_DONE: conversions")

    def test_a_conversion_lost(self):
        self.spoil(1, b"\xd0\x93 %s")
        self.assert_refused("conversions")

    def test_conversions_in_another_order(self):
        self.spoil(1, b"%s \xd0\xb8\xd0\xb7 %d")
        self.assert_refused("conversions")

    def test_a_nul_lost(self):
        self.spoil(0, b"m\\003\xd0\x94")
        self.assert_refused("1 NULs became 0")

    def test_a_buttons_form(self):
        self.spoil(0, b"\xd0\x94\\007\\003\xd0\xb2")
        self.assert_refused("the button form")

    def test_a_table_of_other_rows_than_the_manifests(self):
        whole = self.world.manifest

        def manifest() -> dict:
            out = whole()
            out["tbl"]["rez\\network.tbl"]["ids"] = []
            return out

        self.world.manifest = manifest
        self.assert_refused("NETW: the manifest has 0 rows for the base's 1")

    def test_a_control_byte_the_table_has_not(self):
        self.spoil(2, b"\xd0\x9f\\025")
        self.assert_refused("control bytes [25]")

    def test_a_substitution_of_remastereds(self):
        self.spoil(2, b"{PlayerName}")
        self.assert_refused("a substitution the engine does not make")

    def test_a_dialogs_hotkey(self):
        self.world.rc3 = [("x-1", "Жаqx".encode())]
        self.assert_refused("is not ASCII")

    def test_a_dialog_of_other_records(self):
        data = bytearray(dialog_bin())
        data[86 + 4] = 9  # the button's left edge
        data[86 + 8] = 19
        self.world.dialog_manifest = bytes(data)
        self.assert_refused("rez\\x.bin record 1: not the base's")

    def test_a_dialog_of_fewer_records(self):
        data = bytearray(dialog_bin())  # the root has no child: the manifest's copy has only it
        struct.pack_into("<I", data, 86, 0)
        self.world.dialog_manifest = bytes(data)
        self.assert_refused("records")

    def test_a_script_naming_a_file_the_base_lacks(self):
        self.world.script = b"</BACKGROUND glue\\ru.pcx>text"
        self.assert_refused("which the base's GFIL lacks")

    def test_a_script_not_utf8(self):
        self.world.script = b"\xff"
        self.assert_refused(f"{SCRIPT}: not UTF-8")

    def test_a_maps_string_count(self):
        self.world.map_ours = self.world.map_ours[:-1]
        self.assert_refused("5 map strings became 4")

    def test_a_maps_text_not_utf8(self):
        self.world.map_ours[0] = b"\xff"
        self.assert_refused("string 1: not UTF-8")

    def test_a_maps_control_byte_its_strings_have_not(self):
        self.world.map_ours[0] = b"\x03\xd0\x96"
        self.assert_refused("string 1: control bytes [3]")

    def test_a_maps_substitution_of_remastereds(self):
        self.world.map_ours[0] = b"{Name} \xd0\x96"
        self.assert_refused("string 1: a substitution")

    def test_a_maps_strings_past_the_sections_reach(self):
        # the file itself fits: the rows looked up by their text, the base's, take it past
        self.world.map_ours = ["Ж".encode() * 32750, b"", b"", None, b""]
        self.assert_refused("16-bit offsets")

    def test_a_map_that_does_not_load(self):
        self.world.map_fault = locale.chk.ChkError("a broken section")
        self.assert_refused(f"{MISSION}: a broken section")

    def test_an_escape_past_a_byte(self):
        self.spoil(2, b"\\999")
        self.assert_refused("STR_PLAIN: the escape")

    def test_a_stray_mark_beside_a_nul(self):
        self.spoil(0, b"m\\007\\003\\007\xd0\x94")
        self.assert_refused("STAT STR_MOVE: 1 marks that may be NULs became 2")

    def test_a_subtitle_not_utf8(self):
        self.world.srt = "1\n00:00:01,000 --> 00:00:02,000\nПривет\n\n".encode("cp1251")
        self.assert_refused(".srt")

    def test_too_many_subtitles(self):
        self.world.srt = b"".join(b"%d\n00:00:01,000 --> 00:00:02,000\nx\n\n" % k for k in range(cinematics.CUES_MAX + 1))
        self.assert_refused("more than")

    def test_all_the_failures_are_listed(self):
        self.spoil(1, b"%s")
        self.spoil(2, b"\xff")
        self.world.map_ours = self.world.map_ours[:-1]
        self.assertGreaterEqual(len(self.assert_refused("conversions", "not UTF-8", "map strings")), 3)


class FitsTest(WorldCase):
    """A language's fixes for its widths (localization's stage L3): locales.json's shorter rows and layout."""

    def run_with(self, **fixes) -> dict:
        self.world.fixes = fixes
        return self.world.run()[0]

    def refused_with(self, *words: str, **fixes) -> None:
        self.world.fixes = fixes
        self.assert_refused(*words)

    def stat_row(self, k: int) -> bytes:
        payload = self.world.pack(LOCALE, "strings.kpd").section("STAT").payload
        at = struct.unpack_from("<I", payload, 4 + 4 * k)[0]
        end = struct.unpack_from("<I", payload, 8 + 4 * k)[0]
        return payload[at:end - 1]

    def test_a_shorter_row_replaces_the_languages(self):
        report = self.run_with(rows=[{"locale": LOCALE, "table": "STAT", "row": 3, "text": "Пр", "why": "test"}])
        self.assertEqual(self.stat_row(2), "Пр".encode())
        self.assertEqual(report["tables"]["STAT"]["shorter"], 1)

    def test_a_shorter_row_keeps_its_nuls_as_the_xml_marks_them(self):
        self.run_with(rows=[{"locale": LOCALE, "table": "STAT", "row": 1, "text": "m\u0007\u0003Д", "why": "test"}])
        self.assertEqual(self.stat_row(0), "m\0\x03Д".encode())

    def test_another_languages_rows_are_not_taken(self):
        self.run_with(rows=[{"locale": "deDE", "table": "STAT", "row": 3, "text": "Kurz", "why": "test"}])
        self.assertEqual(self.stat_row(2), self.world.stat_xml[2][1])

    def test_a_shorter_row_is_checked_as_the_languages(self):
        self.refused_with("a substitution the engine does not make",
                          rows=[{"locale": LOCALE, "table": "STAT", "row": 3, "text": "{PlayerName}", "why": "test"}])

    def test_a_shorter_row_past_the_table(self):
        self.refused_with("STAT 99: the shorter text names no such row",
                          rows=[{"locale": LOCALE, "table": "STAT", "row": 99, "text": "x", "why": "test"}])

    def test_the_layout_moves_a_records_rectangle(self):
        report = self.run_with(layout=[{"locale": LOCALE, "dialog": "rez\\x.bin", "record": 1, "rect": [1, 2, 30, 40],
                                        "why": "test"}])
        ours = locale.read_dialogs(self.world.pack(LOCALE, "ui.kpd").section("DLGS").payload)[0]
        button = ours.widgets[1]
        self.assertEqual(button[locale.WIDGET_RECT:locale.WIDGET_RECT + 4], (1, 2, 30, 40))
        self.assertEqual(button[locale.WIDGET_SIZE:locale.WIDGET_SIZE + 2], (30, 39), "the width and height follow")
        self.assertEqual(report["dialogs"]["layout"], 1)

    def test_a_layout_of_no_record(self):
        self.refused_with("rez\\x.bin record 9: the layout names no such record",
                          layout=[{"locale": LOCALE, "dialog": "rez\\x.bin", "record": 9, "rect": [1, 2, 3, 4], "why": "t"}])

    def test_an_empty_layout(self):
        self.refused_with("is empty",
                          layout=[{"locale": LOCALE, "dialog": "rez\\x.bin", "record": 1, "rect": [9, 2, 3, 4], "why": "t"}])

    def test_the_layout_moves_a_buttons_text_rectangle(self):
        report = self.run_with(layout=[{"locale": LOCALE, "dialog": "rez\\x.bin", "record": 2, "text": [5, 6, 70, 80],
                                        "why": "test"}])
        ours = locale.read_dialogs(self.world.pack(LOCALE, "ui.kpd").section("DLGS").payload)[0]
        base = locale.read_dialogs(kpd.dialogs_section({"rez\\x.bin": dialog_bin()})[0])[0]
        button = ours.widgets[2]
        self.assertEqual(button[locale.WIDGET_TEXT_RECT:locale.WIDGET_TEXT_RECT + 4], (5, 6, 70, 80))
        self.assertEqual(button[:locale.WIDGET_TEXT_AT], base.widgets[2][:locale.WIDGET_TEXT_AT], "the rectangle stays")
        self.assertEqual(report["dialogs"]["layout"], 1)

    def test_a_text_rectangle_is_a_buttons(self):
        self.refused_with("record 1: the layout's text rectangle is for a button",
                          layout=[{"locale": LOCALE, "dialog": "rez\\x.bin", "record": 1, "text": [1, 2, 3, 4], "why": "t"}])

    def test_a_layout_with_no_rectangle(self):
        self.refused_with("record 2: the layout gives neither rect nor text",
                          layout=[{"locale": LOCALE, "dialog": "rez\\x.bin", "record": 2, "why": "t"}])

    def test_a_record_laid_out_twice(self):
        self.refused_with("locales.json's layout names rez\\x.bin 2 twice",
                          layout=[{"locale": LOCALE, "dialog": "rez\\x.bin", "record": 2, "rect": [1, 2, 3, 4], "why": "t"},
                                  {"locale": LOCALE, "dialog": "rez\\x.bin", "record": 2, "text": [1, 2, 3, 4], "why": "t"}])

    def test_a_row_shortened_twice(self):
        self.refused_with("locales.json's rows names STAT 3 twice",
                          rows=[{"locale": LOCALE, "table": "STAT", "row": 3, "text": "a", "why": "t"},
                                {"locale": LOCALE, "table": "STAT", "row": 3, "text": "b", "why": "t"}])

    def test_another_languages_entry_is_no_second(self):
        self.run_with(rows=[{"locale": LOCALE, "table": "STAT", "row": 3, "text": "Пр", "why": "t"},
                            {"locale": "deDE", "table": "STAT", "row": 3, "text": "Kurz", "why": "t"}])
        self.assertEqual(self.stat_row(2), "Пр".encode())

    def test_an_empty_text_rectangle(self):
        self.refused_with("the layout's text [1, 9, 3, 4] is empty",
                          layout=[{"locale": LOCALE, "dialog": "rez\\x.bin", "record": 2, "text": [1, 9, 3, 4], "why": "t"}])

    def test_the_repositorys_fixes_name_real_rows_and_records(self):
        defs = locale.load_locales()
        for e in defs.get("rows", []):
            self.assertIn(e["table"], kpd.TBL_FILES)
            self.assertTrue(e["why"])
        for e in defs.get("layout", []):
            self.assertTrue(any(key in e for key in locale.LAYOUT_KEYS), e)
            for key in locale.LAYOUT_KEYS:
                if key in e:
                    left, top, right, bottom = e[key]
                    self.assertTrue(left <= right and top <= bottom and right < 640 and bottom < 480, e)
            self.assertTrue(e["why"])


class MendTest(WorldCase):
    """What a language's data needs on top of the fixes of its widths (stage L6): Remastered's run-time templates filled
    in, a Latin-1 slip of a map mended, a record or string of the language replaced, a record kept as the base's."""

    def run_with(self, **fixes) -> dict:
        self.world.fixes = fixes
        return self.world.run()[0]

    def refused_with(self, *words: str, **fixes) -> None:
        self.world.fixes = fixes
        self.assert_refused(*words)

    def stat_row(self, k: int) -> bytes:
        payload = self.world.pack(LOCALE, "strings.kpd").section("STAT").payload
        at = struct.unpack_from("<I", payload, 4 + 4 * k)[0]
        end = struct.unpack_from("<I", payload, 8 + 4 * k)[0]
        return payload[at:end - 1]

    def record_text(self, k: int) -> bytes:
        ours, pool = locale.read_dialogs(self.world.pack(LOCALE, "ui.kpd").section("DLGS").payload)[0], \
            self.world.pack(LOCALE, "ui.kpd").section("POOL").payload
        return locale.pool_text(pool, ours.widgets[k][locale.WIDGET_TEXT_AT], ours.widgets[k][locale.WIDGET_TEXT_LEN])

    def map_string(self, k: int) -> bytes:
        return locale.str_rows(self.world.pack(LOCALE, "missions", f"{MISSION}.kpm").section("STRS").payload)[k - 1]

    TEMPLATE = {"name": "Year", "text": "2019", "why": "test"}

    def test_a_template_is_filled_in(self):
        self.world.stat_xml[2] = ("STR_PLAIN", b"\xd0\x9f {Year}")
        report = self.run_with(templates=[self.TEMPLATE])
        self.assertEqual(self.stat_row(2), "П 2019".encode())
        self.assertTrue(any("stat_txt.xml STR_PLAIN" in where for where in report["mended"]))

    def test_a_template_nobody_fills_is_refused(self):
        self.world.stat_xml[2] = ("STR_PLAIN", b"\xd0\x9f {Year}")
        self.refused_with("a substitution the engine does not make: b'{Year}'", templates=[])

    def test_a_template_of_a_dialog_text(self):
        self.world.rc3 = [("x-1", b"q{Year}")]
        self.run_with(templates=[self.TEMPLATE])
        self.assertEqual(self.record_text(1), b"q2019")

    def test_a_c1_control_is_the_windows_1252_character_of_its_byte(self):
        self.world.map_ours[0] = "Ж\u0096".encode()  # an en dash decoded as Latin-1 on its way to UTF-8
        report = self.run_with()
        self.assertEqual(self.map_string(1), "Ж\u2013".encode())
        self.assertIn(f"{MISSION} string 1", report["mended"])

    def test_a_c1_control_windows_1252_leaves_undefined_stays(self):
        self.assertEqual(locale.c1_character("\u0081"), "\u0081", "byte 0x81 is no character of windows-1252")
        self.assertEqual(locale.c1_character("\u0096"), "\u2013")
        self.assertEqual(locale.c1_character("\u00e9"), "\u00e9", "only C1 controls are mended")
        self.world.map_ours[0] = "Ж\u0081".encode()
        report = self.run_with()
        self.assertEqual(self.map_string(1), "Ж\u0081".encode(), "left as it is, for the glyph check")
        self.assertEqual(report["mended"], [])

    def test_a_text_nothing_mends_is_not_listed(self):
        self.assertEqual(self.run_with()["mended"], [])

    def test_a_record_is_kept_as_the_bases(self):
        self.world.rc3 = [("x-1", "Жаqx".encode())]  # its hotkey is not ASCII
        report = self.run_with(keep=[{"locale": LOCALE, "dialog": "rez\\x.bin", "record": 1, "why": "test"}])
        self.assertEqual(self.record_text(1), b"quit")
        self.assertEqual(report["dialogs"]["kept"], 1)

    def test_a_record_kept_that_is_not_there(self):
        self.refused_with("rez\\x.bin record 9: locales.json keeps the base's text of no such record",
                          keep=[{"locale": LOCALE, "dialog": "rez\\x.bin", "record": 9, "why": "test"}])

    def test_a_record_kept_twice(self):
        self.refused_with("locales.json's keep names rez\\x.bin 1 twice",
                          keep=[{"locale": LOCALE, "dialog": "rez\\x.bin", "record": 1, "why": "t"},
                                {"locale": LOCALE, "dialog": "rez\\x.bin", "record": 1, "why": "t"}])

    def test_a_records_text_is_replaced(self):
        report = self.run_with(texts=[{"locale": LOCALE, "dialog": "rez\\x.bin", "record": 1, "text": "qКор", "why": "test"}])
        self.assertEqual(self.record_text(1), "qКор".encode())
        self.assertEqual(report["dialogs"]["translated"], 1)

    def test_a_replaced_text_is_checked_as_the_languages(self):
        self.refused_with("a substitution the engine does not make",
                          texts=[{"locale": LOCALE, "dialog": "rez\\x.bin", "record": 1, "text": "q{Name}", "why": "t"}])

    def test_a_replaced_texts_hotkey_must_be_ascii(self):
        self.refused_with("record 1: the hotkey of locales.json's",
                          texts=[{"locale": LOCALE, "dialog": "rez\\x.bin", "record": 1, "text": "Ж", "why": "t"}])

    def test_a_text_for_no_record(self):
        self.refused_with("rez\\x.bin record 9: locales.json gives a text to no such record",
                          texts=[{"locale": LOCALE, "dialog": "rez\\x.bin", "record": 9, "text": "q", "why": "t"}])

    def test_a_record_given_a_text_twice(self):
        self.refused_with("locales.json's texts names rez\\x.bin 1 twice",
                          texts=[{"locale": LOCALE, "dialog": "rez\\x.bin", "record": 1, "text": "q", "why": "t"},
                                 {"locale": LOCALE, "dialog": "rez\\x.bin", "record": 1, "text": "q", "why": "t"}])

    def test_another_languages_text_is_not_taken(self):
        self.run_with(texts=[{"locale": "deDE", "dialog": "rez\\x.bin", "record": 1, "text": "qKurz", "why": "t"}])
        self.assertEqual(self.record_text(1), "qЖа".encode())

    def test_a_map_strings_text_is_replaced(self):
        report = self.run_with(strings=[{"locale": LOCALE, "mission": MISSION, "string": 1, "text": "Кор", "why": "test"}])
        self.assertEqual(self.map_string(1), "Кор".encode())
        self.assertEqual(report["missions"][MISSION]["translated"], 1, "the replaced string counts as the language's")

    def test_a_replaced_map_string_is_checked_as_the_languages(self):
        self.refused_with("string 1: a substitution",
                          strings=[{"locale": LOCALE, "mission": MISSION, "string": 1, "text": "{Name}", "why": "t"}])

    def test_a_text_for_no_mission(self):
        self.refused_with("terran/none string 1: locales.json gives a text to no such mission",
                          strings=[{"locale": LOCALE, "mission": "terran/none", "string": 1, "text": "x", "why": "t"}])

    def test_a_text_for_no_string(self):
        self.refused_with(f"{MISSION} string 9: locales.json gives a text to no such string",
                          strings=[{"locale": LOCALE, "mission": MISSION, "string": 9, "text": "x", "why": "t"}])

    def test_a_map_string_given_a_text_twice(self):
        self.refused_with(f"locales.json's strings names {MISSION} 1 twice",
                          strings=[{"locale": LOCALE, "mission": MISSION, "string": 1, "text": "a", "why": "t"},
                                   {"locale": LOCALE, "mission": MISSION, "string": 1, "text": "b", "why": "t"}])

    def test_the_repositorys_entries_are_reasoned(self):
        defs = locale.load_locales()
        known = {row["id"] for row in defs["locales"]}
        for name in ("templates", "keep", "texts", "strings"):
            for e in defs.get(name, []):
                self.assertTrue(e["why"], (name, e))
                self.assertIn(e.get("locale", LOCALE), known, (name, e))
        for e in defs.get("templates", []):
            self.assertRegex(e["name"], r"^[A-Za-z_]+$")
        for e in defs.get("texts", []):
            first = e["text"].encode("utf-8")[:1]
            self.assertTrue(first < b"\x80", f"{e['dialog']} {e['record']}: the first byte of a text, a hotkey or not, is ASCII")


class AllTest(WorldCase):
    """`converter locale --locale all` (stage L6): each language the install holds."""

    DE = "locales/deDE/Assets/rez/RC3.xml"

    def storage(self, held: bool = False) -> Storage:
        install = self.world.install()
        install.files[self.DE] = b""  # the install lists deDE's file ...
        if not held:
            install.missing = (self.DE,)  # ... without its data
        return install

    def test_the_languages_the_install_holds(self):
        self.assertEqual(locale.installed(self.storage()), [LOCALE])
        self.assertEqual(locale.installed(self.storage(held=True)), ["deDE", LOCALE])

    def test_an_install_with_no_language(self):
        with self.assertRaisesRegex(locale.LocaleError, "holds no language"):
            locale.generate_all(Storage({}), self.world.out)

    def test_each_language_the_install_holds_is_converted(self):
        done = []
        with mock.patch.object(locale, "generate", lambda storage, name, out, rasterizer=None: done.append(name)):
            self.assertEqual(locale.generate_all(self.storage(held=True), self.world.out), ["deDE", LOCALE])
        self.assertEqual(done, ["deDE", LOCALE])

    def test_a_language_that_fails_does_not_stop_the_others(self):
        done = []

        def generate(storage, name, out, rasterizer=None):
            done.append(name)
            if name == "deDE":
                raise locale.LocaleError("deDE: 1 problems")

        with mock.patch.object(locale, "generate", generate), self.assertRaisesRegex(locale.LocaleError, "deDE"):
            locale.generate_all(self.storage(held=True), self.world.out)
        self.assertEqual(done, ["deDE", LOCALE], "ruRU was converted after deDE failed")


class OursTest(WorldCase):
    """Our own records in a language (localization's stage L4): the texts of defs/l10n/koprulu-<locale>.json, the
    records we append to an original's dialog, and the languages' list LOCS in glyphs.kpd."""

    LANGUAGE = {"type": 14, "id": 11, "flags": dialog.FLAG_HOTKEY | 8, "rect": [80, 370, 170, 20], "text": "lLanguage"}

    def appended(self) -> tuple[locale.BaseDialog, bytes]:
        pack = self.world.pack(LOCALE, "ui.kpd")
        return locale.read_dialogs(pack.section("DLGS").payload)[0], pack.section("POOL").payload

    def test_an_appended_record_takes_its_text_from_our_file(self):
        self.world.extend = {"rez\\x.bin": [self.LANGUAGE]}
        self.world.own = {"rez\\x.bin": {b"lLanguage": "zЯзык".encode()}}
        report = self.world.run()[0]
        ours, pool = self.appended()
        self.assertEqual(len(ours.widgets), 4, "the language's 3 records and ours")
        record = ours.widgets[3]
        self.assertEqual(locale.pool_text(pool, record[locale.WIDGET_TEXT_AT], record[locale.WIDGET_TEXT_LEN]),
                         "zЯзык".encode())
        self.assertEqual(report["dialogs"]["translated"], 2, "the language's button and ours")

    def test_an_appended_record_with_no_text_of_ours_stays_the_bases(self):
        self.world.extend = {"rez\\x.bin": [self.LANGUAGE]}
        self.world.run()
        ours, pool = self.appended()
        record = ours.widgets[3]
        self.assertEqual(locale.pool_text(pool, record[locale.WIDGET_TEXT_AT], record[locale.WIDGET_TEXT_LEN]),
                         b"lLanguage")

    def test_our_hotkey_must_be_ascii(self):
        self.world.extend = {"rez\\x.bin": [self.LANGUAGE]}
        self.world.own = {"rez\\x.bin": {b"lLanguage": "ЯЯзык".encode()}}
        self.assert_refused("record 3: the hotkey of")

    def test_our_text_is_checked_as_the_languages(self):
        self.world.extend = {"rez\\x.bin": [self.LANGUAGE]}
        self.world.own = {"rez\\x.bin": {b"lLanguage": "z%d".encode()}}
        self.assert_refused("conversions")

    def test_a_text_of_ours_no_record_has(self):
        self.world.own = {"rez\\x.bin": {b"Nothing": b"x"}}
        self.assert_refused("no record of ours has the text 'Nothing'")

    def test_a_dialog_of_ours_that_is_not_there(self):
        self.world.own = {"koprulu\\none.bin": {b"a": b"b"}}
        self.assert_refused("no dialog koprulu\\none.bin")

    def test_glyphs_list_the_languages_by_their_own_names(self):
        report = self.world.run()[0]
        payload = parse((self.world.out / "locale" / "glyphs.kpd").read_bytes()).section("LOCS").payload
        names, at = [], 1
        for _ in range(payload[0]):
            ident = payload[at + 1:at + 1 + payload[at]].decode("ascii")
            at += 1 + payload[at]
            names.append((ident, payload[at + 1:at + 1 + payload[at]].decode("utf-8")))
            at += 1 + payload[at]
        self.assertEqual(at, len(payload))
        self.assertEqual(names, [(row["id"], row["name"]) for row in locale.load_locales()["locales"]])
        self.assertIn(("ruRU", "Русский"), names)
        self.assertGreater(report["glyphs"]["codes"], 0)

    def test_the_repositorys_texts_name_our_records(self):
        defs = kpd.koprulu_defs()
        texts = {name: {w.get("text") for w in widgets} for name, widgets in {**defs["dialogs"], **defs["extend"]}.items()}
        for name, labels in defs["labels"].items():
            texts.setdefault(name, set()).update(labels.values())
        for row in locale.load_locales()["locales"]:
            for name, own in locale.our_texts(row["id"]).items():
                self.assertIn(name, texts, row["id"])
                for base in own:
                    self.assertIn(base.decode("utf-8"), texts[name], (row["id"], name))


class RefusedTest(WorldCase):
    def test_the_base_and_an_unknown_language(self):
        defs = locale.load_locales()
        with self.assertRaises(locale.LocaleError):
            locale.check_locale(defs["base"], defs)
        with self.assertRaises(locale.LocaleError):
            locale.check_locale("xxXX", defs)
        locale.check_locale(LOCALE, defs)

    def test_no_base_packs(self):
        with self.assertRaisesRegex(locale.LocaleError, "run `data` first"):
            locale.generate(self.world.install(), LOCALE, self.world.out, rasterizer=Rasterizer())

    def test_a_language_the_install_lacks(self):
        self.world.write_base()
        with self.assertRaisesRegex(locale.LocaleError, "names no"):
            locale.generate(Storage({}), LOCALE, self.world.out, rasterizer=Rasterizer())

    def test_a_file_of_the_language_the_install_lacks(self):
        whole = self.world.install
        self.world.install = lambda: Storage({k: v for k, v in whole().files.items() if k != REZ + "network.xml"})
        with self.assertRaisesRegex(locale.LocaleError, "names no .*network.xml"):
            self.world.run()

    def test_no_base_missions(self):
        with self.assertRaisesRegex(locale.LocaleError, "run `missions` first"):
            locale.mission_names(self.world.out)

    def test_a_language_not_downloaded(self):
        self.world.missing = (REZ + "stat_txt.xml",)
        with self.assertRaisesRegex(locale.LocaleError, "not in the local storage"):
            self.world.run()


class DumpTest(WorldCase):
    def test_a_languages_packs_read(self):
        self.world.run()
        text = dump((self.world.out / "locale" / LOCALE / "strings.kpd").read_bytes(), "STAT")
        self.assertIn("7 entries, UTF-8", text)
        self.assertIn("Гот", dump((self.world.out / "locale" / LOCALE / "strings.kpd").read_bytes(), "STAT"))
        glyphs = (self.world.out / "locale" / "glyphs.kpd").read_bytes()
        self.assertIn("fonts\\Eurostile.ttf: 7 bytes", dump(glyphs, "VFNT"))
        self.assertRegex(dump(glyphs, "U8  "), r"\d+ glyphs: U\+[0-9A-F]+\.\.U\+[0-9A-F]+")


class TextFormsTest(unittest.TestCase):
    def test_escapes(self):
        for value, want in ((b"\\\\0041", b"\x041"), (b"\\121es", b"yes"), (b"\\032", b" "), (b"\\169", "©".encode()),
                            (b"\\\\n", b"\n"), (b"\\t", b"\t"), ("Жа".encode(), "Жа".encode())):
            self.assertEqual(rebuild.unescape_utf8(value), want, value)

    def test_an_escape_is_a_byte(self):
        self.assertEqual(rebuild.unescape_rc3(b"\\255"), b"\xff")
        for unescape in (rebuild.unescape_rc3, rebuild.unescape_utf8):
            with self.assertRaisesRegex(rebuild.EscapeError, "past a byte"):
                unescape(b"\\256")

    def test_xml_values_undo_the_entities(self):
        self.assertEqual(rebuild.xml_values(xml([("a", b"&lt;&amp;&gt;")])), [("a", b"<&>")])

    def test_the_marks_of_a_span(self):
        self.assertEqual(locale.with_nuls(b"m\0\x03M", b"m\x07\x03X"), b"m\0\x03X", "the base's NUL")
        self.assertEqual(locale.with_nuls(b"a\x07b", b"\xd0\x96\x07"), b"\xd0\x96\x07", "the base's colour")
        self.assertEqual(locale.with_nuls(b"m\0a\x07b", b"m\x07a\x07b"), b"m\0a\x07b", "both in order")
        self.assertEqual(locale.with_nuls(b"plain", b"\x07x"), b"\x07x", "more than the base's")

    def test_a_wide_table(self):
        self.assertEqual(locale.wide_table([b"a", b"", b"bc"]),
                         struct.pack("<4I", 3, 16, 18, 19) + b"a\0" + b"\0" + b"bc\0")

    def test_the_strings_section(self):
        data = locale.str_encode([b"a", None, b"bc"], "m")
        self.assertEqual(locale.str_rows(data), [b"a", None, b"bc"])
        with self.assertRaisesRegex(locale.LocaleError, "16-bit offsets"):
            locale.str_encode([b"x" * locale.STR_LIMIT], "m")

    def test_a_row_past_the_section_has_no_text(self):
        self.assertEqual(locale.str_rows(struct.pack("<3H", 2, 100, 6) + b"a\0"), [None, b"a"])

    def test_a_buttons_form(self):
        self.assertTrue(locale.button_form(b"plain text", "Жа".encode()), "no button")
        self.assertTrue(locale.button_form(b"m\0\x03Move", b"m\0\x03X"))
        self.assertFalse(locale.button_form(b"m\0\x03Move", "Жа".encode()))
        self.assertFalse(locale.button_form(b"m\0\x03Move", b"m"))
        self.assertFalse(locale.button_form(b"q\x01Quit", b"q\x02X"), "another mode")

    def test_the_rows_kept(self):
        trig = chk_trigger([(WAV_ACTION, 2)]) + next_scenario_trigger(3)
        mbrf = chk_trigger([(sorted(kpm.MBRF_WAV_ACTIONS)[0], 7)])
        self.assertEqual(locale.kept_string_ids({"TRIG": trig, "MBRF": mbrf}), {2, 3, 7})
        self.assertEqual(locale.kept_string_ids({"TRIG": next_scenario_trigger(0), "MBRF": b""}), set())

    def test_the_glyph_set_leaves_out_slots_and_controls(self):
        codes = locale.glyph_set({ord("Ж"): "a", ord("A"): "b", 0x2019: "c", 0x4e00: "d"}, None)
        self.assertIn(0x4e00, codes)
        self.assertNotIn(ord("A"), codes)
        self.assertNotIn(0x2019, codes)
        self.assertNotIn(0x98, codes, "cp1251's undefined byte is no character")
        self.assertEqual(codes, sorted(set(codes)))

    def test_slots_are_the_engines(self):
        slots = locale.slots()
        self.assertEqual(slots[ord("a")], ord("a"))
        self.assertEqual(slots[0x2019], 0x92)
        self.assertEqual(slots[0xa9], 0xa9)

    def test_the_rasterizer_needs_the_tool(self):
        with mock.patch.dict("os.environ", {locale.FNT_MAKE_ENV: "/nonexistent/kp_fnt_make"}):
            with self.assertRaisesRegex(locale.LocaleError, "build the kp_fnt_make target"):
                locale.rasterize(b"", b"", [1])


if __name__ == "__main__":
    unittest.main()
