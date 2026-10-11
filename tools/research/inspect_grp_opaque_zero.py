#!/usr/bin/env python3
"""Count opaque color-0 pixels in every unit GRP referenced by images.dat (1.17 archive set).

Blend tables are indexed by source color * 256 + destination; their row 0 is left
uninitialized by the loader (atlas P22, V01 item 6). This check shows whether any GRP can
feed source color 0 through an opaque RLE packet. Reads archives only.
"""
import argparse
import hashlib
import json
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from inspect_resource_manifest import ARCHIVES, Storm  # noqa: E402


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--app", required=True, type=Path)
    ap.add_argument("--stormlib", default="/opt/homebrew/lib/libstorm.dylib")
    ap.add_argument("--out", required=True, type=Path)
    a = ap.parse_args()
    st = Storm(a.stormlib)
    arcs = [st.open(a.app / n) for n in ARCHIVES]

    def get(name):
        for h in arcs:
            d = st.read(h, name)
            if d:
                return d
        return None

    tbl = get("arr\\images.tbl")
    dat = get("arr\\images.dat")
    grp_ids = struct.unpack_from("<999I", dat, 0)

    def tbl_string(i):
        off = struct.unpack_from("<H", tbl, 2 + 2 * i)[0]
        return tbl[off:tbl.index(b"\0", off)].decode("latin1")

    seen, rows = set(), []
    total = zero = 0
    for gid in grp_ids:
        if not gid or gid in seen:
            continue
        seen.add(gid)
        name = "unit\\" + tbl_string(gid - 1)
        g = get(name)
        if not g:
            continue
        frames = struct.unpack_from("<H", g, 0)[0]
        opaque = zeros = 0
        for f in range(frames):
            _, _, fw, fh, lo = struct.unpack_from("<BBBBI", g, 6 + 8 * f)
            for r in range(fh):
                ro = lo + struct.unpack_from("<H", g, lo + 2 * r)[0]
                px = 0
                while px < fw:
                    b = g[ro]
                    ro += 1
                    if b >= 0x80:
                        px += b - 0x80
                    elif b >= 0x40:
                        n = b - 0x40
                        opaque += n
                        zeros += n if g[ro] == 0 else 0
                        ro += 1
                        px += n
                    else:
                        seg = g[ro:ro + b]
                        opaque += b
                        zeros += seg.count(0)
                        ro += b
                        px += b
        total += opaque
        zero += zeros
        rows.append({"name": name, "sha256": hashlib.sha256(g).hexdigest(), "frames": frames,
                     "opaque_pixels": opaque, "opaque_zero_pixels": zeros})
    report = {"grp_files": len(rows), "opaque_pixels": total, "opaque_zero_pixels": zero, "files": rows}
    a.out.write_text(json.dumps(report, indent=1))
    print(json.dumps({k: report[k] for k in ("grp_files", "opaque_pixels", "opaque_zero_pixels")}))


if __name__ == "__main__":
    main()
