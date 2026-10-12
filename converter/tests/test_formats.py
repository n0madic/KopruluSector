"""Format decoders on synthetic data, plus original-data checks when KOPRULU_ORIGINAL is set."""
from __future__ import annotations

import json
import os
import struct
from pathlib import Path

import pytest

from converter.formats import grp, palette, pcx, tbl, tileset, wav

ROOT = Path(__file__).resolve().parents[2]
ORIGINAL = os.environ.get("KOPRULU_ORIGINAL")


def _grp_bytes(rows: list[bytes], width: int) -> bytes:
    """One-frame GRP of the given encoded rows, frame placed at (1, 2) in a 10x10 canvas."""
    table = bytearray()
    offset = 2 * len(rows)
    for r in rows:
        table += struct.pack("<H", offset)
        offset += len(r)
    payload = bytes(table) + b"".join(rows)
    return struct.pack("<3H", 1, 10, 10) + struct.pack("<4BI", 1, 2, width, len(rows), 14) + payload


def test_grp_packets_and_opaque_zero():
    # Row 0: skip 2, repeat color 0 twice (opaque zero), literal [5, 0].
    # Row 1: literal [7], skip 5.
    data = _grp_bytes([bytes([0x82, 0x42, 0x00, 0x02, 5, 0]), bytes([0x01, 7, 0x85])], width=6)
    g = grp.decode(data)
    assert (g.width, g.height, len(g.frames)) == (10, 10, 1)
    f = g.frames[0]
    assert (f.x, f.y, f.width, f.height) == (1, 2, 6, 2)
    assert f.indices == bytes([0, 0, 0, 0, 5, 0, 7, 0, 0, 0, 0, 0])
    assert f.mask == bytes([0, 0, 255, 255, 255, 255, 255, 0, 0, 0, 0, 0])
    assert f.opaque_pixels == 5


def test_grp_round_trip():
    w, h = 70, 3
    indices = bytes((x * 7 + y) % 256 if x % 9 else 0 for y in range(h) for x in range(w))
    mask = bytes(0 if x % 13 == 0 or x > 66 else 255 for y in range(h) for x in range(w))
    indices = bytes(i if m else 0 for i, m in zip(indices, mask))
    frames = [grp.Frame(3, 4, w, h, indices, mask), grp.Frame(0, 0, 1, 1, b"\0", b"\xff")]
    g = grp.decode(grp.encode(80, 10, frames))
    assert g.frames == tuple(frames)


def test_grp_rejects_truncated():
    with pytest.raises(grp.GrpError):
        grp.decode(_grp_bytes([bytes([0x05, 1, 2])], width=5))
    with pytest.raises(grp.GrpError):
        grp.decode(struct.pack("<3H", 3, 1, 1))


def test_wpe_and_cycle():
    data = b"".join(bytes((i, 255 - i, i // 2, 0)) for i in range(256))
    p = palette.decode_wpe(data)
    assert p.rgb[3] == (3, 252, 1)
    assert p.to_rgba()[:8] == bytes((0, 255, 0, 255, 1, 254, 0, 255))
    colors = list(range(10))
    palette.rotate_simple(colors, 1, 6)
    assert colors == [0, 6, 1, 2, 3, 4, 5, 7, 8, 9]
    assert palette.CYCLE_RANGES["install"] == ()
    with pytest.raises(palette.PaletteError):
        palette.decode_wpe(b"\0" * 10)


def test_pcx_rle_and_palette():
    header = bytearray(128)
    header[0], header[1], header[2], header[3] = 0x0A, 5, 1, 8
    struct.pack_into("<4H", header, 4, 0, 0, 2, 1)  # 3x2
    header[65] = 1
    struct.pack_into("<H", header, 66, 4)  # stride 4 (padded)
    body = bytes([0xC4, 9]) + bytes([1, 2, 0xC1, 0xC5, 0])  # row0: 9 9 9 9; row1: 1 2 0xC5 0
    pal = bytes([0x0C]) + bytes(range(256)) * 3
    img = pcx.decode(bytes(header) + body + pal)
    assert (img.width, img.height) == (3, 2)
    assert img.pixels == bytes([9, 9, 9, 1, 2, 0xC5])
    assert img.palette[1] == (3, 4, 5)


def _tileset() -> tileset.Tileset:
    cv5 = bytearray(2 * tileset.CV5_RECORD)
    struct.pack_into("<16H", cv5, tileset.CV5_RECORD + tileset.CV5_HEADER, *range(1, 17))
    vx4 = struct.pack("<16H", *([0] * 16)) + struct.pack("<16H", *([2 | 1] + [0] * 15))
    vr4 = bytes(64) + bytes(range(64))
    vf4 = bytes(2 * tileset.VF4_RECORD)
    return tileset.decode(bytes(cv5), vx4, vr4, vf4)


def test_tileset_lookup_and_render():
    t = _tileset()
    assert t.megatile_count == 2
    assert t.megatile_for_tile((1 << 4) | 2) == 3
    assert t.megatile_for_tile(5000 << 4) == 0
    px = t.render_megatile(1)
    assert px[0:8] == bytes(range(7, -1, -1))  # minitile 1, flipped
    assert px[32:40] == bytes(range(15, 7, -1))
    assert px[8:16] == bytes(8)


def test_wav_odd_chunk_is_not_padded():
    fmt = struct.pack("<HHIIHH", 1, 1, 22050, 22050, 1, 8)
    data = b"RIFF" + struct.pack("<I", 0) + b"WAVE"
    data += b"fmt " + struct.pack("<I", 16) + fmt
    data += b"LIST" + struct.pack("<I", 3) + b"abc"  # odd, no pad byte follows
    data += b"data" + struct.pack("<I", 4410) + bytes(4410)
    info = wav.read_info(data)
    assert (info.sample_rate, info.avg_bytes_per_sec, info.data_length) == (22050, 22050, 4410)
    assert info.duration_ms == 200
    with pytest.raises(wav.WavError):
        wav.read_info(b"RIFF\0\0\0\0WAVE")


def test_tbl():
    data = struct.pack("<3H", 2, 6, 9) + b"ab\0c\0d"
    assert tbl.decode_tbl(data) == [b"ab", b"c"]


@pytest.mark.skipif(not ORIGINAL, reason="KOPRULU_ORIGINAL not set")
def test_original_terran_grps_match_opaque_counts():
    from converter.mpq.profile import Resolver

    evidence = json.loads((ROOT / "docs/research/evidence/grp-opaque-zero.json").read_text())
    expected = {f["name"].lower(): f for f in evidence["files"]}
    with Resolver(ORIGINAL, full_hash=False) as r:
        checked = 0
        for name, row in expected.items():
            if not name.startswith("unit\\terran\\"):
                continue
            g = grp.decode(r.read(name)[1])
            assert len(g.frames) == row["frames"], name
            assert sum(f.opaque_pixels for f in g.frames) == row["opaque_pixels"], name
            checked += 1
        assert checked > 50


@pytest.mark.skipif(not ORIGINAL, reason="KOPRULU_ORIGINAL not set")
def test_original_tileset_counts():
    from converter.mpq.profile import Resolver

    evidence = json.loads((ROOT / "docs/research/evidence/terrain-assets.json").read_text())
    with Resolver(ORIGINAL, full_hash=False) as r:
        for row in evidence["tilesets"]:
            name = row["tileset"]
            t = tileset.decode(*(r.read(f"tileset\\{name}.{ext}")[1] for ext in ("cv5", "vx4", "vr4", "vf4")))
            assert len(t.groups) == row["cv5_records"]
            assert t.megatile_count == row["vx4_records"]
            assert len(t.minitiles) // tileset.VR4_RECORD == row["vr4_records"]
