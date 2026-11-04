"""SubRip subtitles (`.srt`): StarCraft: Remastered's cinematics' (`locales/<locale>/Assets/Smk/<name>.srt`, UTF-8
with a byte order mark, CRLF or LF lines). Each cue is its number, `HH:MM:SS,mmm --> HH:MM:SS,mmm` and one or more
lines of text, cues apart by a blank line."""
from __future__ import annotations

import re
from dataclasses import dataclass

from ..errors import ConverterError

TIMES = re.compile(r"^(\d+):(\d\d):(\d\d)[,.](\d{3})\s*-->\s*(\d+):(\d\d):(\d\d)[,.](\d{3})\s*$")


class SrtError(ConverterError, ValueError):
    """A subtitles file that does not parse."""


@dataclass(frozen=True)
class Cue:
    start_ms: int
    end_ms: int
    text: str  # its lines joined by "\n"


def _ms(h: str, m: str, s: str, ms: str) -> int:
    if int(m) > 59 or int(s) > 59:
        raise SrtError(f"time {h}:{m}:{s},{ms}")
    return ((int(h) * 60 + int(m)) * 60 + int(s)) * 1000 + int(ms)


def parse(data: bytes) -> list[Cue]:
    """The cues by their start (a cue's order kept among equal starts); SrtError for text that is not UTF-8, a block
    without its number, times or text, or a cue that ends before it starts."""
    try:
        text = data.decode("utf-8-sig")
    except UnicodeDecodeError as e:
        raise SrtError(f"not UTF-8: {e}") from e
    cues = []
    for block in re.split(r"\n[ \t]*\n", text.replace("\r\n", "\n").replace("\r", "\n").strip()):
        if not block.strip():
            continue
        lines = [line.rstrip() for line in block.split("\n")]
        if len(lines) < 3 or not lines[0].strip().isdigit():
            raise SrtError(f"block {lines[0]!r}: a number, the times and text are needed")
        times = TIMES.match(lines[1].strip())
        if not times:
            raise SrtError(f"block {lines[0]}: times {lines[1]!r}")
        start, end = _ms(*times.groups()[:4]), _ms(*times.groups()[4:])
        if end < start:
            raise SrtError(f"block {lines[0]}: ends before it starts")
        body = "\n".join(line for line in lines[2:]).strip("\n")
        if not body.strip():
            raise SrtError(f"block {lines[0]}: no text")
        cues.append(Cue(start, end, body))
    return sorted(cues, key=lambda c: c.start_ms)
