#!/usr/bin/env python3
"""Check that every Smacker video in the 1.17 archive set decodes with a public decoder.

Storm's SVid functions (atlas S01) play portraits, glue animations and cinematics through
smackw32.dll. Smacker is a published format, so a converter can transcode it offline. For
each `.smk` resolved in the resource manifest this tool parses the Smacker header (frame
count, frame rate, flags, audio tracks) and fully decodes the file with FFmpeg, comparing
the decoded frame count with the header. Reads archives only; executes no original code.
"""
import argparse
import hashlib
import json
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from inspect_resource_manifest import ARCHIVES, Storm  # noqa: E402


def header(b):
    sig, width, height, frames, rate, flags = struct.unpack_from("<4sIIIiI", b, 0)
    audio = struct.unpack_from("<7I", b, 0x48)
    # rate > 0: milliseconds per frame; rate < 0: units of 10 microseconds; 0 -> 10 fps.
    if rate > 0:
        ms = rate
    elif rate < 0:
        ms = -rate / 100.0
    else:
        ms = 100
    ring = 1 if flags & 1 else 0
    return {"signature": sig.decode("latin1"), "width": width, "height": height, "frames": frames,
            "ring_frame": bool(ring), "flags": hex(flags), "ms_per_frame": ms,
            "audio_tracks": [{"rate": a & 0xFFFFFF, "flags": hex(a >> 24)} for a in audio if a & 0xFFFFFF]}


def ffdecode(path):
    p = subprocess.run(["ffprobe", "-v", "error", "-count_frames", "-select_streams", "v:0",
                        "-show_entries", "stream=nb_read_frames,codec_name,avg_frame_rate", "-of", "json", str(path)],
                       capture_output=True, text=True)
    if p.returncode:
        return {"ok": False, "error": p.stderr.strip()[:200]}
    s = json.loads(p.stdout)["streams"][0]
    return {"ok": True, "codec": s.get("codec_name"), "decoded_frames": int(s.get("nb_read_frames", 0)),
            "avg_frame_rate": s.get("avg_frame_rate")}


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--app", required=True, type=Path)
    ap.add_argument("--manifest", required=True, type=Path)
    ap.add_argument("--stormlib", default="/opt/homebrew/lib/libstorm.dylib")
    ap.add_argument("--out", required=True, type=Path)
    a = ap.parse_args()
    st = Storm(a.stormlib)
    arcs = {n: st.open(a.app / n) for n in ARCHIVES}
    man = json.loads(a.manifest.read_text())
    rows = []
    with tempfile.TemporaryDirectory() as tmp:
        for r in man["resources"]:
            if not r["name"].lower().endswith(".smk"):
                continue
            b = st.read(arcs[r["archive"]], r["name"])
            f = Path(tmp) / "v.smk"
            f.write_bytes(b)
            h = header(b)
            d = ffdecode(f)
            # A ring frame repeats frame 0 at the end and is not a separate display frame.
            match = d.get("ok") and d["decoded_frames"] in (h["frames"], h["frames"] + (1 if h["ring_frame"] else 0))
            rows.append({"name": r["name"], "sha256": hashlib.sha256(b).hexdigest(), "header": h, "ffmpeg": d,
                         "frame_count_matches": bool(match)})
    summary = {"files": len(rows), "decoded": sum(1 for x in rows if x["ffmpeg"]["ok"]),
               "frame_count_matches": sum(1 for x in rows if x["frame_count_matches"]),
               "signatures": sorted({x["header"]["signature"] for x in rows}),
               "ffmpeg_version": subprocess.run(["ffmpeg", "-version"], capture_output=True, text=True).stdout.split("\n")[0]}
    a.out.write_text(json.dumps({"summary": summary, "files": rows}, indent=1))
    print(json.dumps(summary))
    for x in rows:
        if not x["frame_count_matches"]:
            print("mismatch", x["name"], x["header"]["frames"], x["ffmpeg"])


if __name__ == "__main__":
    main()
