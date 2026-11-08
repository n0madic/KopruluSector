"""StarCraft: Remastered's cinematics for the optional player (ADR 0015): `generated/video/cinematics-<sd|hd2>.kpd`
(`CINE`).

The 28 movies of the original's `smk\\` table (`defs/campaign/cinematics.json`) as Remastered ships them (`SD/smk/` or
`HD2/Smk/`, WebM: VP9 and Vorbis), each split by FFmpeg without re-encoding into its VP9 packets (IVF, formats/ivf.py)
and its Ogg Vorbis stream. Sections:
- HEAD: u8 variant (0 SD, 1 HD2), u8 0, u16 clips, u16 preludes;
- CLIP: per clip (CLIP_FORMAT) the lower-case name (`smk\\<name>`), u8 id (the original's table index), u8 0, u16 0, u16
  width, height of the coded frames, u16 width, height the frame is shown at (a movie coded 320 wide is shown 640
  wide, a pixel of 2:1), u32 frame rate numerator and denominator, u32 frames, u32 offset and
  size of the packets in DATA, u32 first frame in FRMS, u32 offset and size of the Ogg in DATA (size 0: no sound);
- FRMS: per frame, all clips in turn, a u32: the packet's size, bit 31 set for a key frame; a clip's packets follow
  each other from its offset in DATA;
- PREL: per prelude u8 cinematic, u8 prelude (PlayCinematicWithPrelude's table, 0x4edad8);
- SUBS (schema 2, only when the movies have subtitles): Remastered's (`locales/<locale>/Assets/Smk/<name>.srt`, not
  the original's): the locale (4 bytes, NUL-padded), u32 cues, u32 text bytes; per clip (CLIP's order) u32 first cue,
  u32 cues; per cue u32 start and end in ms, u32 offset and size of its UTF-8 text (lines apart by "\n") in the text;
  then the text;
- DATA: each clip's packets and its Ogg, each from a 4-byte boundary.
"""
from __future__ import annotations

import json
import logging
import re
import struct
import subprocess
import tempfile
from fractions import Fraction
from pathlib import Path

from .. import CONVERTER_VERSION
from ..casc.storage import CascError, CascStorage
from ..errors import ConverterError
from ..formats import ffmpeg, ivf, srt
from ..kpf.container import Container
from ..provenance import DEFS_ROOT
from .files import write_parts_if_changed

log = logging.getLogger(__name__)

CINE_KIND = b"CINE"
SCHEMA = 2
VARIANTS = {"sd": ("SD/smk/", 0), "hd2": ("HD2/Smk/", 1)}
NAME_SIZE = 64
CLIP_FORMAT = struct.Struct(f"<{NAME_SIZE}sBBHHHHHIIIIIIII")
KEY = 0x80000000
CODED_WIDTH, SHOWN_WIDTH = 320, 640  # an SD movie coded 320 wide is shown 640 wide
OUT_DIR = "video"
RATE_TERM_MAX = 1_000_000  # a frame rate's numerator and denominator the player takes (KP_CINE_MAX_RATE_TERM)
# How far a packet's time may stray from the constant rate, in frames: past half a frame the player would show another
# frame than the WebM (the real SD and HD2 movies stray at most 0.015).
PTS_DRIFT_FRAMES = Fraction(1, 2)
SUBTITLES = "locales/{locale}/Assets/Smk/{name}.srt"
SUBTITLES_AUTO, SUBTITLES_NONE, SUBTITLES_FALLBACK = "auto", "none", "enUS"
LOCALE = re.compile(r"^[a-z]{2}[A-Z]{2}$")
CUES_MAX = 128  # a clip's cues the player keeps (KP_CINE_CUES)
CUE_TEXT_MAX = 16384  # and their text's bytes (KP_CINE_CUE_TEXT)
CUE_FORMAT = struct.Struct("<IIII")
CUE_MS_MAX = 0xFFFFFFFF


class CinematicsError(ConverterError, ValueError):
    """A cinematic cannot be read or split."""


def table() -> tuple[list[str], list[tuple[int, int]]]:
    """(the 28 movie names, the (cinematic, prelude) pairs) of defs/campaign/cinematics.json."""
    raw = json.loads((DEFS_ROOT / "campaign" / "cinematics.json").read_text())
    return raw["names"], [(c, p) for c, p in raw["preludes"]]


def shown_size(width: int, height: int) -> tuple[int, int]:
    """The size a movie's frames are shown at: a movie coded 320 wide (the SD movies, and the one Remastered's HD2
    set takes from them, `blizzard`) is shown 640 wide, a pixel of 2:1; the others as coded."""
    if width == CODED_WIDTH:
        return SHOWN_WIDTH, height
    return width, height


def frame_rate(text: str) -> Fraction:
    """ffprobe's frame rate ("15/1"); CinematicsError for none (0/0) or terms the player refuses."""
    try:
        rate = Fraction(text)
    except (ValueError, ZeroDivisionError) as e:
        raise CinematicsError(f"frame rate {text!r}") from e
    if rate <= 0 or rate.numerator > RATE_TERM_MAX or rate.denominator > RATE_TERM_MAX:
        raise CinematicsError(f"frame rate {text!r}")
    return rate


def start_offset(video_start: str, audio_start: str, rate: Fraction) -> None:
    """CinematicsError when the video begins more than PTS_DRIFT_FRAMES of a frame before or after the sound: the
    pack keeps no offset, the player starts both at 0 (the real movies' video begins 3 to 12 ms after the sound)."""
    try:
        offset = Fraction(video_start) - Fraction(audio_start)
    except (ValueError, ZeroDivisionError) as e:
        raise CinematicsError(f"start times {video_start!r} and {audio_start!r}") from e
    if abs(offset) * rate > PTS_DRIFT_FRAMES:
        raise CinematicsError(f"the video begins {float(offset) * 1000:+.0f} ms from the sound, past half a frame")


def probe(path: str) -> tuple[int, int, Fraction, bool]:
    """(width, height, frame rate, whether it has sound) of a WebM: VP9 video and, if any, Vorbis sound that begins
    with the video."""
    video = ffmpeg.probe(path, "v:0", ("codec_name", "width", "height", "r_frame_rate", "start_time"), CinematicsError)
    if video is None or video[0] != "vp9":
        raise CinematicsError(f"the video is {video[0] if video else 'missing'}, not VP9")
    rate = frame_rate(video[3])
    audio = ffmpeg.probe(path, "a:0", ("codec_name", "start_time"), CinematicsError)
    if audio is not None:
        if audio[0] != "vorbis":
            raise CinematicsError(f"the sound is {audio[0]}, not Vorbis")
        start_offset(video[4], audio[1], rate)
    return int(video[1]), int(video[2]), rate, audio is not None


def check_times(packets: ivf.Ivf, rate: Fraction) -> None:
    """CinematicsError when a packet's presentation time strays more than PTS_DRIFT_FRAMES from where the player's
    constant rate shows it (the pack keeps the rate, not the times)."""
    start = packets.frames[0].pts
    for n, frame in enumerate(packets.frames):
        drift = (frame.pts - start) * packets.timebase * rate - n
        if abs(drift) > PTS_DRIFT_FRAMES:
            raise CinematicsError(f"frame {n} is {float(drift):+.2f} frames from its time at {rate} fps")


def split(webm: bytes) -> tuple[ivf.Ivf, Fraction, bytes]:
    """(the VP9 packets, the frame rate, the Ogg Vorbis stream or b"" when it has no sound) of a WebM, copied, not
    re-encoded; the same bytes each time (bitexact: Ogg's stream serial is otherwise random)."""
    ffmpeg.require(CinematicsError, "split StarCraft: Remastered's WebM")
    with tempfile.TemporaryDirectory() as tmp:
        src, video, audio = (Path(tmp) / n for n in ("in.webm", "v.ivf", "a.ogg"))
        src.write_bytes(webm)
        width, height, rate, sound = probe(str(src))
        runs = [(["-an", "-c:v", "copy", "-f", "ivf"], video)]
        if sound:
            runs.append((["-vn", "-c:a", "copy", "-f", "ogg"], audio))
        for args, out in runs:
            run = subprocess.run(["ffmpeg", "-v", "error", "-y", "-i", str(src), *args, "-fflags", "+bitexact", str(out)],
                                 capture_output=True, check=False)
            if run.returncode:
                raise CinematicsError(f"ffmpeg ({out.suffix}): {run.stderr.decode(errors='replace').strip()}")
        packets = ivf.parse(video.read_bytes())
        if (packets.width, packets.height) != (width, height) or not packets.frames or not packets.frames[0].key:
            raise CinematicsError("the IVF does not match the WebM's video")
        check_times(packets, rate)
        return packets, rate, audio.read_bytes() if sound else b""


def text_locale(storage: CascStorage) -> str | None:
    """The install's text locale: the locale of .build.info's Tags group that has `text?` (Remastered downloads that
    locale's files), or None."""
    tags = getattr(storage, "build_info", {}).get("Tags", "")
    for group in tags.split(":"):
        words = group.split()
        if "text?" in words:
            for word in words:
                if LOCALE.match(word):
                    return word
    return None


def read_subtitles(storage: CascStorage, locale: str, names: list[str], required: bool) -> dict[str, list[srt.Cue]]:
    """The cues of each movie whose subtitles the locale has; one the storage names but does not hold is a
    CinematicsError when required, else left out."""
    found = {}
    for name in names:
        path = SUBTITLES.format(locale=locale, name=name.lower())
        if not storage.has(path):
            continue
        try:
            data = storage.read(path)
        except CascError as e:
            if required:
                raise CinematicsError(f"{path}: {e}") from e
            log.warning("cinematics: %s is not in the local storage", path)
            continue
        try:
            cues = srt.parse(data)
        except srt.SrtError as e:
            raise CinematicsError(f"{path}: {e}") from e
        if len(cues) > CUES_MAX or sum(len(c.text.encode()) for c in cues) > CUE_TEXT_MAX:
            raise CinematicsError(f"{path}: more than {CUES_MAX} cues or {CUE_TEXT_MAX} bytes of text")
        if any(c.end_ms > CUE_MS_MAX for c in cues):
            raise CinematicsError(f"{path}: a cue past {CUE_MS_MAX} ms (SUBS's times are 32-bit)")
        found[name] = cues
    return found


def subtitles(storage: CascStorage, names: list[str], choice: str) -> tuple[str, dict[str, list[srt.Cue]]]:
    """(the locale, each movie's cues) of the subtitles choice: none; a locale, whose files must be in the local
    storage; or auto: the install's text locale, else enUS, the first whose files the storage holds."""
    if choice == SUBTITLES_NONE:
        return "", {}
    if choice != SUBTITLES_AUTO:
        if not LOCALE.match(choice):
            raise CinematicsError(f"subtitles {choice!r} is not auto, none or a locale such as enUS")
        found = read_subtitles(storage, choice, names, required=True)
        if not found:
            raise CinematicsError(f"subtitles {choice}: the storage names no {SUBTITLES.format(locale=choice, name='*')}")
        return choice, found
    for locale in dict.fromkeys(filter(None, (text_locale(storage), SUBTITLES_FALLBACK))):
        found = read_subtitles(storage, locale, names, required=False)
        if found:
            return locale, found
    log.warning("cinematics: no subtitles in the local storage")
    return "", {}


def subtitles_section(locale: str, cues: list[list[srt.Cue]]) -> bytes:
    """SUBS of the clips' cues, in CLIP's order."""
    table, records, text = bytearray(), bytearray(), bytearray()
    total = 0
    for clip in cues:
        table += struct.pack("<II", total, len(clip))
        for cue in clip:
            body = cue.text.encode("utf-8")
            records += CUE_FORMAT.pack(cue.start_ms, cue.end_ms, len(text), len(body))
            text += body
        total += len(clip)
    return locale.encode("ascii").ljust(4, b"\0") + struct.pack("<II", total, len(text)) + table + records + text


def generate(storage: CascStorage, out: Path, variant: str, profile_id: str, source_hash: bytes,
             subs: tuple[str, dict[str, list[srt.Cue]]] | None = None) -> dict:
    """cinematics-<variant>.kpd under out/video; the movies found, the subtitles' locale and the pack's path. subs:
    subtitles()'s (read once for all the variants), by default the install's (auto)."""
    prefix, code = VARIANTS[variant]
    names, preludes = table()
    locale, subs_of = subs if subs is not None else subtitles(storage, names, SUBTITLES_AUTO)
    clip_cues: list[list[srt.Cue]] = []
    index, frames, body = bytearray(), bytearray(), []  # body: DATA's pieces, joined once
    size = found = 0

    def put(piece: bytes) -> int:
        """Appends a piece and its padding to a 4-byte boundary to DATA; its offset."""
        nonlocal size
        at = size
        body.append(piece)
        body.append(bytes(-len(piece) % 4))
        size += len(piece) + len(body[-1])
        return at

    for number, name in enumerate(names):
        webm_name = f"{prefix}{name}.webm"
        if not storage.has(webm_name):
            log.warning("cinematics %s: %s is not in the storage", variant, webm_name)
            continue
        packets, rate, ogg = split(storage.read(webm_name))
        coded = (packets.width, packets.height)
        shown = shown_size(*coded)
        key = f"smk\\{name}".lower().encode("ascii")
        first = len(frames) // 4
        for frame in packets.frames:
            if not 0 < len(frame.data) < KEY:  # libvpx refuses an empty packet (the player refuses its table too)
                raise CinematicsError(f"{name}: packet of {len(frame.data)} bytes")
            frames += struct.pack("<I", len(frame.data) | (KEY if frame.key else 0))
        video = b"".join(frame.data for frame in packets.frames)
        video_at, audio_at = put(video), put(ogg)
        if size >= 1 << 32:  # early: the container refuses a file past 32 bits too (its header, tables and DATA)
            raise CinematicsError("the pack outgrew its 32-bit offsets")
        index += CLIP_FORMAT.pack(key, number, 0, 0, *coded, *shown, rate.numerator, rate.denominator,
                                  len(packets.frames), video_at, len(video), first, audio_at, len(ogg))
        found += 1
        clip_cues.append(subs_of.get(name, []))
        log.info("cinematics %s: %s %dx%d (shown %dx%d) %d frames at %s, %d bytes of video, %d of sound", variant, name,
                 *coded, *shown, len(packets.frames), rate, len(video), len(ogg))
    if not found:
        raise CinematicsError(f"no {variant} cinematic in the storage")
    c = Container(CINE_KIND, SCHEMA, CONVERTER_VERSION, profile_id, source_hash=source_hash)
    c.add("HEAD", struct.pack("<BBHH", code, 0, found, len(preludes)))
    c.add("CLIP", bytes(index))
    c.add("FRMS", bytes(frames))
    c.add("PREL", b"".join(struct.pack("<BB", cinematic, prelude) for cinematic, prelude in preludes))
    if any(clip_cues):
        c.add("SUBS", subtitles_section(locale, clip_cues))
    data = b"".join(body)
    body.clear()  # the pieces go before the container is written: one copy of the movies in memory
    c.add("DATA", data)
    del data
    path = out / OUT_DIR / f"cinematics-{variant}.kpd"
    write_parts_if_changed(path, c.to_parts())  # compared and written piece by piece, never joined
    log.info("cinematics %s: %d of %d movies, %d bytes, %d with subtitles (%s)", variant, found, len(names), size,
             sum(1 for c in clip_cues if c), locale or "none")
    return {"clips": found, "movies": len(names), "bytes": size, "path": str(path),
            "subtitles": locale if any(clip_cues) else ""}
