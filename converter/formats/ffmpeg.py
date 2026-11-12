"""FFmpeg's command line tools as the converters use them (formats/kpw.py, kpf/fixtures.py): the
check that they are installed and ffprobe's fields of one stream."""
from __future__ import annotations

import json
import shutil
import subprocess


def require(error: type[Exception], what: str) -> None:
    """Raise `error` unless ffmpeg and ffprobe are on the path; `what` names the work that needs them."""
    if shutil.which("ffmpeg") is None or shutil.which("ffprobe") is None:
        raise error(f"ffmpeg is needed to {what} (brew install ffmpeg)")


def probe(path: str, stream: str, entries: tuple[str, ...], error: type[Exception]) -> list[str] | None:
    """ffprobe's `entries` of a file's stream (`v:0`, `a:0`) as strings in the order asked for (taken by name: ffprobe
    prints a stream's fields in its own order); None when the file has no such stream; `error` when ffprobe fails or
    lacks a field."""
    run = subprocess.run(["ffprobe", "-v", "error", "-select_streams", stream, "-show_entries",
                          "stream=" + ",".join(entries), "-of", "json", path], capture_output=True, check=False)
    if run.returncode:
        raise error(f"ffprobe failed: {run.stderr.decode(errors='replace').strip()}")
    try:
        streams = json.loads(run.stdout or b"{}").get("streams") or []
    except ValueError as e:
        raise error(f"ffprobe gave no JSON: {e}") from e
    if not streams:
        return None
    missing = [e for e in entries if e not in streams[0]]
    if missing:
        raise error(f"ffprobe gave no {','.join(missing)}")
    return [str(streams[0][e]) for e in entries]
