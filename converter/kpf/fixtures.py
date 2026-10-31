"""Cross-language fixtures: containers written here are read by the C loaders.

Run `python3 -m converter.kpf.fixtures [out_dir]`; the default output is
tests/fixtures/generated/kpf (gitignored). The C suites (tests/c: test_data, test_ui_data) read the files and
expect.json, the values the loaders must give.
"""
from __future__ import annotations

import hashlib
import logging
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

from . import container as kpf
from ..emit import cinematics
from ..formats import dialog, grp, kpv
from . import kpd, kpm

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_OUT = ROOT / "tests" / "fixtures" / "generated" / "kpf"

INTS = [0, 1, 127, 128, 255, 256, 32767, 32768, 65535, 65536, 2**31 - 1, 2**31, 2**32 - 1]
LARGE_SIZE = 8 * 1024 * 1024  # multi-megabyte custom resource for the loading spike

log = logging.getLogger(__name__)

# Minimal CHK payloads accepted by the version 59 header, briefing and UMS tables.
V59 = {
    "VER ": struct.pack("<H", 59), "DIM ": struct.pack("<HH", 64, 64), "ERA ": struct.pack("<H", 0),
    "OWNR": bytes(12), "SIDE": bytes(12), "STR ": b"\x01\x00\x04\x00\x00", "SPRP": bytes(4), "FORC": bytes(20),
    "VCOD": b"vcod", "MBRF": b"", "MTXM": bytes(128), "THG2": b"", "MASK": b"", "UNIS": bytes(0xFD0),
    "UPGS": bytes(0x256), "TECS": bytes(0xD8), "PUNI": bytes(0x1644), "UPGR": bytes(0x6D4), "PTEC": bytes(0x390),
    "UNIT": b"", "UPRP": bytes(0x500), "MRGN": bytes(0x500), "TRIG": b"",
}
MISSION_STRINGS = (b"name", b"a.wav", b"b.wav", b"\x03ctl\x01")


def build_chk(sections: dict[str, bytes] | list[tuple[str, bytes]]) -> bytes:
    items = sections.items() if isinstance(sections, dict) else sections
    return b"".join(t.encode("latin1") + struct.pack("<I", len(p)) + p for t, p in items)


def chk_strings(*strings: bytes) -> bytes:
    """STR/TBL layout: u16 count, u16 offsets, NUL-terminated blob."""
    offsets, blob = [], b""
    base = 2 + 2 * len(strings)
    for s in strings:
        offsets.append(base + len(blob))
        blob += s + b"\0"
    return struct.pack(f"<H{len(strings)}H", len(strings), *offsets) + blob


def chk_trigger(actions: list[tuple[int, int]]) -> bytes:
    """One 2400-byte TRIG/MBRF record with (action id, WAV string) actions."""
    rec = bytearray(kpm.TRIGGER_SIZE)
    for i, (action_id, wav) in enumerate(actions):
        act = kpm.ACTIONS_OFFSET + i * kpm.ACTION_SIZE
        struct.pack_into("<I", rec, act + kpm.ACTION_WAV, wav)
        rec[act + kpm.ACTION_ID] = action_id
    return bytes(rec)


def iscript_bin(entries: list[tuple[int, int]]) -> bytes:
    """iscript.bin with one SCPE header per distinct header index; entries are (id, header index)."""
    count = max(h for _, h in entries) + 1
    headers = b"".join(b"SCPE" + bytes([0, 0, 0, 0]) + struct.pack("<2H", 0, 0) for _ in range(count))
    index_offset = 4 + len(headers)
    index = b"".join(struct.pack("<HH", i, 4 + 12 * h) for i, h in entries) + struct.pack("<HH", 0xFFFF, 0)
    return struct.pack("<I", index_offset) + headers + index


def ai_bin(records: list[tuple[bytes, int]], stride: int, code_size: int = 64) -> bytes:
    """AI binary with `code_size` bytes of code and a directory of (id, offset) records."""
    directory = b"".join(sid + struct.pack("<I", off) + bytes(stride - 8) for sid, off in records) + bytes(4)
    return struct.pack("<I", 4 + code_size) + bytes(code_size) + directory


AI_RECORDS = [(b"Ter5", 16), (b"Redi", 0), (b"Both", 20)]
BW_RECORDS = [(b"Redi", 24), (b"BwOn", 28), (b"Both", 32), (b"Zero", 0)]


def dialog_record(x1=0, y1=0, **fields) -> bytes:
    """A synthetic record: 10 x 10 at x1, y1 with x2/y2 one past it (as the fixtures were made)."""
    return dialog.record(x1=x1, y1=y1, x2=x1 + 10, y2=y1 + 10, **fields)


def dialog_bin() -> bytes:
    """Synthetic BIN: root container at 0 -> button (hotkey text) at 86 -> type-14 widget at 172 with one SMK."""
    root = dialog_record(kind=0, ident=0, child_or_smk=86)
    button = dialog_record(nxt=172, kind=1, ident=7, flags=0x200, string=300)
    html = dialog_record(kind=14, ident=8, x1=100, y1=50, child_or_smk=258, extra=(1, 2, 3, 4))
    smk = dialog.SMK.pack(0, 3, 9, 310, 0, 5, 6, 0, 0)
    data = bytearray(root + button + html + smk)
    data += bytes(300 - len(data)) + b"Quit\0" + b"\0" * 5 + b"port.smk\0"
    return bytes(data)


def portraits_fixture(source_hash: bytes) -> bytes:
    """portraits.kpd: row 0 idle variants 0 and 2 (SMKs 0, 1), talking variant 1 (SMK 2)."""
    from .. import CONVERTER_VERSION

    c = kpf.Container(kpd.PORTRAITS_KIND, kpd.PORTRAITS_SCHEMA, CONVERTER_VERSION, "bw-1.17.0.1",
                      source_hash=source_hash)
    c.add("ROWS", struct.pack("<H4H4H", 2, 0, 0b101, 2, 0b10, kpd.NONE16, 0, kpd.NONE16, 0))
    smks = bytearray(struct.pack("<H", 3))
    for i, path in enumerate((b"portrait\\a0.smk", b"portrait\\a2.smk", b"portrait\\t1.smk")):
        smks += bytes([len(path)]) + path + struct.pack("<HHHHBI", 60, 56, 2, 2, 0, 100_000) + struct.pack("<2H", 0, i % 2)
    c.add("SMKS", bytes(smks))
    c.add("PALS", struct.pack("<H", 2) + bytes(768) + bytes([1]) * 768)
    return c.to_bytes()


def sounds_fixture(source_hash: bytes) -> bytes:
    """sounds.kpd: id 0 absent, id 1 a 22,050 Hz mono PCM16 sound of 44,100 bytes, id 2 a
    sound whose duration product wraps u32, id 3 a file the native parser rejects."""
    from .. import CONVERTER_VERSION

    records = [kpd.sound_record(None), kpd.SOUND_RECORD.pack(3, 1, 1, 22050, 44100, 2, 16, 44100),
               kpd.SOUND_RECORD.pack(3, 1, 1, 22050, 44100, 2, 16, 5_000_000),
               kpd.sound_record(b"RIFF\0\0\0\0WAVEdata\0\0\0\0")]
    c = kpf.Container(kpd.SOUNDS_KIND, kpd.SOUNDS_SCHEMA, CONVERTER_VERSION, "bw-1.17.0.1", source_hash=source_hash)
    c.add("SFX ", struct.pack("<H", len(records)) + b"".join(records))
    c.add("PACK", struct.pack("<H4B", len(records), 0, 2, 2, 1))
    c.add("MUSL", struct.pack("<BII", 1, 441000, 44100))  # one music track of ten seconds
    return c.to_bytes()


def raw_grp(width: int, height: int, frames: list[tuple[int, int, int, list[bytes]]]) -> bytes:
    """GRP bytes from hand-encoded rows: frames are (x, y, frame width, packet rows)."""
    header = bytearray(struct.pack("<3H", len(frames), width, height))
    payload = bytearray()
    base = grp.HEADER.size + grp.FRAME.size * len(frames)
    for x, y, w, rows in frames:
        header += grp.FRAME.pack(x, y, w, len(rows), base + len(payload))
        offset = 2 * len(rows)
        for r in rows:
            payload += struct.pack("<H", offset)
            offset += len(r)
        payload += b"".join(rows)
    return bytes(header + payload)


GRPMETA_NAMES = ["terran\\fix.grp", "terran\\fix.lol", "thingy\\fx.grp", "Terran\\FIX.grp", "unused.grp"]


def grpmeta_fixture() -> tuple[kpd.GrpSet, dict[str, bytes]]:
    """A GrpSet over synthetic files: a Terran GRP named twice (case differs), an effects GRP
    with an opaque index 0, and one LO file. Frame 1 of the Terran GRP covers the run
    canonicalisation cases: leading empty packets, adjacent literal/repeat packets, an empty
    packet mid-row, a merged transparent run longer than 127, and overshooting last packets."""
    import random

    rng = random.Random(17)
    w, h = 23, 9
    mask = bytes(rng.choice((0, 255, 255)) for _ in range(w * h))
    indices = bytes(rng.randrange(1, 4) if m else 0 for m in mask)
    first = grp.encode(40, 30, [grp.Frame(3, 4, w, h, indices, mask)])
    frame0 = grp.decode(first).frames[0]
    rows0 = [grp._encode_row(frame0.indices[r * w:(r + 1) * w], frame0.mask[r * w:(r + 1) * w]) for r in range(h)]
    rows1 = [  # frame width 200; the decoder rejects empty opaque packets, so only transparent ones are empty
        # leading empty transparent, literal 2, repeat 3, transparent 5 + 127 + 1, empty transparent, literal 3, transparent 59
        bytes([0x80, 0x02, 9, 9, 0x43, 5, 0x85, 0xFF, 0x81, 0x80, 0x03, 1, 2, 3, 0xBB]),
        # leading empty transparent, transparent 3, repeat 2, transparent 112, repeat 63, transparent 20
        bytes([0x80, 0x83, 0x42, 7, 0xF0, 0x7F, 6, 0x94]),
        # transparent 254: overshoots the width
        bytes([0xFF, 0xFF]),
        # literal 1, empty transparent, literal 1, transparent 127 + 2, literal 63, literal 8 (overshoots)
        bytes([0x01, 4, 0x80, 0x01, 5, 0xFF, 0x82, 0x3F]) + bytes(range(1, 64)) + bytes([0x08]) + bytes([1]) * 8,
    ]
    terran = raw_grp(40, 30, [(3, 4, w, rows0), (0, 1, 200, rows1), (5, 5, 0, [])])
    effects = raw_grp(8, 8, [(1, 1, 4, [bytes([0x42, 0, 0x82]), bytes([0x81, 0x01, 7, 0x82])])])
    lo_file = struct.pack("<II", 2, 2) + struct.pack("<II", 16, 20) + struct.pack("<4b", -3, 4, 127, -128) + \
        struct.pack("<4b", 0, -1, 1, 2)
    files = {"unit\\terran\\fix.grp": terran, "unit\\Terran\\FIX.grp": terran, "unit\\thingy\\fx.grp": effects,
             "unit\\terran\\fix.lol": lo_file}
    gs = kpd.grp_set(len(GRPMETA_NAMES), GRPMETA_NAMES, {1, 3, 4}, {2}, files.__getitem__)
    return gs, files


def grpmeta_expect(gs: kpd.GrpSet) -> list[str]:
    """Python `0x40fd50` over every frame pixel plus a one-pixel border, as strings of
    '0'/'1' per frame (row-major, x and y from -1 to the frame size)."""
    rows = []
    for e in gs.grps:
        for f in range(len(e.grp.frames)):
            _, _, w, h, _ = grp.frame_record(e.data, f)
            hits = "".join(str(grp.pixel_opaque(e.data, f, x, y)) for y in range(-1, h + 1) for x in range(-1, w + 1))
            rows.append(hits)
    return rows


def ui_fixture(source_hash: bytes) -> bytes:
    """ui.kpd holding the synthetic dialog of converter/tests/test_ui.py."""
    from .. import CONVERTER_VERSION
    dlgs, pool = kpd.dialogs_section({"rez\\x.bin": dialog_bin()})
    c = kpf.Container(kpd.UI_KIND, kpd.UI_SCHEMA, CONVERTER_VERSION, "bw-1.17.0.1", source_hash=source_hash)
    c.add("DLGS", dlgs)
    c.add("POOL", pool)
    c.add("TFNT", bytes(range(48)) * 12)
    c.add("TEFF", struct.pack("<H", 2) + bytes(24))
    header = struct.pack("<3H", 1, 8, 9) + struct.pack("<4BI", 1, 2, 3, 4, 14)
    c.add("DGRP", struct.pack("<H", 1) + bytes([5]) + b"a.grp" + struct.pack("<I", len(header)) + header)
    c.add("PCXS", struct.pack("<H", 0))
    for name, payload in kpd.ui_table_sections(UI_TABLES):
        c.add(name, payload)
    glue = b"glue\\x.pcx"
    c.add("GFIL", struct.pack("<H", 1) + bytes([len(glue)]) + glue + struct.pack("<I", 3) + b"abc")
    return c.to_bytes()


# Synthetic interface tables: two button sets (set 1 linked to set 0), one status layout, the fluff of two races, one establishing and one finale text
UI_TABLES = {
    "button_sets": [{"link": 0xFFFF, "buttons": [[1, 228, "0x49f410", "0x47c520", 0, 0, 664, 0]]},
                    {"link": 0, "buttons": [[7, 252, "0x49f850", "0x47c420", 10, 10, 344, 355],
                                            [9, 311, "0x49fae0", "0x47c530", 0, 0, 685, 761]]}],
    "status_layouts": [[0, "0x4a55e0", "0x4a4e40"]],
    "fluff": [{"rects": [[0, 289, 29, 26]]}, {"rects": [[0, 293, 23, 22], [138, 315, 28, 52]]}],
    "cursors": ["arrow.grp", "Illegal.grp"],
    "edge_scroll": [18, 11, 12, 17, 0, 13, 16, 15, 14],
    "rects": {"command_card": [496, 354, 639, 479], "minimap": [6, 348, 133, 475]},
    "music": [["", 0, 0, 0], ["music\\zerg1.wav", 2, 0, 2]],
    "attenuation": [-4000, -3396, -20],
    "accelerators": [{"module": "Local.dll", "id": 101, "entries": [[2, 43, 40056]]},
                     {"module": "StarCraft.exe", "id": 113, "entries": [[11, 48, 39910], [19, 49, 39921]]}],
    "establishing": [["EstT0t", 1]],
    "finale": [["FinZ09bx", 63]],
}


def fonts_fixture(source_hash: bytes) -> bytes:
    """fonts.kpd with one tiny synthetic font in every slot: codes 32..34, glyph 33 is 2x1,
    glyph 34 is 3x2; 32 (space) has no glyph."""
    from .. import CONVERTER_VERSION

    header = b"FONT" + bytes((32, 34, 3, 2)) + struct.pack("<3I", 0, 20, 26)
    data = header + bytes((2, 1, 0, 1)) + bytes((0x00, 0x01)) + bytes((3, 2, 1, 0)) + bytes((0x0A,))
    c = kpf.Container(kpd.FONTS_KIND, kpd.FONTS_SCHEMA, CONVERTER_VERSION, "bw-1.17.0.1", source_hash=source_hash)
    for f in kpd.font_defs()["fonts"]:
        c.add(f["section"], data + b"\0")
    c.add("TFGM", bytes(range(192)))
    c.add("TFNT", bytes(range(40)))
    return c.to_bytes()


def palettes_fixture(source_hash: bytes) -> bytes:
    """palettes.kpd from synthetic PCXs: pixel value = (row * 7 + column + salt) % 256."""
    from ..formats import pcx

    sizes = {"ofire": 63, "gfire": 32, "bfire": 40, "bexpl": 63, "trans50": 255, "red": 1, "green": 1,
             "dark": 32, "shift": 1}
    widths = {"game\\tunit.pcx": 128, "game\\tminimap.pcx": 16, "game\\tselect.pcx": 24, "game\\thpbar.pcx": 19}

    def image(w: int, h: int, salt: int) -> bytes:
        return pcx.encode(w, h, bytes((r * 7 + c + salt) % 256 for r in range(h) for c in range(w)))

    def read(path: str) -> bytes:
        if path in widths:
            return image(widths[path], 1, len(path))
        tileset, name = path.split("\\")[1], path.rsplit("\\", 1)[1][:-4]
        return image(256, sizes[name], len(tileset) + len(name))

    return kpd.build_palettes(read, profile_id="bw-1.17.0.1", data_set_hash=source_hash).to_bytes()


def fixture_tileset() -> tuple[bytes, bytes, bytes, bytes]:
    """Synthetic (CV5, VX4, VR4, VF4): 70 groups over 40 megatiles, covering the mission
    fixture's tiles (groups 62-64)."""
    import random

    from ..formats import tileset

    rng = random.Random(1997)
    megatiles, groups = 40, 70
    cv5 = bytearray()
    for _ in range(groups):
        cv5 += bytes([0, 0]) + struct.pack("<H", rng.randrange(65536)) + bytes(16)
        cv5 += struct.pack("<16H", *(rng.randrange(megatiles) for _ in range(16)))
    vf4 = struct.pack(f"<{16 * megatiles}H", *(rng.choice((0, 1, 2, 3, 4, 5, 8, 0x0F, 0xFF)) for _ in range(16 * megatiles)))
    return bytes(cv5), bytes(tileset.VX4_RECORD * megatiles), bytes(tileset.VR4_RECORD), vf4


def creep_edge_grp() -> bytes:
    """A tileset GRP of two creep edges: a full tile of index 5 and a 2 x 3 frame in the tile's bottom-right corner."""
    from ..formats import grp

    o, t = grp.OPAQUE, grp.TRANSPARENT
    full = grp.Frame(0, 0, 32, 32, bytes([5]) * 1024, bytes([o]) * 1024)
    corner = grp.Frame(30, 29, 2, 3, bytes([7, 0, 0, 0, 9, 0]), bytes([o, t, t, t, o, t]))
    return grp.encode(32, 32, [full, corner])


def tileset_fixture(source_hash: bytes) -> tuple[bytes, list[int], list[int]]:
    """The synthetic tileset container, plus the Python terrain import of the mission fixture
    for the C suites."""
    from ..derive import terrain

    cv5, vx4, vr4, vf4 = fixture_tileset()
    wpe = bytes((i * 3) % 256 if i % 4 != 3 else 0 for i in range(1024))
    files = {"cv5": cv5, "vx4": vx4, "vr4": vr4, "vf4": vf4, "wpe": wpe, "grp": creep_edge_grp()}
    data = kpd.build_tileset("badlands", files, profile_id="bw-1.17.0.1", data_set_hash=source_hash).to_bytes()
    mtxm = struct.pack("<32H", *MISSION_TILES)
    ids, flags = terrain.import_terrain(mtxm, 8, 4, cv5, terrain.megatile_flags(vf4))
    return data, ids, flags


MISSION_TILES = (*range(1000, 1031), 1000 | 0x8000)


def rules_fixture() -> tuple[bytes, list[tuple[int, int, int]], dict[str, int]]:
    """A RULE payload covering every override tag with mixed default flags, synthetic unit
    associations, and per-category checksums of the Python effects for the C suites."""
    import random

    from ..derive import rules

    rng = random.Random(64)
    assoc = [(rng.choice((rules.NO_SUBUNIT, rng.randrange(228))), rng.choice((rules.NO_WEAPON, rng.randrange(100))),
              rng.choice((rules.NO_WEAPON, rng.randrange(100)))) for _ in range(228)]
    payload = bytearray()
    sections = []
    for tag in ("UNIS", "UPGS", "TECS", "PUNI", "UPGR", "PTEC", "UNIx", "UPGx", "TECx", "PUPx", "PTEx"):
        count, _, length = rules.LAYOUT[tag]
        raw = bytearray(rng.randrange(256) for _ in range(length))
        if tag in ("UPGR", "PUPx", "PTEC", "PTEx"):
            raw[26 * count :] = bytes(rng.choice((0, 0, 1)) for _ in range(12 * count))
        elif tag != "PUNI":
            raw[:count] = bytes(rng.choice((0, 1, 1)) for _ in range(count))
        sections.append((tag, bytes(raw)))
        payload += tag.encode() + struct.pack("<I", length) + raw
    e = rules.effects_of(sections, assoc, expansion=False)

    def total(d: dict) -> int:
        return sum((hash_key(k) * 31 + v) % 1_000_003 for k, v in d.items()) % 1_000_003

    def hash_key(k) -> int:
        return k if isinstance(k, int) else sum((i + 1) * 997 ** n for n, i in enumerate(k))

    sums = {f"unit_{n}": total(e.units[n]) for n, _ in rules.UNIT_FIELDS}
    sums |= {f"upgrade_{n}": total(e.upgrades[n]) for n in rules.UPGRADE_FIELDS}
    sums |= {f"tech_{n}": total(e.techs[n]) for n in rules.TECH_FIELDS}
    sums |= {"weapon_damage": total(e.weapon_damage), "weapon_bonus": total(e.weapon_bonus),
             "availability": total(e.availability), "upgrade_levels": total(e.upgrade_levels),
             "tech_states": total(e.tech_states), "upgrade_level_count": len(e.upgrade_levels)}
    return bytes(payload), assoc, sums


def unit_records_fixture() -> tuple[bytes, list[list]]:
    """UNIT records covering every property branch, and the Python results as rows
    [max hp, max shields, hero, container, health, shields, energy, status, resources, start x, start y, camera x,
    camera y]."""
    import random

    from ..derive import unit_records as ur

    rng = random.Random(36)
    payload, rows = bytearray(), []
    for i in range(48):
        rec = ur.UnitRecord(i, rng.randrange(8192), rng.randrange(8192), ur.START_LOCATION if i % 7 == 0 else rng.randrange(228),
                            0, rng.choice((0, 0x10, 0x1F)), rng.randrange(64), rng.randrange(12), rng.choice((0, 1, 50, 100, 101, 255)),
                            rng.choice((0, 50, 100, 128, 255)), rng.choice((0, 50, 100, 128, 255)),
                            rng.choice((0, 249, 65536, 0xFFFFFFFF)), 0, rng.choice((0, 0x10)), 0, 0)
        payload += ur.RECORD.pack(*rec.__dict__.values())
        max_hp, max_sh = rng.choice((256, 10240, 0x7FFFFF00)), rng.choice((0, 1, 100))
        hero, container = i % 3 == 0, i % 2 == 0
        a = ur.apply_properties(rec.valid_flags, rec.hp_percent, rec.shield_percent, rec.energy_percent, rec.states,
                                rec.resources, max_hp_fixed=max_hp, max_shields=max_sh,
                                max_energy=ur.HERO_ENERGY if hero else ur.NORMAL_ENERGY, resource_container=container,
                                current=ur.Applied(max_hp, max_sh << 8, 25600, 0, 42330))
        start, camera = ur.start_location(rec, local_player=3)
        sx, sy = start or (-1, -1)
        cx, cy = camera or (-1, -1)
        rows.append([max_hp, max_sh, hero, container, a.health_fixed, a.shields_fixed, a.energy_fixed, a.status,
                     a.resources, sx, sy, cx, cy])
    return bytes(payload), rows


def dat_fixture(source_hash: bytes) -> tuple[bytes, list[tuple[str, str, int, int, int]]]:
    """dat.kpd with every native layout filled by a byte counter, plus probes
    (table, column, id, element, expected value) for the C suites."""
    from ..formats import dat

    layouts = dat.load_layouts()
    files = {f.path: bytes((i * 7 + len(f.path)) % 256 for i in range(f.size)) for f in layouts}
    data = kpd.build_dat(files, layouts, profile_id="bw-1.17.0.1", data_set_hash=source_hash).to_bytes()
    probes = []
    for f in layouts:
        for c in f.columns:
            last = c.first_id + (c.count - 1) // c.per_id
            for record, element in ((c.first_id, 0), (last, c.per_id - 1)):
                probes.append((f.table, c.name, record, element, dat.value(f, files[f.path], c.name, record, element)))
    return data, probes


def mission_derived(data: bytes) -> dict:
    """Python decoding of the mission fixture's derived sections, for the suites."""
    from ..derive import contours, regions

    c = kpf.parse(data)
    g = regions.decode(c.section("RGNS").payload)
    k = contours.decode(c.section("CONT").payload)
    last = g.count - 1
    first_contour = next((list(a[0]) for a in k.arrays if a), [0, 0, 0, 0, 0])
    return {"walk_size": len(c.section("WALK").payload), "regions": g.count, "splits": len(g.splits),
            "last_class": g.classes[last], "last_group": g.groups[last], "last_tiles": g.tiles[last],
            "last_neighbors": list(g.neighbors[last]), "last_center_x": g.centers[last][0],
            "cell_3_2": g.raster[2 * g.width + 3], "contour_counts": list(k.counts()), "capacities": list(k.capacities),
            "first_contour": list(first_contour)}


def fixture_wav(length: int) -> bytes:
    """Mono 22,050 Hz PCM16 RIFF file with `length` data bytes."""
    fmt = struct.pack("<HHIIHH", 1, 1, 22050, 44100, 2, 16)
    body = b"WAVE" + b"fmt " + struct.pack("<I", 16) + fmt + b"data" + struct.pack("<I", length) + bytes(length)
    return b"RIFF" + struct.pack("<I", len(body)) + body


def mission_fixture(source_hash: bytes) -> bytes:
    """8x4 version 205 mission with colors, forces, overrides and two WAV strings (one missing)."""
    from ..formats import chk

    sections = {k: v for k, v in V59.items() if k not in ("UNIS", "UPGS", "TECS", "UPGR", "PTEC")}
    sections.update({
        "VER ": struct.pack("<H", 205), "DIM ": struct.pack("<HH", 8, 4), "ERA ": struct.pack("<H", 13),
        "OWNR": bytes(range(12)), "SIDE": bytes(range(20, 32)), "SPRP": struct.pack("<HH", 1, 4),
        "FORC": bytes(range(1, 21)), "MTXM": struct.pack("<32H", *MISSION_TILES), "MASK": bytes(range(32)),
        "STR ": chk_strings(*MISSION_STRINGS), "TRIG": chk_trigger([(8, 2), (7, 3)]), "MBRF": chk_trigger([(2, 2)]),
        "UNIx": bytes(0x1048), "UPGx": bytes(0x31A), "TECx": bytes(0x18C), "PUPx": bytes(0x90E),
        "PTEx": bytes(0x688), "MRGN": bytes(0x13EC), "COLR": bytes([7, 6, 5, 4, 3, 2, 1, 0]),
    })
    scenario = chk.load(build_chk(sections), chk.LoaderTables.load(), vcod_sha256=hashlib.sha256(b"vcod").hexdigest())
    sounds = kpm.mission_sounds(scenario, "campaign\\fixture", lambda p: fixture_wav(4410) if p.endswith("a.wav") else None)
    cv5, _, _, vf4 = fixture_tileset()
    c = kpm.build(scenario, sounds, profile_id="bw-1.17.0.1", data_set_hash=source_hash, tileset=(cv5, vf4))
    kpm.add_packs(c, ["terran", "effects"], b"fixture")
    return c.to_bytes()


def _large_payload() -> bytes:
    # Deterministic, poorly compressible bytes (SHA-256 counter stream).
    out = bytearray()
    counter = 0
    while len(out) < LARGE_SIZE:
        out += hashlib.sha256(counter.to_bytes(8, "little")).digest()
        counter += 1
    return bytes(out[:LARGE_SIZE])


def _text_value(value: object) -> object:
    """expect's data for JSON: byte strings as text of one character per byte."""
    if isinstance(value, bytes):
        return value.decode("latin-1")
    if isinstance(value, (list, tuple)):
        return [_text_value(v) for v in value]
    if isinstance(value, dict):
        return {k: _text_value(v) for k, v in value.items()}
    return value


CINE_FRAMES = 12  # the cinematics fixture's movies: 64 x 48, 15 fps, a key frame every 5


def cine_webm(source: str, pix_fmt: str, duration: float = 0.8, gop: int = 5) -> bytes | None:
    """A 64 x 48, 15 fps VP9 + Vorbis WebM of FFmpeg's test source in a pixel format (gbrp: profile 1 4:4:4 sRGB as
    the SD movies, yuv420p: profile 0 as the HD2 ones), a key frame every `gop`, or None where FFmpeg lacks the
    encoders."""
    if shutil.which("ffmpeg") is None or shutil.which("ffprobe") is None:
        return None
    with tempfile.TemporaryDirectory() as tmp:
        out = Path(tmp) / "m.webm"
        run = subprocess.run(["ffmpeg", "-v", "error", "-y", "-f", "lavfi", "-i",
                              f"{source}=size=64x48:rate=15:duration={duration}", "-f", "lavfi", "-i",
                              f"sine=frequency=440:duration={duration}:sample_rate=22050", "-c:v", "libvpx-vp9",
                              "-pix_fmt", pix_fmt, "-g", str(gop), "-c:a", "vorbis", "-strict", "-2", "-ac", "2",
                              "-shortest", str(out)], capture_output=True, check=False)
        return out.read_bytes() if run.returncode == 0 and out.exists() else None


def cine_rgba(webm: bytes) -> bytes:
    """The WebM's frames as FFmpeg decodes them, RGBA one after the other."""
    with tempfile.TemporaryDirectory() as tmp:
        src = Path(tmp) / "m.webm"
        src.write_bytes(webm)
        run = subprocess.run(["ffmpeg", "-v", "error", "-i", str(src), "-an", "-f", "rawvideo", "-pix_fmt", "rgba", "-"],
                             capture_output=True, check=True)
        return run.stdout


class _CineStorage:
    """The cinematics emitter's storage over two movies: `blizzard` (cinematic 0) and `starintr` (1)."""

    def __init__(self, movies: dict[str, bytes]):
        self.movies = {f"SD/smk/{n}.webm".lower(): d for n, d in movies.items()}

    def has(self, name: str) -> bool:
        return name.lower() in self.movies

    def read(self, name: str) -> bytes:
        return self.movies[name.lower()]


def cine_fixture(source_hash: bytes) -> dict[str, bytes]:
    """cine.kpd (cinematic 0: gbrp, 1: yuv420p) and the movies' FFmpeg-decoded frames, cine0.rgba and cine1.rgba; empty
    where FFmpeg cannot encode them (the C suite then skips)."""
    srgb, ycc = cine_webm("testsrc", "gbrp"), cine_webm("gradients", "yuv420p")
    if srgb is None or ycc is None:
        log.warning("cinematics fixtures: ffmpeg with libvpx-vp9 and vorbis is not available")
        return {}
    with tempfile.TemporaryDirectory() as tmp:
        info = cinematics.generate(_CineStorage({"blizzard": srgb, "starintr": ycc}), Path(tmp), "sd", "bw-1.17.0.1",
                                   source_hash)
        return {"cine.kpd": Path(info["path"]).read_bytes(), "cine0.rgba": cine_rgba(srgb), "cine1.rgba": cine_rgba(ycc)}


def build_fixtures(out: Path = DEFAULT_OUT) -> list[Path]:
    out.mkdir(parents=True, exist_ok=True)
    source_hash = hashlib.sha256(b"fixture source").digest()
    basic = kpf.Container(b"TEST", schema_version=3, converter_version=7, profile_id="bw-1.17.0.1",
                          source_hash=source_hash, provenance_hash=hashlib.sha256(b"provenance").digest())
    basic.add("INTS", struct.pack(f"<{len(INTS)}I", *INTS))
    basic.add("BYTE", bytes(range(256)), schema_version=2)
    basic.add("ODD ", b"abc")
    basic.add("EMPT", b"")
    basic.add_derived("WALK", bytes([0x80, 1, 2, 4, 8]) * 7, algorithm_id="WLK1", algorithm_version=5,
                      inputs_hash=hashlib.sha256(b"walk inputs").digest(), flags=0x8001)
    large = kpf.Container(b"TEST", schema_version=3, converter_version=7, profile_id="bw-1.17.0.1",
                          source_hash=source_hash)
    large_payload = _large_payload()
    large.add("BLOB", large_payload)

    dat_kpd, dat_probes = dat_fixture(source_hash)
    tileset_kpd, terrain_ids, terrain_flags = tileset_fixture(source_hash)
    rule_payload, rule_assoc, rule_sums = rules_fixture()
    unit_payload, unit_rows = unit_records_fixture()
    (out / "units.bin").write_bytes(unit_payload)
    (out / "rules.bin").write_bytes(rule_payload)
    files = {"basic.kpd": basic.to_bytes(), "large.kpd": large.to_bytes(), "mission.kpm": mission_fixture(source_hash),
             "dat.kpd": dat_kpd, "tileset.kpd": tileset_kpd, "palettes.kpd": palettes_fixture(source_hash),
             "fonts.kpd": fonts_fixture(source_hash), "ui.kpd": ui_fixture(source_hash),
             "sounds.kpd": sounds_fixture(source_hash), "portraits.kpd": portraits_fixture(source_hash),
             "iscript.kpd": kpd.build_iscript(iscript_bin([(5, 0), (3, 1), (5, 1)]), {0: 2, 12: 14},
                                              profile_id="bw-1.17.0.1", data_set_hash=source_hash).to_bytes(),
             "aiscript.kpd": kpd.build_aiscript(ai_bin(AI_RECORDS, 16), ai_bin(BW_RECORDS, 8),
                                                profile_id="bw-1.17.0.1", data_set_hash=source_hash).to_bytes()}
    # KPV1 (formats/kpv.py), read by test_ui_data.kpv_movie_plays_as_an_smk: a 2 x 2 ring movie, the same frames as
    # the test's own bytes
    files["movie.kpv"] = kpv.encode(2, 2, 100, bytes(range(256)) * 3, [b"\1\2\3\4", b"\x09" * 4, b"\5\6\7\x08"],
                                    ring=True)
    files.update(cine_fixture(source_hash))
    grpmeta_set, _ = grpmeta_fixture()
    files["grpmeta.kpd"] = kpd.build_grpmeta(grpmeta_set, profile_id="bw-1.17.0.1", data_set_hash=source_hash).to_bytes()
    written = []
    for name, data in files.items():
        path = out / name
        path.write_bytes(data)
        written.append(path)

    expect = {
        "source_hash": source_hash, "ints": INTS, "basic_size": len(files["basic.kpd"]),
        "large_size": len(files["large.kpd"]), "large_payload_size": len(large_payload),
        "large_crc32": large.section("BLOB").crc32, "large_tail": large_payload[-16:],
        "dat_probes": [list(p) for p in dat_probes], "unit_rows": unit_rows,
        "rule_associations": [list(a) for a in rule_assoc], "rule_sums": dict(sorted(rule_sums.items())),
        "terrain_ids": terrain_ids, "terrain_flags": terrain_flags,
        "mission_derived": mission_derived(files["mission.kpm"]), "mission_strings": list(MISSION_STRINGS),
        "grpmeta_hits": grpmeta_expect(grpmeta_set),
    }
    sys.path.insert(0, str(ROOT / "tools" / "oracle"))
    from oracle_common import golden_json

    (out / "expect.json").write_text(golden_json(_text_value(expect)))
    written.append(out / "expect.json")
    return written


def main() -> int:
    logging.basicConfig(level=logging.INFO, format="%(message)s")
    out = Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_OUT
    for path in build_fixtures(out):
        log.info("wrote %s", path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
