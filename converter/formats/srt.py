"""SubRip subtitles (`.srt`): StarCraft: Remastered's cinematics' (`locales/<locale>/Assets/Smk/<name>.srt`, UTF-8
with a byte order mark, CRLF or LF lines). Each cue is its number, `HH:MM:SS,mmm --> HH:MM:SS,mmm` and one or more
lines of text, cues apart by a blank line. A cue with no text shows nothing and is left out (frFR's terran1 has one)."""
from __future__ import annotations

import re
import logging
from dataclasses import dataclass

from ..errors import ConverterError

log = logging.getLogger(__name__)
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
    without its number or times, or a cue that ends before it starts."""
    try:
        text = data.decode("utf-8-sig")
    except UnicodeDecodeError as e:
        raise SrtError(f"not UTF-8: {e}") from e
    cues = []
    for block in re.split(r"\n[ \t]*\n", text.replace("\r\n", "\n").replace("\r", "\n").strip()):
        if not block.strip():
            continue
        lines = [line.rstrip() for line in block.strip("\n").split("\n")]  # a run of blank lines splits off blank edges
        if len(lines) < 2 or not lines[0].strip().isdigit():
            raise SrtError(f"block {lines[0]!r}: a number and the times are needed")
        times = TIMES.match(lines[1].strip())
        if not times:
            raise SrtError(f"block {lines[0]}: times {lines[1]!r}")
        start, end = _ms(*times.groups()[:4]), _ms(*times.groups()[4:])
        if end < start:
            raise SrtError(f"block {lines[0]}: ends before it starts")
        body = "\n".join(line for line in lines[2:]).strip("\n")
        if not body.strip():
            log.info("srt: cue %s (%s) has no text and is left out", lines[0], lines[1].strip())
            continue
        cues.append(Cue(start, end, body))
    return sorted(cues, key=lambda c: c.start_ms)
