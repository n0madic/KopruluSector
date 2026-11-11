"""Cross-language fixtures the C suites read, with expect.json, the values their readers must give.

Run `python3 -m converter.kpf.fixtures [out_dir]`; the default output is tests/fixtures/generated/kpf (gitignored).
files/ holds synthetic original files at their MPQ paths (lowercased, `/` for `\\`), which the runtime's readers take
as an install's (ADR 0018; tests/c/kp_test.c kp_t_fixture_files): the DAT, the scripts, a tileset, the palettes' PCXs,
images.tbl with its GRPs and LO file, and a campaign mission's CHK with a sound. The interface's parts the suites set by
hand (ui.kpd, fonts.kpd, sounds.kpd, portraits.kpd) and the generic containers (basic.kpd, large.kpd) stay containers;
the movies, KPW1 and the Remastered rebuilds have their own files.
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
from ..formats import dialog, grp, kpw
from . import kpd, kpm
from ..derive import mission as mission_terrain

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


def palettes_files() -> dict[str, bytes]:
    """The PCXs the palettes are read from, synthetic: pixel value = (row * 7 + column + salt) % 256."""
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

    files: dict[str, bytes] = {}

    def record(path: str) -> bytes:
        files[path] = read(path)
        return files[path]

    kpd.build_palettes(record, profile_id="bw-1.17.0.1", data_set_hash=bytes(32))
    return files


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


FIXTURE_TILESET = "desert"  # ERA 13 & 7: the mission fixture's


def tileset_files() -> dict[str, bytes]:
    """The synthetic tileset's files (tileset\\desert.*)."""
    cv5, vx4, vr4, vf4 = fixture_tileset()
    wpe = bytes((i * 3) % 256 if i % 4 != 3 else 0 for i in range(1024))
    files = {"cv5": cv5, "vx4": vx4, "vr4": vr4, "vf4": vf4, "wpe": wpe, "grp": creep_edge_grp()}
    return {f"tileset\\{FIXTURE_TILESET}.{ext}": data for ext, data in files.items()}


def mission_terrain_fixture() -> mission_terrain.Terrain:
    """The mission fixture's terrain state as the game builds it (converter/derive/mission.py)."""
    cv5, _, _, vf4 = fixture_tileset()
    return mission_terrain.build(8, 4, struct.pack("<32H", *MISSION_TILES), cv5, vf4)


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


def dat_fixture() -> tuple[dict[str, bytes], list[tuple[str, str, int, int, int]]]:
    """The DAT files of every native layout filled by a byte counter, plus probes (table, column, id, element, expected
    value) for the C suites."""
    from ..formats import dat

    layouts = dat.load_layouts()
    files = {f.path: bytes((i * 7 + len(f.path)) % 256 for i in range(f.size)) for f in layouts}
    probes = []
    for f in layouts:
        for c in f.columns:
            last = c.first_id + (c.count - 1) // c.per_id
            for record, element in ((c.first_id, 0), (last, c.per_id - 1)):
                probes.append((f.table, c.name, record, element, dat.value(f, files[f.path], c.name, record, element)))
    return files, probes


def mission_derived(t: mission_terrain.Terrain) -> dict:
    """The mission fixture's terrain state decoded, for the suites."""
    from ..derive import contours, regions

    g = regions.decode(t.rgns)
    k = contours.decode(t.cont)
    last = g.count - 1
    first_contour = next((list(a[0]) for a in k.arrays if a), [0, 0, 0, 0, 0])
    return {"walk_size": len(t.walk), "regions": g.count, "splits": len(g.splits),
            "last_class": g.classes[last], "last_group": g.groups[last], "last_tiles": g.tiles[last],
            "last_neighbors": list(g.neighbors[last]), "last_center_x": g.centers[last][0],
            "cell_3_2": g.raster[2 * g.width + 3], "contour_counts": list(k.counts()), "capacities": list(k.capacities),
            "first_contour": list(first_contour)}


def fixture_wav(length: int) -> bytes:
    """Mono 22,050 Hz PCM16 RIFF file with `length` data bytes."""
    fmt = struct.pack("<HHIIHH", 1, 1, 22050, 44100, 2, 16)
    body = b"WAVE" + b"fmt " + struct.pack("<I", 16) + fmt + b"data" + struct.pack("<I", length) + bytes(length)
    return b"RIFF" + struct.pack("<I", len(body)) + body


def mission_chk() -> bytes:
    """The scenario of the mission fixture: 8x4, version 205, with colors, forces, overrides and two WAV strings."""
    sections = {k: v for k, v in V59.items() if k not in ("UNIS", "UPGS", "TECS", "UPGR", "PTEC")}
    sections.update({
        "VER ": struct.pack("<H", 205), "DIM ": struct.pack("<HH", 8, 4), "ERA ": struct.pack("<H", 13),
        "OWNR": bytes(range(12)), "SIDE": bytes(range(20, 32)), "SPRP": struct.pack("<HH", 1, 4),
        "FORC": bytes(range(1, 21)), "MTXM": struct.pack("<32H", *MISSION_TILES), "MASK": bytes(range(32)),
        "STR ": chk_strings(*MISSION_STRINGS), "TRIG": chk_trigger([(8, 2), (7, 3)]), "MBRF": chk_trigger([(2, 2)]),
        "UNIx": bytes(0x1048), "UPGx": bytes(0x31A), "TECx": bytes(0x18C), "PUPx": bytes(0x90E),
        "PTEx": bytes(0x688), "MRGN": bytes(0x13EC), "COLR": bytes([7, 6, 5, 4, 3, 2, 1, 0]),
    })
    return build_chk(sections)


FIXTURE_MISSION = "campaign\\fixture"  # tests/c/kp_test.h KP_T_FIXTURE_MISSION


def mission_files() -> dict[str, bytes]:
    """The 8x4 version 205 mission with colors, forces, overrides and two WAV strings, a.wav there and b.wav missing (its
    packs: tests/c/kp_test.c's MPAK)."""
    return {f"{FIXTURE_MISSION}\\staredit\\scenario.chk": mission_chk(), f"{FIXTURE_MISSION}\\a.wav": fixture_wav(4410)}


def grpmeta_files(files: dict[str, bytes]) -> dict[str, bytes]:
    """images.tbl over GRPMETA_NAMES (the C suite's images.dat refers to its entries) and the files they name."""
    from ..casc import rebuild

    return {"arr\\images.tbl": rebuild.tbl_encode([n.encode("ascii") for n in GRPMETA_NAMES]), **files}


def original_files() -> tuple[dict[str, bytes], list[tuple[str, str, int, int, int]]]:
    """The synthetic original files by MPQ path, and the DAT's probes."""
    dat_files, probes = dat_fixture()
    files = {**dat_files, **tileset_files(), **palettes_files(), **mission_files(),
             kpd.ISCRIPT_FILE: iscript_bin([(5, 0), (3, 1), (5, 1)]), kpd.AISCRIPT_FILE: ai_bin(AI_RECORDS, 16),
             kpd.BWSCRIPT_FILE: ai_bin(BW_RECORDS, 8)}
    files.update(grpmeta_files(grpmeta_fixture()[1]))
    return files, probes


def files_path(path: str) -> str:
    """Where an MPQ path's file lies under files/ (tests/c/kp_test.c fixture_read)."""
    return "files/" + path.lower().replace("\\", "/")


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
    encoders. The same bytes each time (bitexact: the muxer's identifiers are otherwise random), so that regenerating
    the fixtures leaves tools/ci.sh's remembered passes valid. FFmpeg's own Vorbis encoder is the first try; where its
    sound does not begin with the video (6.1 of Ubuntu 24.04 starts it 46 ms early, the encoder's delay as a negative
    start time, which the cinematics emitter refuses: a real movie's video begins 3 to 12 ms after the sound) libvorbis
    is the second."""
    if shutil.which("ffmpeg") is None or shutil.which("ffprobe") is None:
        return None
    for vorbis in ("vorbis", "libvorbis"):
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp) / "m.webm"
            run = subprocess.run(["ffmpeg", "-v", "error", "-y", "-f", "lavfi", "-i",
                                  f"{source}=size=64x48:rate=15:duration={duration}", "-f", "lavfi", "-i",
                                  f"sine=frequency=440:duration={duration}:sample_rate=22050", "-c:v", "libvpx-vp9",
                                  "-pix_fmt", pix_fmt, "-g", str(gop), "-c:a", vorbis, "-strict", "-2", "-ac", "2",
                                  "-shortest", "-fflags", "+bitexact", "-flags", "+bitexact", str(out)],
                                 capture_output=True, check=False)
            if run.returncode != 0 or not out.exists():
                continue
            try:
                cinematics.probe(str(out))
            except cinematics.CinematicsError as e:
                log.warning("cinematics fixtures: FFmpeg's %s encoder: %s", vorbis, e)
                continue
            return out.read_bytes()
    return None


def cine_frames(webm: bytes, pix_fmt: str = "rgba") -> bytes:
    """The WebM's frames as FFmpeg decodes them, one after the other in a raw pixel format: rgba, or the stream's own
    (yuv420p: its decoder's planes, Y, Cb and Cr, unconverted)."""
    with tempfile.TemporaryDirectory() as tmp:
        src = Path(tmp) / "m.webm"
        src.write_bytes(webm)
        run = subprocess.run(["ffmpeg", "-v", "error", "-i", str(src), "-an", "-f", "rawvideo", "-pix_fmt", pix_fmt, "-"],
                             capture_output=True, check=True)
        return run.stdout


# the cinematics fixture's subtitles: `starintr`'s (the second clip), `blizzard` has none
CINE_SUBTITLES = ("1\n00:00:00,100 --> 00:00:00,300\nПервая\n\n"
                  "2\n00:00:00,300 --> 00:00:00,700\nВторая\nстрока\n").encode("utf-8")


class _CineStorage:
    """The cinematics emitter's storage over two movies: `blizzard` (cinematic 0) and `starintr` (1), the second with
    enUS subtitles."""

    def __init__(self, movies: dict[str, bytes]):
        self.movies = {f"SD/smk/{n}.webm".lower(): d for n, d in movies.items()}
        self.movies["locales/enus/assets/smk/starintr.srt"] = CINE_SUBTITLES

    def has(self, name: str) -> bool:
        return name.lower() in self.movies

    def read(self, name: str) -> bytes:
        return self.movies[name.lower()]


# KPW1 fixtures (formats/kpw.py), read by test_ui_data's kpw tests: each movie's file and its stored frames' indices
# (`.indices`, frames + ring of width * height), the frames it was encoded from
KPW_PALETTE = bytes(c for i in range(256) for c in (i, 255 - i, (i * 37) % 256))  # 256 distinct colours
KPW_SRGB = (8, 4, 3, 100)  # width, height, WebM frames, frame rate: lossless VP9 4:4:4 sRGB as the SD portraits
KPW_ALPHA = (16, 16, 100)  # width, height, frame rate: VP8 with alpha as the glue's overlays
# the alpha movie's frames, colours far apart (a lossy 4:2:0 movie's colours come back near, not equal): white
# opaque; red, its left half transparent; green opaque
KPW_ALPHA_COLOURS = ((255, 255, 255), (255, 0, 0), (0, 255, 0))
KPW_ALPHA_INDICES = (10, 20, 30)


def kpw_srgb_indices(frame: int, pixel: int) -> int:
    return (pixel * 7 + frame * 13) % 255 + 1


def _webm(frames: list[bytes], width: int, height: int, rate_ms: int, args: list[str]) -> bytes | None:
    """The RGBA frames encoded by FFmpeg (bitexact), or None where it cannot."""
    if shutil.which("ffmpeg") is None:
        return None
    with tempfile.TemporaryDirectory() as tmp:
        out = Path(tmp) / "m.webm"
        run = subprocess.run(["ffmpeg", "-v", "error", "-y", "-f", "rawvideo", "-pix_fmt", "rgba", "-s",
                              f"{width}x{height}", "-r", f"{1000 / rate_ms:g}", "-i", "-", *args, "-fflags", "+bitexact",
                              "-flags", "+bitexact", str(out)], input=b"".join(frames), capture_output=True, check=False)
        return out.read_bytes() if run.returncode == 0 and out.exists() else None


def kpw_fixture() -> dict[str, bytes]:
    """movie.kpw (the sRGB ring movie: its header counts the WebM's 3 frames and a ring frame past them, the last
    repeated) and movie-alpha.kpw (2 frames and the ring frame), each with `.indices`; empty without FFmpeg's
    libvpx encoders (the C tests then skip)."""
    width, height, count, rate = KPW_SRGB
    indices = [bytes(kpw_srgb_indices(f, i) for i in range(width * height)) for f in range(count)]
    rgba = [b"".join(KPW_PALETTE[3 * k:3 * k + 3] + b"\xff" for k in f) for f in indices]
    srgb = _webm(rgba, width, height, rate, ["-c:v", "libvpx-vp9", "-pix_fmt", "gbrp", "-lossless", "1"])
    aw, ah, arate = KPW_ALPHA
    palette = bytearray(bytes((128, 128, 128)) * 256)
    palette[:3] = bytes(3)
    for colour, index in zip(KPW_ALPHA_COLOURS, KPW_ALPHA_INDICES):
        palette[3 * index:3 * index + 3] = bytes(colour)
    alpha_rgba, alpha_indices = [], []
    for f, (colour, index) in enumerate(zip(KPW_ALPHA_COLOURS, KPW_ALPHA_INDICES)):
        clear = [f == 1 and x < aw // 2 for _ in range(ah) for x in range(aw)]
        alpha_rgba.append(b"".join(bytes(colour) + (b"\0" if c else b"\xff") for c in clear))
        alpha_indices.append(bytes(0 if c else index for c in clear))
    alpha = _webm(alpha_rgba, aw, ah, arate, ["-c:v", "libvpx", "-pix_fmt", "yuva420p", "-auto-alt-ref", "0",
                                              "-crf", "4", "-b:v", "1M"])
    if srgb is None or alpha is None:
        log.warning("kpw fixtures: ffmpeg with libvpx and libvpx-vp9 is not available")
        return {}
    return {"movie.kpw": kpw.encode(width, height, count, rate, True, KPW_PALETTE, srgb),
            "movie.kpw.indices": b"".join(indices + indices[-1:]),
            "movie-alpha.kpw": kpw.encode(aw, ah, 2, arate, True, bytes(palette), alpha),
            "movie-alpha.kpw.indices": b"".join(alpha_indices)}


def _dds(width: int, height: int, fourcc: bytes, body: bytes) -> bytes:
    """A DDS file: uncompressed 32-bit R, G, B, A in byte order (fourcc b""), or DXT1 / DXT5 blocks."""
    from ..formats import dds

    header = bytearray(dds.HEADER_SIZE)
    header[:4] = dds.MAGIC
    struct.pack_into("<II", header, 12, height, width)
    if fourcc:
        struct.pack_into("<I4s", header, 80, dds.PF_FOURCC, fourcc)
    else:
        struct.pack_into("<I4sI4I", header, 80, dds.PF_RGB | 1, b"\0\0\0\0", 32, 0xFF, 0xFF00, 0xFF0000, 0xFF000000)
    return bytes(header) + body


def _dialog_texts(texts: list) -> bytes:
    """The inputs of rebuild.dialog_rebuild for test_scr: u16 count; per text u32 offset, u32 length, u8 hotkey, u8
    pointers and their u32 fields, u16 text length and its bytes."""
    out = bytearray(struct.pack("<H", len(texts)))
    for t, text in texts:
        out += struct.pack("<IIBB", t.offset, t.length, t.hotkey, len(t.pointers))
        out += struct.pack(f"<{len(t.pointers)}I", *t.pointers) + struct.pack("<H", len(text)) + text
    return bytes(out)


def scr_fixture() -> dict[str, bytes]:
    """Synthetic inputs of StarCraft: Remastered's formats and what converter/casc/rebuild.py makes of them (scr/),
    read by test_scr: kp_scr.c, the C of rebuild.py, gives the same bytes (stage D6; over a real install
    `kp_data_probe --digests` checks every rebuild against `converter scr-digests`). Images: an uncompressed DDS with
    alpha, DXT1 in both colour modes, DXT5 in both alpha modes, a `.dds.grp` of the first two; each decoded (`.rgba`), its
    indices in palette.pal (`.indices`, rebuild.indices: duplicate colours, translucent and missing ones) and brought
    to another size (`fit-<name>`: u32 rows above, then the RGBA; rebuild.fit's three modes). A PCX (rebuild.pcx_file
    of pcx.pixels), a TBL (tbl_encode), two string XMLs (xml_strings, the second as RC3.xml's escapes: `.expect`, per
    string its id, a NUL, u32 size and the value), a dialog rebuilt (dialog_rebuild of dialog.skeleton by
    dialog.texts), a GRP of plain rows (uncompressed_grp of plain.in: u16 box width, height, frames; per frame x, y,
    w, h, u32 offset and its w * h pixels; u32 size) and a row-compressed GRP drawn in another palette (remap_grp of
    remap.grp from remap.wpe)."""
    from ..casc import rebuild
    from ..formats import dds

    files: dict[str, bytes] = {}
    # a palette with duplicate colours (k and k + 128 share theirs), index 0 black
    colours = tuple(((k % 128) * 2, (k % 128 * 7) % 256, 255 - (k % 128) * 2) for k in range(256))
    files["scr/palette.pal"] = b"".join(bytes(c) for c in colours)
    rgba = bytes(c for k in range(6) for c in (*colours[k * 9 + 3], (0, 128, 255, 170, 255, 1)[k]))
    images = {
        "rgba": _dds(3, 2, b"", rgba),
        # block 1: four colours (c0 > c1), block 2: three and transparent (c0 <= c1), its texels using index 3
        "dxt1": _dds(8, 4, b"DXT1", struct.pack("<HHI", 0xF800, 0x001F, 0x1B1B1B1B)
                     + struct.pack("<HHI", 0x07E0, 0xFFE0, 0xE4E4E4E4)),
        # block 1: eight alphas (a0 > a1), block 2: six and 0, 255 (a0 <= a1)
        "dxt5": _dds(8, 4, b"DXT5", bytes((200, 20)) + bytes.fromhex("88c6fa2ba6c6") + struct.pack("<HHI", 0xFFFF, 0x0000, 0x1B1B1B1B)
                     + bytes((20, 200)) + bytes.fromhex("f8c6fa7fa6ff") + struct.pack("<HHI", 0x07FF, 0xF81F, 0x4E4E4E4E)),
    }
    for name, data in images.items():
        image = dds.decode(data)
        files[f"scr/{name}.dds"], files[f"scr/{name}.rgba"] = data, image.rgba
        files[f"scr/{name}.indices"] = rebuild.indices(image, colours)
    for name, source, size in (("crop", "dxt1", (6, 3)), ("sample", "dxt1", (4, 2)), ("bottom", "rgba", (3, 5))):
        image, above = rebuild.fit(dds.decode(images[source]), *size)
        files[f"scr/fit-{name}"] = struct.pack("<I", above) + image.rgba
    body = b"".join(struct.pack("<IHHI", 0, dds.header(images[n]).width, dds.header(images[n]).height, len(images[n])) + images[n] for n in ("rgba", "dxt1"))
    files["scr/images.dds.grp"] = struct.pack("<IHH", 8 + len(body), 2, 1) + body
    pixels = bytes((k * 37) % 256 for k in range(15))
    files["scr/pcx.pixels"] = pixels
    files["scr/pcx.pcx"] = rebuild.pcx_file(5, 3, pixels, colours, bytes((1, 2, 3, 4, 5, 6)))
    files["scr/strings.tbl"] = rebuild.tbl_encode([b"first", b"", b"third\0part", b"\x01colour"])
    xml = ("<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<strings>\n"
           "  <string>\n    <id>a-1</id>\n    <value>Plain &amp; &lt;simple&gt;</value>\n  </string>\n"
           "  <string><id>b-2</id><value>Caf\u00e9 \u2014 \u20ac 5\u0007two</value></string>\n"
           "  <string> <id>a-1</id> <value>the last of equal ids</value> </string>\n"
           "  <string><id>c-3</id><value>\\\\4red\\n\\0041 \\t end\\</value></string>\n</strings>\n").encode("utf-8")
    for name, rc3 in (("strings", False), ("rc3", True)):
        files[f"scr/{name}.xml"] = xml
        strings = rebuild.xml_strings(xml)
        if rc3:
            strings = [(k, rebuild.unescape_rc3(v)) for k, v in strings]
        files[f"scr/{name}.expect"] = b"".join(k.encode() + b"\0" + struct.pack("<I", len(v)) + v for k, v in strings)
    original = dialog_bin()
    texts = []
    for offset, length, pointers, hotkey, _ in rebuild.dialog_strings(original):
        t = rebuild.DialogText(offset, length, tuple(pointers), "", hotkey)
        # the button's hotkey text longer (appended, stored upper case), the movie's name of the same length
        texts.append((t, b"quit now" if hotkey else original[offset:offset + length].upper()))
    files["scr/dialog.skeleton"] = rebuild.dialog_skeleton(original)
    files["scr/dialog.texts"] = _dialog_texts(texts)
    files["scr/dialog.bin"] = rebuild.dialog_rebuild(files["scr/dialog.skeleton"], texts)
    layout = {"box": [6, 4], "frames": [[0, 0, 3, 2, 50], [1, 1, 3, 2, 50], [2, 0, 4, 3, 60]]}
    frames = [bytes(range(10, 16)), bytes(range(20, 26)), bytes(range(30, 42))]
    files["scr/plain.in"] = (struct.pack("<3H", 6, 4, 3) + b"".join(struct.pack("<4BI", *f) + px
                                                                   for f, px in zip(layout["frames"], frames))
                             + struct.pack("<I", 80))
    files["scr/plain.grp"] = rebuild.uncompressed_grp(layout, frames, 80)
    mask = bytes((255, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 0))
    source = grp.Grp(4, 3, (grp.Frame(0, 0, 4, 3, bytes(k * 20 % 256 if m else 0 for k, m in enumerate(mask)), mask),))
    files["scr/remap.grp"] = grp.encode(4, 3, list(source.frames))
    wpe = b"".join(bytes((*colours[(k * 5) % 256], 0)) for k in range(256))
    wpe = wpe[:4 * 60] + bytes((1, 2, 3, 0)) + wpe[4 * 61:]  # a colour palette.pal lacks: the nearest
    files["scr/remap.wpe"] = wpe
    files["scr/remap.out"] = rebuild.remap_grp(grp.decode(files["scr/remap.grp"]), wpe, colours)
    return files


def cine_fixture(source_hash: bytes) -> dict[str, bytes]:
    """cine.kpd (cinematic 0: gbrp, 1: yuv420p) and the movies' FFmpeg-decoded frames, cine0.rgba and cine1.rgba, and
    cine1.yuv, the second movie's decoded planes (VP9 decoding is exact: libvpx, in C or with SIMD, gives FFmpeg's);
    the WebM the pack was split from, cine0.webm and cine1.webm, and the second one's subtitles, cine1.srt (stage D7:
    an install's movies played as they are, against the pack's clips); empty where FFmpeg cannot encode them (the C
    suite then skips)."""
    srgb, ycc = cine_webm("testsrc", "gbrp"), cine_webm("testsrc2", "yuv420p")  # not gradients: its colours are random
    if srgb is None or ycc is None:
        log.warning("cinematics fixtures: ffmpeg with libvpx-vp9 and vorbis is not available")
        return {}
    storage = _CineStorage({"blizzard": srgb, "starintr": ycc})
    subtitles = cinematics.subtitles(storage, cinematics.table()[0], cinematics.SUBTITLES_AUTO)
    with tempfile.TemporaryDirectory() as tmp:
        info = cinematics.generate(cinematics.remastered(storage, "sd"), Path(tmp), "sd", "bw-1.17.0.1", source_hash,
                                   subtitles)
        return {"cine.kpd": Path(info["path"]).read_bytes(), "cine0.rgba": cine_frames(srgb),
                "cine1.rgba": cine_frames(ycc), "cine1.yuv": cine_frames(ycc, "yuv420p"), "cine0.webm": srgb,
                "cine1.webm": ycc, "cine1.srt": CINE_SUBTITLES}


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

    originals, dat_probes = original_files()
    terrain = mission_terrain_fixture()
    rule_payload, rule_assoc, rule_sums = rules_fixture()
    unit_payload, unit_rows = unit_records_fixture()
    (out / "units.bin").write_bytes(unit_payload)
    (out / "rules.bin").write_bytes(rule_payload)
    files = {"basic.kpd": basic.to_bytes(), "large.kpd": large.to_bytes(),
             "fonts.kpd": fonts_fixture(source_hash), "ui.kpd": ui_fixture(source_hash),
             "sounds.kpd": sounds_fixture(source_hash), "portraits.kpd": portraits_fixture(source_hash)}
    files.update({files_path(p): data for p, data in originals.items()})
    files.update(kpw_fixture())
    files.update(scr_fixture())
    files.update(cine_fixture(source_hash))
    grpmeta_set, _ = grpmeta_fixture()
    shutil.rmtree(out / "files", ignore_errors=True)  # no file of an earlier run's layout stays
    written = []
    for name, data in files.items():
        path = out / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        written.append(path)

    expect = {
        "source_hash": source_hash, "ints": INTS, "basic_size": len(files["basic.kpd"]),
        "large_size": len(files["large.kpd"]), "large_payload_size": len(large_payload),
        "large_crc32": large.section("BLOB").crc32, "large_tail": large_payload[-16:],
        "dat_probes": [list(p) for p in dat_probes], "unit_rows": unit_rows,
        "rule_associations": [list(a) for a in rule_assoc], "rule_sums": dict(sorted(rule_sums.items())),
        "terrain_ids": terrain.ids, "terrain_flags": terrain.flags,
        "mission_derived": mission_derived(terrain), "mission_strings": list(MISSION_STRINGS),
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
