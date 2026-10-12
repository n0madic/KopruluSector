"""Fonts: FNT decoding and glyph packets against the native blitter evidence."""
from __future__ import annotations

import hashlib
import json
import os
import struct
from pathlib import Path

import pytest

from converter.formats import fnt, pcx
from converter.kpf import kpd

ROOT = Path(__file__).resolve().parents[2]
ORIGINAL = os.environ.get("KOPRULU_ORIGINAL")
# Private bitmap of the native text probe (tools/research/probe_native_text_117.py).
PROBE_WIDTH, PROBE_HEIGHT, PROBE_START, PROBE_FILL = 128, 64, 128, 0xA5


def _font(glyph_packets: bytes, width: int, height: int) -> bytes:
    header = fnt.MAGIC + bytes((33, 33, width, height)) + struct.pack("<I", 12)
    return header + bytes((width, height, 0, 0)) + glyph_packets


@pytest.mark.parametrize("packets,masks,consumed", [
    (bytes([0x00, 0x01, 0x02, 0x03]), [1, 2, 3, 4], 4),  # zero skip plots, mask 0 included
    (bytes([0x08, 0x09, 0x00]), [0, 0, 2, 1], 3),  # skip 1 + mask 0 plots nothing; skip 1 + mask 1
    (bytes([0x19]), [0, 0, 0, 2], 1),  # skip 3 then mask 1 at the last pixel
    (bytes([0x21, 0x00]), [0, 0, 0, 0], 1),  # skip 4 reaches the end: its mask is not plotted
])
def test_packets(packets, masks, consumed):
    data = _font(packets, 2, 2)
    g = fnt.decode(data).glyphs[33]
    assert fnt.glyph_masks(data, g) == (bytes(masks), consumed)
    assert fnt.blit(bytes(masks), bytes(range(10, 18)), 0xEE)[0] == (0xEE if masks[0] == 0 else 9 + masks[0])


def test_font_hash_check():
    with pytest.raises(fnt.FntError, match="hashes to"):
        kpd.decoded_font(_font(b"\0", 1, 1), "0" * 64, "font")


@pytest.mark.skipif(not ORIGINAL, reason="KOPRULU_ORIGINAL not set")
def test_native_glyph_blits():
    from converter.mpq.profile import Resolver

    evidence = json.loads((ROOT / "docs/research/evidence/native-text-probes.json").read_text())
    cases = [c for c in evidence["cases"] if c["case"] == "all_stock_font_glyph_packets_private_indexed_buffers"]
    defs = kpd.font_defs()
    with Resolver(ORIGINAL, full_hash=False) as r:
        fonts = {f["name"]: kpd.decoded_font(r.read(f["plaintext"])[1], f["decoded_sha256"], f["name"])
                 for f in defs["fonts"]}
        rows = {row["path"]: pcx.decode(r.read(row["path"])[1]).pixels[:8] for row in defs["colour_rows"]}
    decoded = {name: fnt.decode(data) for name, data in fonts.items()}
    assert len(cases) == 4460
    for case in cases:
        inp = case["inputs"]
        data, font = fonts[inp["font"]], decoded[inp["font"]]
        g = font.glyphs[inp["char"]]
        masks, consumed = fnt.glyph_masks(data, g)
        pixels = fnt.blit(masks, rows[inp["lookup_resource"]], PROBE_FILL)
        bitmap = bytearray([PROBE_FILL] * (PROBE_WIDTH * PROBE_HEIGHT))
        stride = inp["stride"]
        for y in range(g.height):
            at = PROBE_START + y * stride
            bitmap[at : at + g.width] = pixels[y * g.width : (y + 1) * g.width]
        assert hashlib.sha256(bitmap).hexdigest() == case["actual"]["bitmap_sha256"], inp
        assert consumed == case["actual"]["packet_cursor"], inp
