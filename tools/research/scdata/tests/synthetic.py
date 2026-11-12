"""Synthetic original files for the tests of scdata (the Python converter's fixture builders kept for them when the
converter was removed, ADR 0019 stage C7; the C suites read the committed fixtures, tests/fixtures/kpf)."""
from __future__ import annotations

import struct

from ..formats import dialog, grp
from ..kpf import kpd, kpm

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


def fixture_wav(length: int) -> bytes:
    """Mono 22,050 Hz PCM16 RIFF file with `length` data bytes."""
    fmt = struct.pack("<HHIIHH", 1, 1, 22050, 44100, 2, 16)
    body = b"WAVE" + b"fmt " + struct.pack("<I", 16) + fmt + b"data" + struct.pack("<I", length) + bytes(length)
    return b"RIFF" + struct.pack("<I", len(body)) + body
