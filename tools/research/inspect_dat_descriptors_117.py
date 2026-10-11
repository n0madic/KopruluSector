#!/usr/bin/env python3
"""Exhaustive DAT record schemas of StarCraft 1.17.0.1 from the executable's own descriptors.

Every call of the generic DAT loader 0x4442b0 passes the file name in ECX and a column
descriptor in EDX. A descriptor is a list of {destination, element size, element count}
triples terminated by a zero destination; the loader reads the columns in that order and
rejects a file whose size differs from the descriptor sum (fatal error 24, atlas P14).
This tool finds every call site, decodes each descriptor, checks the expected size against
the effective archive file, and labels columns with the pinned PyMS DAT property names
(parsed with ast, never imported). Original code is not executed.
"""
import argparse
import ast
import hashlib
import json
import struct
import sys
from pathlib import Path

import pefile

EXE_HASH = "94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0"
LOADER = 0x4442B0
PYMS_FILES = {
    "arr\\units.dat": "UnitsDAT.py", "arr\\weapons.dat": "WeaponsDAT.py", "arr\\flingy.dat": "FlingyDAT.py",
    "arr\\sprites.dat": "SpritesDAT.py", "arr\\images.dat": "ImagesDAT.py", "arr\\upgrades.dat": "UpgradesDAT.py",
    "arr\\techdata.dat": "TechDAT.py", "arr\\sfxdata.dat": "SoundsDAT.py", "arr\\portdata.dat": "PortraitsDAT.py",
    "arr\\mapdata.dat": "CampaignDAT.py", "arr\\orders.dat": "OrdersDAT.py",
}
TYPE_SIZE = {"byte": 1, "short": 2, "long": 4, "dword": 4, "word": 2}


def pyms_properties(path):
    tree = ast.parse(path.read_text())
    for node in ast.walk(tree):
        if isinstance(node, ast.Assign) and any(isinstance(t, ast.Name) and t.id == "FORMAT" for t in node.targets):
            call = node.value
            fmt = ast.literal_eval(call.args[0])
            return fmt
    raise ValueError(f"no FORMAT in {path}")


def call_sites(pe, img):
    text = next(s for s in pe.sections if s.Name.startswith(b".text"))
    data = text.get_data()
    va0 = 0x400000 + text.VirtualAddress
    out = []
    for i in range(len(data) - 5):
        if data[i] != 0xE8:
            continue
        target = (va0 + i + 5 + struct.unpack_from("<i", data, i + 1)[0]) & 0xFFFFFFFF
        if target != LOADER:
            continue
        # Walk back up to 40 bytes for the last `mov ecx, imm32` (b9) and `mov edx, imm32` (ba).
        ecx = edx = None
        for back in range(5, 41):
            op = data[i - back]
            if op == 0xB9 and ecx is None:
                ecx = struct.unpack_from("<I", data, i - back + 1)[0]
            if op == 0xBA and edx is None:
                edx = struct.unpack_from("<I", data, i - back + 1)[0]
            if ecx and edx:
                break
        out.append((va0 + i, ecx, edx))
    return out


def cstring(img, va):
    off = va - 0x400000
    return img[off:img.index(b"\0", off)].decode("latin1")


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--exe", required=True, type=Path)
    ap.add_argument("--pyms", required=True, type=Path, help="PyMS checkout (pinned commit)")
    ap.add_argument("--dat-dir", required=True, type=Path, help="directory holding effective arr\\*.dat extracts")
    ap.add_argument("--out", required=True, type=Path)
    a = ap.parse_args()
    raw = a.exe.read_bytes()
    if hashlib.sha256(raw).hexdigest() != EXE_HASH:
        ap.error("unexpected executable")
    pe = pefile.PE(data=raw)
    img = pe.get_memory_mapped_image()
    files = {}
    for site, ecx, edx in call_sites(pe, img):
        name = cstring(img, ecx)
        cols = []
        off = edx - 0x400000
        while True:
            dest, size, count = struct.unpack_from("<III", img, off)
            if dest == 0:
                break
            cols.append({"destination": hex(dest), "element_size": size, "count": count})
            off += 12
        rec = files.setdefault(name.lower(), {"file": name, "descriptor": hex(edx), "call_sites": [], "columns": cols})
        rec["call_sites"].append(hex(site))
    for key, rec in files.items():
        expected = sum(c["element_size"] * c["count"] for c in rec["columns"])
        rec["expected_bytes"] = expected
        local = a.dat_dir / key.replace("\\", "/")
        if local.exists():
            b = local.read_bytes()
            rec["actual_bytes"] = len(b)
            rec["actual_sha256"] = hashlib.sha256(b).hexdigest()
            rec["size_matches"] = len(b) == expected
        pf = PYMS_FILES.get(key)
        if pf:
            fmt = pyms_properties(a.pyms / "PyMS/FileFormats/DAT" / pf)
            props = fmt["properties"]
            rec["pyms_entries"] = fmt.get("entries")
            if len(props) == len(rec["columns"]):
                for col, p in zip(rec["columns"], props):
                    col["name"] = p["name"]
                    want = TYPE_SIZE.get(p.get("type"))
                    col["pyms_type"] = p.get("type")
                    if want and want != col["element_size"]:
                        col["pyms_size_mismatch"] = True
                    for k in ("entry_offset", "entry_count"):
                        if k in p:
                            col["pyms_" + k] = p[k]
            else:
                rec["pyms_column_count"] = len(props)
    report = {"exe_sha256": EXE_HASH, "loader": hex(LOADER), "files": sorted(files.values(), key=lambda r: r["file"].lower())}
    a.out.write_text(json.dumps(report, indent=1))
    for r in report["files"]:
        print(r["file"], len(r["columns"]), "columns", r["expected_bytes"], r.get("actual_bytes"), r.get("size_matches"),
              "named" if all("name" in c for c in r["columns"]) else f"pyms cols {r.get('pyms_column_count')}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
