#!/usr/bin/env python3
"""Second case set of spike 6: cloak/EMP distortion (methods 3/8), the brightness
threshold pass (methods 2/4/5/7), the trans50 blend (methods 5/6/7), the warp texture
(method 12) and the size outline (method 15), checked
against the original results in docs/research/evidence/native-pixel-effects-probes.json
(docs/research/native-pixel-effects.md).

The original buffer is 640x480 with 16-byte 0xE7 guards; the baseline byte at offset i
is (i*17 + (i//640)*31) & 255. Distortion reads the buffer in place and wraps at
0x4b000 bytes, so earlier writes of the same image can feed later samples.

Outputs in spikes/06-palette/generated/effects/ (gitignored): frames.png, tables.png
(row 0 brightness, row 1 identity map, row 2 reversed map, rows 3.. trans50 bank with
its leading row) and cases.kpd (GEOM, FRMS, STAG draw stages, CASE, HASH).
Usage: python3 spikes/06-palette/prepare_effects.py --data <dir> [--cpu-check]
"""
from __future__ import annotations

import argparse
import hashlib
import json
import logging
import struct
import sys
from pathlib import Path

from PIL import Image

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parents[1]))

from prepare import EVIDENCE, FrameImage, pack, rows_frame, sha, ATLAS_WIDTH  # noqa: E402
from converter.formats import grp as grp_format  # noqa: E402
from converter.formats import pcx as pcx_format  # noqa: E402
from converter.kpf.container import Container  # noqa: E402
from converter.mpq.profile import Resolver  # noqa: E402

OUT = HERE / "generated" / "effects"
WIDTH, HEIGHT = 640, 480
WRAP = 0x4B000
GUARD = bytes([0xE7] * 16)
BASELINE = bytes((i * 17 + (i // WIDTH) * 31) & 255 for i in range(WIDTH * HEIGHT))

# Stage kinds understood by effects.fp
CLOAK, RAW, THRESHOLD, BLEND, WARP, OUTLINE = 0, 1, 2, 3, 4, 5
TEXTURE_TRANSPARENT = 0xA5  # probe-supplied scratch byte under transparent texture pixels
STAGES = {"threshold": [THRESHOLD], "cloak": [CLOAK], "raw": [RAW], "cloak_transition": [CLOAK, THRESHOLD],
          "detected_transition": [BLEND, THRESHOLD], "detected": [BLEND], "warp": [WARP], "outline": [OUTLINE]}
ROW_BRIGHTNESS, ROW_IDENTITY, ROW_REVERSED, ROW_BLEND = 0, 1, 2, 3

log = logging.getLogger("prepare_effects")


def signed(v: int) -> int:
    return v - 256 if v >= 128 else v


def brightness(palette: bytes) -> bytes:
    return bytes((77 * palette[i * 4] + 151 * palette[i * 4 + 1] + 28 * palette[i * 4 + 2] + 4096) >> 13
                 for i in range(256))


def compose(case: dict, frame: FrameImage, bright: bytes, blend: bytes, aux: FrameImage | None = None) -> bytes:
    """The probe's prediction model (checked against the original results)."""
    x, y, (left, top, cw, ch), mirror = case["x"], case["y"], case["crop"], case["mirror"]
    cmap = bytes(reversed(range(256))) if case["map_row"] == ROW_REVERSED else bytes(range(256))
    want = bytearray(BASELINE)
    if case["stages"] == [OUTLINE]:
        fh = frame.height
        for yy in range(ch):
            for xx in range(cw):
                if (top == 0 and yy == 0) or (left == 0 and xx == 0) or (left + cw == frame.width and xx == cw - 1) \
                        or (top + ch == fh and yy == ch - 1):
                    want[(y + yy) * WIDTH + x + xx] = case["arg"]
        return GUARD + bytes(want) + GUARD
    if case["stages"] == [WARP]:
        for yy in range(ch):
            for xx in range(cw):
                sx = frame.width - 1 - left - xx if mirror else left + xx
                if not frame.mask[(top + yy) * frame.width + sx]:
                    continue
                tx = cw - 1 - xx if mirror else xx
                p = (top + yy) * aux.width + left + tx
                want[(y + yy) * WIDTH + x + xx] = aux.indices[p] if aux.mask[p] else TEXTURE_TRANSPARENT
        return GUARD + bytes(want) + GUARD
    for stage in case["stages"]:
        for yy in range(ch):
            for xx in (range(cw - 1, -1, -1) if mirror else range(cw)):
                sx = frame.width - 1 - left - xx if mirror else left + xx
                p = (top + yy) * frame.width + sx
                if not frame.mask[p]:
                    continue
                v = frame.indices[p]
                d = (y + yy) * WIDTH + x + xx
                if stage in (CLOAK, RAW):
                    disp = bright[v] if stage == CLOAK else v
                    want[d] = want[(d + (-1 if mirror else 1) * disp) % (WIDTH * HEIGHT)]
                elif stage == BLEND:
                    want[d] = blend[v * 256 + want[d]]
                elif signed(bright[cmap[v]]) > signed(case["arg"] & 255):
                    want[d] = cmap[v]
    return GUARD + bytes(want) + GUARD


def main() -> int:
    logging.basicConfig(level=logging.INFO, format="%(message)s")
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--data", required=True)
    parser.add_argument("--cpu-check", action="store_true")
    args = parser.parse_args()

    probes = json.loads((EVIDENCE / "native-pixel-effects-probes.json").read_text())
    manifests = [json.loads((EVIDENCE / n).read_text()) for n in ("native-image-assets.json", "native-pixel-effect-assets.json")]
    pixel_manifest = json.loads((EVIDENCE / "native-pixel-assets.json").read_text())

    with Resolver(args.data, full_hash=False) as resolver:
        def read(path: str, want: str) -> bytes:
            _, data = resolver.read(path)
            if sha(data) != want:
                raise SystemExit(f"{path}: sha256 differs from the evidence manifest")
            return data

        grps = {}
        for m in manifests:
            for row in m["images"]:
                grps[row["sha256"]] = (row["path"], grp_format.decode(read(row["path"], row["sha256"])))
        assets = pixel_manifest["assets"]
        palette = read("tileset\\badlands.wpe", assets["tileset\\badlands.wpe"]["sha256"])
        trans50 = pcx_format.decode(read("tileset\\badlands\\trans50.pcx", assets["tileset\\badlands\\trans50.pcx"]["sha256"])).pixels

    bright = brightness(palette)
    blend = bytes([0x5A] * 256) + trans50  # probe-supplied leading row (private fixture)

    frames: list[FrameImage] = []
    frame_ids: dict = {}

    def frame_id(key, make) -> int:
        if key not in frame_ids:
            frame_ids[key] = len(frames)
            frames.append(make())
        return frame_ids[key]

    authored = [[None] * 3 + [0] * 4 + list(range(64)) + [None] * 5 + [8] * 63 + [255] * 63 + [None] * 6]
    wrap_rows = [[255] * 32 + list(range(1, 33)) + [None] * 4 + [8] * 32]

    def grp_frame(asset_sha: str, n: int) -> FrameImage:
        f = grps[asset_sha][1].frames[n]
        return FrameImage(f.width, f.height, f.indices, f.mask)

    cases = []
    for c in probes["cases"]:
        name, i = c["case"], c["inputs"]
        if name == "authored_effect_packet_clipping_signed_threshold":
            fid = frame_id("authored", lambda: rows_frame(authored))
            case = dict(x=7, y=9, crop=i["crop"], arg=i["threshold"], map_row=ROW_REVERSED if i["threshold"] == 4 else ROW_IDENTITY)
            mode = i["method"]
        elif name == "distortion_fixed_640x480_wrap_in_place":
            fid = frame_id("wrap", lambda: rows_frame(wrap_rows))
            case = dict(x=i["destination"][0], y=i["destination"][1], crop=[0, 0, len(wrap_rows[0]), 1], arg=0, map_row=ROW_IDENTITY)
            mode = i["method"]
        elif name == "stock_grp_complete_cloak_detected_and_emp_effects":
            fid = frame_id((i["asset_sha256"], i["frame"]), lambda: grp_frame(i["asset_sha256"], i["frame"]))
            case = dict(x=7, y=9, crop=i["crop"], arg=4, map_row=ROW_IDENTITY)
            mode = i["method"]
        elif name == "stock_warp_texture_full_two_stage_draw_and_restore":
            fid = frame_id((i["main_grp_sha256"], 0), lambda: grp_frame(i["main_grp_sha256"], 0))
            aux = frame_id((i["texture_grp_sha256"], i["texture_frame"]), lambda: grp_frame(i["texture_grp_sha256"], i["texture_frame"]))
            case = dict(x=7, y=9, crop=i["crop"], arg=0, map_row=ROW_IDENTITY, aux=aux)
            mode = "warp"
        elif name == "native_size_outline_preserves_clipped_frame_edges":
            fid = frame_id("outline", lambda: rows_frame([[1] * 31 for _ in range(17)]))
            case = dict(x=7, y=9, crop=i["crop"], arg=i["color"], map_row=ROW_IDENTITY, mirror=False)
            mode = "outline"
        else:
            continue
        key = "indexed_bitmap_sha256" if "indexed_bitmap_sha256" in c["actual"] else "bitmap_sha256"
        expected = c["actual"][key]
        assert c["passed"] and expected == c["expected"][key]
        case.update(fid=fid, mirror=int(bool(i.get("mirror", False))), stages=STAGES[mode], expected=bytes.fromhex(expected))
        case.setdefault("aux", 0)
        cases.append(case)

    if args.cpu_check:
        bad = [k for k, case in enumerate(cases)
               if hashlib.sha256(compose(case, frames[case["fid"]], bright, blend, frames[case["aux"]])).digest() != case["expected"]]
        if bad:
            raise SystemExit(f"CPU model differs from the original in {len(bad)} cases, first {bad[:5]}")
        log.info("CPU model matches all %d original hashes", len(cases))

    height = pack(frames)
    atlas = Image.new("RGBA", (ATLAS_WIDTH, height), (0, 0, 0, 0))
    for f in frames:
        atlas.paste(Image.frombytes("RGBA", (f.width, f.height),
                                    bytes(b for v, m in zip(f.indices, f.mask) for b in (v, 0, 0, m))), (f.x, f.y))
    OUT.mkdir(parents=True, exist_ok=True)
    atlas.save(OUT / "frames.png")
    rows = [bright, bytes(range(256)), bytes(reversed(range(256)))] + [blend[k:k + 256] for k in range(0, len(blend), 256)]
    Image.frombytes("RGBA", (256, len(rows)), bytes(b for r in rows for v in r for b in (v, 0, 0, 255))).save(OUT / "tables.png")

    stages = []
    for k, case in enumerate(cases):
        for stage in case["stages"]:
            stages.append((k, stage))
    cont = Container(b"SP6E", schema_version=1, converter_version=0, profile_id="bw-1.17.0.1",
                     source_hash=hashlib.sha256((EVIDENCE / "native-pixel-effects-probes.json").read_bytes()).digest())
    cont.add("GEOM", struct.pack("<4H", WIDTH, HEIGHT, 4, 5))  # tile size, tiles per row, tile rows
    cont.add("FRMS", b"".join(struct.pack("<4H", f.x, f.y, f.width, f.height) for f in frames))
    cont.add("CASE", b"".join(struct.pack("<I8H2BH", c["fid"], c["x"], c["y"], *c["crop"], c["arg"], c["aux"],
                                          c["mirror"], len(c["stages"]), c["map_row"]) for c in cases))
    cont.add("STAG", b"".join(struct.pack("<IB3x", k, stage) for k, stage in stages))
    cont.add("HASH", b"".join(c["expected"] for c in cases))
    (OUT / "cases.kpd").write_bytes(cont.to_bytes())
    log.info("%d effect cases, %d draw stages, %d frames, atlas %dx%d", len(cases), len(stages), len(frames), ATLAS_WIDTH, height)
    return 0


if __name__ == "__main__":
    sys.exit(main())
