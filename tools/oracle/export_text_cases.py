#!/usr/bin/env python3
"""Export the original text engine's probe cases into a Lua golden file (plan phase 9).

docs/research/evidence/native-text-probes.json records the unchanged original font and text routines run in Unicorn
(tools/research/probe_native_text_117.py; docs/research/native-text.md): glyph lookups, byte widths, the bounded
line consumer's measurement, the word-wrap parser, signed tabs, the multi-line engine, vertical alignment, the colour
setter and its grey-row lock, and the glyph packet blitter over every stock glyph. tests/lua/test_kp_ui.lua runs the
same inputs through kp_ui's text engine (kp_ui_text.c).

The probe's context (its `setup`): pen (5, 4), clip left 2, right 120 and bottom 1000, letter spacing 1, gap 3, tab
2 x max width, flags 0x11, the wrap mode on, the bottom extent at the pen's y, row 0 current and row 1 previous.

The packet cases hold no pixels of the fonts: the exporter rebuilds each expected 128 x 64 probe bitmap from the
converted fonts (generated/data/fonts.kpd), checks its SHA-256 against the evidence, and keeps the CRC-32 of the
glyph's rows (stride width + pad) and the packet bytes consumed. The wrapped TBL cases hold the string's resource and
index; the test reads the text from generated/data/strings.kpd.

Output: tests/golden/oracle/ui/text_cases.lua, one case per line, by family:
  {"glyph", font, code, offset (-1 none)}             {"width", font, code, spacing, width}
  {"span", font, code, x, bottom, consumed}            {"wrap", font, hex, right, consumed, x}
  {"tbl", resource, index, consumed, x}                {"tab", font, x, left, wrap, consumed, x}
  {"lines", font, flags, hex, consumed, count, x, y, l, t, r, b}
  {"highlight", font, hex, x, bottom}                  {"valign", flags, first_y, final_y, y}
  {"colour", code, row, previous}                      {"grey", code, row_before, row, previous}
  {"packets", font, code, resource, pad, crc, consumed}  {"zero", width, height, pad, hex, crc, consumed}

Usage: python3 tools/oracle/export_text_cases.py [--check]
"""
from __future__ import annotations

import argparse
import hashlib
import logging
import sys
import zlib

from oracle_common import GOLDEN, ROOT, load_evidence, render_module

sys.path.insert(0, str(ROOT))
from converter.kpf.container import parse  # noqa: E402

log = logging.getLogger("export_text_cases")
GENERATOR = "tools/oracle/export_text_cases.py"
EVIDENCE = "native-text-probes.json"
OUTPUT = GOLDEN / "ui" / "text_cases.lua"
FONTS = ROOT / "generated" / "data" / "fonts.kpd"
SECTIONS = {"font8": "F8  ", "font10": "F10 ", "font14": "F14 ", "font16": "F16 ", "font16x": "F16X"}
FONT_BASE = 0x810000  # the probe's font image
ROW_BASE = 0x6D3050   # the colour rows
ROW_RESOURCES = {"game\\tFontGam.pcx": "TFGM", "glue\\title\\tFont.pcx": "TFNT"}
TBL_RESOURCES = {"rez\\stat_txt.tbl": "STAT", "rez\\gluAll.tbl": "GLUA"}
WIDTH, HEIGHT, AT = 128, 64, 128  # the probe bitmap and the glyph's place in it
SENTINEL = 0xA5


def packets(data: bytes, width: int, height: int, lookup: bytes) -> tuple[bytes, int]:
    """The glyph's pixels (SENTINEL untouched) and the packet bytes consumed (0x4cfdf0, native-text.md)."""
    out = bytearray([SENTINEL] * (width * height))
    pos, at, total = 0, 0, width * height
    while pos < total:
        value = data[at]
        at += 1
        skip, mask = value >> 3, value & 7
        pos += skip
        if pos >= total:
            break
        if not skip or mask:
            out[pos] = lookup[mask]
            pos += 1
    return bytes(out), at


def probe_bitmap(rows: bytes, width: int, height: int, pad: int) -> bytes:
    want = bytearray([SENTINEL] * (WIDTH * HEIGHT))
    for y in range(height):
        o = AT + y * (width + pad)
        want[o:o + width] = rows[y * width:(y + 1) * width]
    return bytes(want)


def strided(rows: bytes, width: int, height: int, pad: int) -> bytes:
    """The glyph's rows at stride width + pad, the pad columns untouched."""
    return b"".join(rows[y * width:(y + 1) * width] + bytes([SENTINEL] * pad) for y in range(height))


def glyph_record(font: bytes, code: int) -> tuple[int, int, int, int, int]:
    low = font[4]
    off = int.from_bytes(font[8 + 4 * (code - low):12 + 4 * (code - low)], "little")
    return off, font[off], font[off + 1], font[off + 2], font[off + 3]


def build() -> str:
    data, digest = load_evidence(EVIDENCE)
    kpd = parse(FONTS.read_bytes())
    fonts = {name: kpd.section(s).payload for name, s in SECTIONS.items()}
    banks = {res: kpd.section(s).payload for res, s in ROW_RESOURCES.items()}
    cases = []
    for c in data["cases"]:
        fam, i, e = c["case"], c["inputs"], c["expected"]
        if not c["passed"]:
            raise SystemExit(f"{fam}: a failed probe case")
        if fam == "all_stock_font_byte_glyph_pointers":
            cases.append(["glyph", i["font"], i["char"], e["pointer"] - FONT_BASE if e["pointer"] else -1])
        elif fam == "all_stock_font_byte_widths_bearing_and_spacing":
            cases.append(["width", i["font"], i["char"], i["spacing"], e["width"]])
        elif fam == "all_byte_bounded_native_measurement_except_alignment":
            cases.append(["span", i["font"], i["char"], e["x"], e["bottom"], e["consumed"]])
        elif fam == "complete_native_word_wrap_and_control_parser":
            cases.append(["wrap", i["font"], i["text_hex"], i["right"], e["consumed"], e["x"]])
        elif fam == "complete_stock_tbl_wrapped_line_metrics":
            cases.append(["tbl", TBL_RESOURCES[i["resource"]], i["index"], e["consumed"], e["x"]])
        elif fam == "complete_signed_tab_idiv_both_parser_modes":
            cases.append(["tab", i["font"], i["x"], i["left"], i["wrap"], e["consumed"], e["x"]])
        elif fam == "complete_native_multiline_measurement_and_alignment":
            cases.append(["lines", i["font"], i["flags"], i["text_hex"], e["consumed"], e["line_count"], e["x"],
                          e["y"], *e["bounds"]])
        elif fam == "complete_bounded_highlight_measurement_advance":
            cases.append(["highlight", i["font"], i["text_hex"], e["x"], e["bottom"]])
        elif fam == "complete_vertical_alignment_signed_rounding":
            cases.append(["valign", i["flags"], i["first_y"], i["final_y"], e["y"]])
        elif fam == "all_control_color_setter_previous_row_state":
            cases.append(["colour", i["code"], (e["current"] - ROW_BASE) // 8, (e["previous"] - ROW_BASE) // 8])
        elif fam == "complete_control_only_text_gray_row_lock":
            cases.append(["grey", i["code"], (i["current"] - ROW_BASE) // 8, (e["current"] - ROW_BASE) // 8,
                          (e["previous"] - ROW_BASE) // 8])
        elif fam == "all_stock_font_glyph_packets_private_indexed_buffers":
            font = fonts[i["font"]]
            off, w, h, _, _ = glyph_record(font, i["char"])
            pad = i["stride"] - w
            rows, n = packets(font[off + 4:], w, h, banks[i["lookup_resource"]][:8])
            if hashlib.sha256(probe_bitmap(rows, w, h, pad)).hexdigest() != e["bitmap_sha256"] or n != e["packet_cursor"]:
                raise SystemExit(f"{i['font']} glyph {i['char']}: the rebuilt bitmap is not the original's")
            cases.append(["packets", i["font"], i["char"], ROW_RESOURCES[i["lookup_resource"]], pad,
                          zlib.crc32(strided(rows, w, h, pad)), n])
        elif fam == "native_glyph_packet_zero_mask_cross_row_and_final_skip":
            w, h, pad = i["width"], i["height"], i["stride_pad"]
            lead = bytes.fromhex(i["packet_prefix_hex"])
            rows, n = packets(lead + bytes([1] * 96), w, h, banks["game\\tFontGam.pcx"][:8])
            if hashlib.sha256(probe_bitmap(rows, w, h, pad)).hexdigest() != e["bitmap_sha256"] or n != e["consumed"]:
                raise SystemExit(f"zero-mask case {i}: the rebuilt bitmap is not the original's")
            cases.append(["zero", w, h, pad, i["packet_prefix_hex"], zlib.crc32(strided(rows, w, h, pad)), n])
    return render_module(GENERATOR, {"evidence_sha256": digest}, cases)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--check", action="store_true", help="fail when the golden file is stale")
    args = ap.parse_args()
    logging.basicConfig(level=logging.INFO, format="%(message)s")
    if not FONTS.exists():
        log.info("no generated/data/fonts.kpd, skipped")
        return 0
    text = build()
    if args.check:
        if not OUTPUT.exists() or OUTPUT.read_text() != text:
            log.error("%s is stale; run %s", OUTPUT.relative_to(ROOT), GENERATOR)
            return 1
        return 0
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT.write_text(text)
    log.info("wrote %s", OUTPUT.relative_to(ROOT))
    return 0


if __name__ == "__main__":
    sys.exit(main())
