"""FFmpeg's command line tools as the WebM converters use them (formats/kpv.py, emit/cinematics.py): the check that
they are installed and ffprobe's fields of one stream."""
from __future__ import annotations

import shutil
import subprocess


def require(error: type[Exception], what: str) -> None:
    """Raise `error` unless ffmpeg and ffprobe are on the path; `what` names the work that needs them."""
    if shutil.which("ffmpeg") is None or shutil.which("ffprobe") is None:
        raise error(f"ffmpeg is needed to {what} StarCraft: Remastered's WebM (brew install ffmpeg)")


def probe(path: str, stream: str, entries: tuple[str, ...], error: type[Exception]) -> list[str] | None:
    """ffprobe's `entries` of a file's stream (`v:0`, `a:0`) as strings in their order; None when the file has no
    such stream; `error` when ffprobe fails or gives other fields."""
    run = subprocess.run(["ffprobe", "-v", "error", "-select_streams", stream, "-show_entries",
                          "stream=" + ",".join(entries), "-of", "csv=p=0", path], capture_output=True, check=False)
    out = run.stdout.decode(errors="replace").strip()
    if run.returncode:
        raise error(f"ffprobe failed: {run.stderr.decode(errors='replace').strip()}")
    if not out:
        return None
    fields = out.split(",")
    if len(fields) != len(entries):
        raise error(f"ffprobe gave {out!r} for {','.join(entries)}")
    return fields
