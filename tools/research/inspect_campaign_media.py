#!/usr/bin/env python3
"""Resolve every WAV that campaign maps reference through CHK data (1.17 archive set).

Campaign maps are stored as directories inside the CD archives (`<mapdata dir>\\staredit\\
scenario.chk`). Their sounds are named by strings relative to that directory. For each
mapdata entry (except the placeholder at index 0) this tool parses the effective CHK
sections (last instance wins), collects the string IDs listed in `WAV ` and the WAV field
of every TRIG and MBRF action, resolves `<map dir>\\<string>` against the archives in
effective order and reports anything missing. Reads archives only.
"""
import argparse
import hashlib
import json
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from inspect_resource_manifest import ARCHIVES, Storm  # noqa: E402

TRIGGER_SIZE = 2400
CONDITIONS = 16 * 20
ACTION_SIZE = 32


def sections(chk):
    out, off = {}, 0
    while off + 8 <= len(chk):
        name = chk[off:off + 4]
        size = struct.unpack_from("<i", chk, off + 4)[0]
        out[name] = chk[off + 8:off + 8 + max(size, 0)]
        off += 8 + size
    return out


def string_table(s):
    def get(i):
        if i <= 0 or 2 * i + 2 > len(s):
            return None
        off = struct.unpack_from("<H", s, 2 * i)[0]
        end = s.find(b"\0", off)
        return s[off:end].decode("latin1") if end >= 0 else None
    return get


def action_wavs(trig, kind):
    refs = []
    for t in range(len(trig) // TRIGGER_SIZE):
        base = t * TRIGGER_SIZE + CONDITIONS
        for a in range(64):
            off = base + a * ACTION_SIZE
            wav = struct.unpack_from("<I", trig, off + 8)[0]
            action = trig[off + 26]
            if wav and action:
                refs.append({"kind": kind, "trigger": t, "action_index": a, "action_id": action, "string": wav})
    return refs


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--app", required=True, type=Path)
    ap.add_argument("--stormlib", default="/opt/homebrew/lib/libstorm.dylib")
    ap.add_argument("--out", required=True, type=Path)
    a = ap.parse_args()
    st = Storm(a.stormlib)
    arcs = [(n, st.open(a.app / n)) for n in ARCHIVES]

    def find(name):
        for n, h in arcs:
            if st.lib.SFileHasFile(h, name.encode("latin1")):
                return n, st.read(h, name)
        return None, None

    _, tbl = find("arr\\mapdata.tbl")
    count = struct.unpack_from("<H", tbl, 0)[0]
    dirs = []
    for i in range(count):
        off = struct.unpack_from("<H", tbl, 2 + 2 * i)[0]
        dirs.append(tbl[off:tbl.index(b"\0", off)].decode("latin1"))
    maps, missing, total = [], [], 0
    for index, d in enumerate(dirs):
        if index == 0:
            continue
        arc, chk = find(d + "\\staredit\\scenario.chk")
        if chk is None:
            missing.append({"map": d, "file": "staredit\\scenario.chk"})
            continue
        sec = sections(chk)
        get = string_table(sec.get(b"STR ", b""))
        ids = set()
        wav_sec = sec.get(b"WAV ", b"")
        ids.update(x for x in struct.unpack_from(f"<{len(wav_sec) // 4}I", wav_sec) if x)
        refs = action_wavs(sec.get(b"TRIG", b""), "TRIG") + action_wavs(sec.get(b"MBRF", b""), "MBRF")
        ids.update(r["string"] for r in refs)
        files = []
        for sid in sorted(ids):
            rel = get(sid)
            if rel is None:
                missing.append({"map": d, "string": sid, "reason": "string index outside STR"})
                continue
            where, data = find(d + "\\" + rel)
            total += 1
            row = {"string": sid, "path": rel, "archive": where,
                   "sha256": hashlib.sha256(data).hexdigest() if data else None,
                   "used_by_actions": sum(1 for r in refs if r["string"] == sid)}
            files.append(row)
            if data is None:
                missing.append({"map": d, "path": rel, "used_by_actions": row["used_by_actions"]})
        maps.append({"mission_index": index, "map": d, "chk_archive": arc, "wav_files": files,
                     "trig_wav_actions": sum(1 for r in refs if r["kind"] == "TRIG"),
                     "mbrf_wav_actions": sum(1 for r in refs if r["kind"] == "MBRF")})
    report = {"maps": len(maps), "wav_references": total, "missing": missing, "per_map": maps}
    a.out.write_text(json.dumps(report, indent=1))
    print(json.dumps({"maps": len(maps), "wav_references": total, "missing": len(missing)}))
    for m in missing[:40]:
        print("missing", m)


if __name__ == "__main__":
    main()
