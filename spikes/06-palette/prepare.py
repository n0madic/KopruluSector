#!/usr/bin/env python3
"""Builds the inputs of spike 6: GPU indexed composition checked byte-for-byte against
the original 1.17.0.1 blitter results (docs/research/native-pixel-blitters.md).

For every drawing case of docs/research/evidence/native-pixel-blitters-probes.json it
reconstructs the probe's inputs (frame, crop, mirror, destination, lookup) and keeps
the SHA-256 of the 320x320 result buffer (with its two 16-byte 0xE7 guards) that the
original code produced. Retail resources are read from the private data directory and
checked against the evidence manifests; outputs go to spikes/06-palette/generated/
(gitignored):
  frames.png   RGBA atlas of all frames: R = colour index, A = opaque mask
  remap.png    256 x N source maps (R): identity, owner player colours, selection, shift
  tables.png   256 x N destination tables (R): blend banks with their leading row,
               dark row 18 (shadow)
  cases.kpd    Koprulu container: FRMS frame rectangles, CASE records, HASH hashes
Usage: python3 spikes/06-palette/prepare.py --data <dir with StarCraft.exe and MPQs>
"""
from __future__ import annotations

import argparse
import hashlib
import json
import logging
import struct
import sys
from dataclasses import dataclass
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))

from converter.formats import grp as grp_format  # noqa: E402
from converter.formats import pcx as pcx_format  # noqa: E402
from converter.kpf.container import Container  # noqa: E402
from converter.mpq.profile import Resolver  # noqa: E402

EVIDENCE = ROOT / "docs" / "research" / "evidence"
OUT = Path(__file__).resolve().parent / "generated"
ATLAS_WIDTH = 4096
DEST_X, DEST_Y = 7, 9  # probe default destination

# Draw methods understood by spikes/06-palette/case.fp
METHOD_SOURCE = 0      # out = remap[row][src]
METHOD_BLEND = 1       # out = tables[row + src][dst]
METHOD_DEST = 2        # out = tables[row][dst] (shadow, fire background lookup)

BLEND_PCX = ["ofire", "gfire", "bfire", "bexpl", "trans50", "red", "green"]

log = logging.getLogger("prepare")


@dataclass
class FrameImage:
    width: int
    height: int
    indices: bytes
    mask: bytes
    x: int = 0
    y: int = 0


def sha(b: bytes) -> str:
    return hashlib.sha256(b).hexdigest()


def rows_frame(rows: list[list[int | None]]) -> FrameImage:
    width, height = len(rows[0]), len(rows)
    indices = bytes(0 if v is None else v for row in rows for v in row)
    mask = bytes(0 if v is None else 255 for row in rows for v in row)
    return FrameImage(width, height, indices, mask)


def pack(frames: list[FrameImage]) -> int:
    """Shelf packing into ATLAS_WIDTH columns; returns the atlas height."""
    x = y = shelf = 0
    for f in sorted(frames, key=lambda f: -f.height):
        if x + f.width > ATLAS_WIDTH:
            x, y, shelf = 0, y + shelf, 0
        f.x, f.y = x, y
        x += f.width + 1
        shelf = max(shelf, f.height + 1)
    return y + shelf


WIDTH = HEIGHT = 320
GUARD = bytes([0xE7] * 16)
BASELINE = bytes(i & 255 for i in range(WIDTH * HEIGHT))


def compose(case: tuple, frames: list[FrameImage], remap_rows: list[bytes], table_rows: list[bytes]) -> bytes:
    """The kernel semantics the GPU shader implements, on a 320x320 buffer."""
    fid, x, y, left, top, w, h, mirror, method, row, _ = case
    f = frames[fid]
    out = bytearray(BASELINE)
    for yy in range(h):
        for xx in range(w):
            sx = f.width - 1 - left - xx if mirror else left + xx
            p = (top + yy) * f.width + sx
            if not f.mask[p]:
                continue
            src = f.indices[p]
            d = (y + yy) * WIDTH + x + xx
            if method == METHOD_SOURCE:
                out[d] = remap_rows[row][src]
            elif method == METHOD_BLEND:
                out[d] = table_rows[row + src][out[d]]
            else:
                out[d] = table_rows[row][out[d]]
    return GUARD + bytes(out) + GUARD


def cpu_check(cases: list, frames: list[FrameImage], remap_rows: list[bytes], table_rows: list[bytes]) -> None:
    bad = [i for i, c in enumerate(cases) if hashlib.sha256(compose(c, frames, remap_rows, table_rows)).digest() != c[-1]]
    if bad:
        raise SystemExit(f"CPU reconstruction differs from the original in {len(bad)} cases, first {bad[:5]}")
    log.info("CPU reconstruction matches all %d original hashes", len(cases))


def main() -> int:
    logging.basicConfig(level=logging.INFO, format="%(message)s")
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--data", required=True)
    parser.add_argument("--cpu-check", action="store_true",
                        help="recompute every case on the CPU and compare with the original hashes")
    args = parser.parse_args()

    probes = json.loads((EVIDENCE / "native-pixel-blitters-probes.json").read_text())
    image_manifest = json.loads((EVIDENCE / "native-image-assets.json").read_text())
    pixel_manifest = json.loads((EVIDENCE / "native-pixel-assets.json").read_text())

    with Resolver(args.data, full_hash=False) as resolver:
        def read(path: str, want: str) -> bytes:
            _, data = resolver.read(path)
            if sha(data) != want:
                raise SystemExit(f"{path}: sha256 differs from the evidence manifest")
            return data

        grps = {row["sha256"]: (row["path"], grp_format.decode(read(row["path"], row["sha256"])))
                for row in image_manifest["images"]}
        pcx = {path: pcx_format.decode(read(path, row["sha256"])).pixels
               for path, row in pixel_manifest["assets"].items() if path.endswith(".pcx")}

    player, selection = pcx["game\\tunit.pcx"], pcx["game\\tselect.pcx"]
    shift, dark = pcx["tileset\\badlands\\shift.pcx"], pcx["tileset\\badlands\\dark.pcx"]
    identity = bytes(range(256))

    def override(first: int, values: bytes) -> bytes:
        return identity[:first] + values + identity[first + len(values):]

    remap_rows = [identity]
    owner_row = {}
    for owner in range(12):
        owner_row[owner] = len(remap_rows)
        remap_rows.append(override(8, player[owner * 8:(owner + 1) * 8]))
    selection_row = {}
    for color in range(3):
        selection_row[color] = len(remap_rows)
        remap_rows.append(override(1, selection[color * 8:(color + 1) * 8]))
    shift_row = len(remap_rows)
    remap_rows.append(shift[:256])

    table_rows: list[bytes] = []
    bank_base: dict[str, int] = {}
    bank_by_sha: dict[str, str] = {}
    for name in BLEND_PCX:
        path = f"tileset\\badlands\\{name}.pcx"
        bank = bytes([0x5A] * 256) + pcx[path]  # probe-supplied leading row (private fixture)
        bank_base[path] = len(table_rows)
        bank_by_sha[pixel_manifest["assets"][path]["sha256"]] = path
        table_rows += [bank[i:i + 256] for i in range(0, len(bank), 256)]
    shadow_row = len(table_rows)
    table_rows.append(dark[18 * 256:19 * 256])

    frames: list[FrameImage] = []
    grp_frame_id: dict[tuple[str, int], int] = {}
    for asset_sha, (_, g) in sorted(grps.items(), key=lambda kv: kv[1][0]):
        for n, f in enumerate(g.frames):
            grp_frame_id[(asset_sha, n)] = len(frames)
            frames.append(FrameImage(f.width, f.height, f.indices, f.mask))

    synthetic_ids: dict[str, int] = {}

    def synthetic(key: str, rows: list[list[int | None]]) -> int:
        if key not in synthetic_ids:
            synthetic_ids[key] = len(frames)
            frames.append(rows_frame(rows))
        return synthetic_ids[key]

    authored = [[None] * 3 + [0] * 4 + list(range(64)) + [None] * 5 + [8] * 63 + [255] * 63 + [None] * 6]
    ramp32 = [list(range(1, 33))]

    cases = []  # (frame, x, y, left, top, w, h, mirror, method, row, expected)
    skipped = 0
    for c in probes["cases"]:
        name, i = c["case"], c["inputs"]
        if "indexed_bitmap_sha256" not in c["actual"]:
            skipped += 1  # colour-setter cases: no bitmap
            continue
        expected = c["actual"]["indexed_bitmap_sha256"]
        assert c["passed"] and expected == c["expected"]["indexed_bitmap_sha256"]
        mirror = int(bool(i.get("mirror", False)))
        x, y, method, row = DEST_X, DEST_Y, METHOD_SOURCE, 0
        if name == "all_recorded_grp_frames_complete_native_indexed_blit":
            fid = grp_frame_id[(i["asset_sha256"], i["frame"])]
            left, top, w, h = 0, 0, frames[fid].width, frames[fid].height
        elif name == "stock_grp_crop_and_zero_height_private_buffer":
            fid = grp_frame_id[(i["asset_sha256"], i["frame"])]
            left, top, w, h = i["crop"]
        elif name == "authored_rle_packet_boundaries_and_opaque_zero":
            fid = synthetic("authored", authored)
            left, top, w, h = i["left"], 0, i["width"], 1
        elif name == "stock_player_override_complete_draw_and_restore":
            fid = synthetic("ramp32", ramp32)
            left, top, w, h, row = 0, 0, 32, 1, owner_row[i["new_owner"]]
        elif name == "stock_selection_color_override_and_restore":
            fid = synthetic("ramp32", ramp32)
            left, top, w, h, row = 0, 0, 32, 1, selection_row[i["selection_class"]]
        elif name == "stock_shift_lookup_entire_global_map_and_restore":
            fid = synthetic("ramp32", ramp32)
            left, top, w, h, row = 0, 0, 32, 1, shift_row
        elif name in ("stock_dark_row18_shadow_background_lookup", "stock_fire_wrapper_low_byte_row_and_mask"):
            fg = i["foreground"]
            rows = [[None] * 3 + [fg] * 63 + [None] * 5 + list(range(64))]
            fid = synthetic(f"mask{fg}", rows)
            left, top, w, h = 0, 0, len(rows[0]), 1
            method = METHOD_DEST
            row = shadow_row if "shadow" in name else bank_base["tileset\\badlands\\ofire.pcx"] + i["bank"]
        elif name == "all_recorded_stock_blend_rows_all_background_indices":
            fg = i["foreground_index"]
            wide = i["background_range"] == [0, 254]
            width = 255 if wide else 1
            fid = synthetic(f"blend{fg}x{width}", [[fg] * width])
            left, top, w, h = 0, 0, width, 1
            x, y = (0, 0) if wide else (255, 0)
            method, row = METHOD_BLEND, bank_base[bank_by_sha[i["asset_sha256"]]]
            # tables[row + src][dst] == bank[src*256 + dst]; src is the foreground index.
        else:
            raise SystemExit(f"unhandled drawing case {name}")
        cases.append((fid, x, y, left, top, w, h, mirror, method, row, bytes.fromhex(expected)))

    if args.cpu_check:
        cpu_check(cases, frames, remap_rows, table_rows)

    height = pack(frames)
    atlas = Image.new("RGBA", (ATLAS_WIDTH, height), (0, 0, 0, 0))
    for f in frames:
        img = Image.frombytes("RGBA", (f.width, f.height), bytes(
            b for v, m in zip(f.indices, f.mask) for b in (v, 0, 0, m)))
        atlas.paste(img, (f.x, f.y))
    OUT.mkdir(parents=True, exist_ok=True)
    atlas.save(OUT / "frames.png")

    def rows_png(rows: list[bytes], path: Path) -> None:
        img = Image.frombytes("RGBA", (256, len(rows)), bytes(b for r in rows for v in r for b in (v, 0, 0, 255)))
        img.save(path)

    rows_png(remap_rows, OUT / "remap.png")
    rows_png(table_rows, OUT / "tables.png")

    c = Container(b"SP06", schema_version=1, converter_version=0, profile_id="bw-1.17.0.1",
                  source_hash=hashlib.sha256((EVIDENCE / "native-pixel-blitters-probes.json").read_bytes()).digest())
    c.add("FRMS", b"".join(struct.pack("<4H", f.x, f.y, f.width, f.height) for f in frames))
    c.add("CASE", b"".join(struct.pack("<I8H2BH", fid, x, y, left, top, w, h, 0, 0, mirror, method, row)
                           for fid, x, y, left, top, w, h, mirror, method, row, _ in cases))
    c.add("HASH", b"".join(e for *_, e in cases))
    (OUT / "cases.kpd").write_bytes(c.to_bytes())
    log.info("%d drawing cases (%d bitmap-free cases skipped), %d frames, atlas %dx%d, %d remap rows, %d table rows",
             len(cases), skipped, len(frames), ATLAS_WIDTH, height, len(remap_rows), len(table_rows))
    return 0


if __name__ == "__main__":
    sys.exit(main())
