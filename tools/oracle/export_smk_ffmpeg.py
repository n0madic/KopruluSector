#!/usr/bin/env python3
"""Export FFmpeg's decode of the 1.17 install's 28 cinematics into a JSON golden (ADR 0018, stage D7).

Each movie of defs/campaign/cinematics.json (`smk\\<name>.smk`, the effective copy of the profile's archives checked
against the reference install's size and SHA-256, as converter/emit/cinematics_smk.py reads it) is decoded by FFmpeg
twice: its frames as `pal8` (each frame's width x height palette indices, then its palette's 256 entries as B, G, R,
255) and its first sound track as 16-bit little-endian PCM. The golden keeps the byte count and SHA-256 of each;
tests/c/test_movies.c decodes the same files with kp_smk (its palette records and Smacker sound) and compares.

Output: tests/golden/oracle/cinematics/smk_ffmpeg.json, {"movies": {name: {"sound": [bytes, sha256], "video": [...]}}}.

Usage: python3 tools/oracle/export_smk_ffmpeg.py --data DIR [--check]   (DIR: the 1.17 install, KOPRULU_ORIGINAL)
"""
from __future__ import annotations

import argparse
import hashlib
import logging
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

from oracle_common import GOLDEN, ROOT, ExportError, update_goldens

sys.path.insert(0, str(ROOT))
from converter.emit import cinematics, cinematics_smk  # noqa: E402
from converter.mpq.profile import DEFAULT_PROFILE, Profile, Resolver  # noqa: E402

log = logging.getLogger("export_smk_ffmpeg")
GENERATOR = "tools/oracle/export_smk_ffmpeg.py --data DIR"
OUTPUT = GOLDEN / "cinematics" / "smk_ffmpeg.json"
DECODES = {
    "video": ["-map", "0:v", "-fps_mode", "passthrough", "-f", "rawvideo", "-pix_fmt", "pal8"],
    "sound": ["-map", "0:a:0", "-f", "s16le", "-c:a", "pcm_s16le"],
}
CHUNK = 1 << 20


def decode(path: Path, args: list[str]) -> list:
    """[bytes, SHA-256] of FFmpeg's output for one movie, read as it comes (the longest movie's frames are 3 GB)."""
    p = subprocess.Popen(["ffmpeg", "-v", "error", "-i", str(path), *args, "-"], stdout=subprocess.PIPE)
    h, n = hashlib.sha256(), 0
    assert p.stdout is not None
    while chunk := p.stdout.read(CHUNK):
        h.update(chunk)
        n += len(chunk)
    if p.wait() != 0:
        raise ExportError(f"ffmpeg failed on {path.name}")
    return [n, h.hexdigest()]


def movies(data: Path) -> dict:
    """Each movie's decodes, the copies read and checked as the converter reads them."""
    known = cinematics_smk.expected()
    out = {}
    with Resolver(data, Profile.load(DEFAULT_PROFILE)) as resolver, tempfile.TemporaryDirectory() as tmp:
        for name in cinematics.table()[0]:
            size, sha256 = known[name]
            _, raw = resolver.read_expected(cinematics_smk.SMK_PATH.format(name=name), size, sha256)
            path = Path(tmp) / f"{name}.smk"
            path.write_bytes(raw)
            del raw
            out[name] = {kind: decode(path, args) for kind, args in DECODES.items()}
            path.unlink()
            log.info("%s: %s", name, out[name])
    return out


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--data", default=os.environ.get("KOPRULU_ORIGINAL"), help="the 1.17 install")
    parser.add_argument("--check", action="store_true", help="report a stale golden, write nothing")
    args = parser.parse_args(argv)
    logging.basicConfig(level=logging.INFO, format="%(name)s: %(message)s")
    if not args.data:
        parser.error("--data (or KOPRULU_ORIGINAL) names the 1.17 install")
    if shutil.which("ffmpeg") is None:
        log.error("ffmpeg is not installed")
        return 1
    return update_goldens({OUTPUT: {"movies": movies(Path(args.data))}}, args.check, GENERATOR)


if __name__ == "__main__":
    sys.exit(main())
