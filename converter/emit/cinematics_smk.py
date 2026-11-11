"""The 1.17 install's cinematics for the optional player (ADR 0015, the classic source): the SD set of the pack
(emit/cinematics.py) from the original's Smacker movies.

Each of the 28 movies of the table (`smk\\<name>.smk`) is the effective copy of the profile's archives (mpq/profile.py
`Resolver`: `blizzard` from StarDat.mpq, the others from StarCraft.mpq and BroodWar.mpq), checked against the size and
SHA-256 that `defs/campaign/cinematics.json` records of the reference install (`smk`). FFmpeg decodes it (its
smackvideo frames equal smackw32's: docs/research/evidence/smk-decoder-equivalence.json) and encodes it as
StarCraft: Remastered's SD movies are:
- the video as VP9 profile 1, 4:4:4 sRGB (the palette's colours looked up, without loss) at the movie's own size
  (320 x 308, which the player shows 640 x 308; the briefings' 640 x 308) and its exact frame rate (the header's
  -6666: 50000/3333), one key frame each KEY_INTERVAL frames;
- the sound through a WAV into Ogg Vorbis by oggenc with a stream serial of the movie's name (emit/sound.py), not by
  FFmpeg's Vorbis encoders (FFmpeg 6.1's begins the sound 46 ms before the video).
The 1.17 install has no subtitles.

VIDEO_CRF, VIDEO_CPU_USED and OGG_QUALITY keep Remastered's SD set's size and quality: on its movies whose frames match
the originals' one for one (blizzard, Terran2, Zerg3, Protoss3, the briefings) the PSNR against the decoded SMK is
within 0.6 dB of Remastered's at 5 to 9 % more bytes (docs/adr/0015-video-decoder.md, the classic source).

Every copy is read and checked before any is encoded, so a copy unlike the reference install's stops the conversion at
once; the archives are read in the calling thread only. The copies go to files (beside the cache when there is one),
from which the movies are encoded side by side, each in its own FFmpeg; libvpx encodes each movie in one thread, so the
same tools give the same bytes. The IVF and the Ogg are kept under the output's cache/cinematics/ by the movie's SHA-256
and the settings (the Ogg also by its serial), so a conversion run again encodes nothing.
"""
from __future__ import annotations

import json
import logging
import os
import re
import shutil
import struct
import subprocess
import tempfile
import time
from collections.abc import Iterator
from concurrent.futures import ThreadPoolExecutor
from fractions import Fraction
from pathlib import Path
from typing import Protocol

from ..formats import ffmpeg, ivf, kpw, smk, wav
from ..mpq.profile import sha256_file
from ..mpq.stormlib import MpqFileNotFound
from ..provenance import DEFS_ROOT
from . import sound
from .cinematics import (PTS_DRIFT_FRAMES, CinematicsError, CinematicsToolsError, Movie, check_times, frame_rate,
                         table)
from .files import cached

log = logging.getLogger(__name__)

SMK_PATH = "smk\\{name}.smk"
CACHE = Path("cache") / "cinematics"  # under the output directory
VIDEO_ENCODER = "libvpx-vp9"
VIDEO_CRF = "32"
VIDEO_CPU_USED = "2"
KEY_INTERVAL = "128"  # libvpx's own longest distance between key frames, named so that it stays
VIDEO_ARGS = ("-an", "-c:v", VIDEO_ENCODER, "-pix_fmt", "gbrp", "-crf", VIDEO_CRF, "-b:v", "0", "-deadline", "good",
              "-cpu-used", VIDEO_CPU_USED, "-g", KEY_INTERVAL, "-threads", "1")
SOUND_ARGS = ("-vn", "-c:a", "pcm_s16le")
OGG_QUALITY = "3"  # oggenc -q of the movies' sound, measured against Remastered's SD set (not sound.OGG_QUALITY's)
BITEXACT = ("-fflags", "+bitexact", "-flags", "+bitexact")
SETTINGS = " ".join((*VIDEO_ARGS, *SOUND_ARGS, "oggenc -q", OGG_QUALITY)).encode()


class Source(Protocol):
    def read_expected(self, name: str, size: int, sha256: str) -> tuple[str, bytes]:
        """(archive name, bytes) of the effective copy, checked against the size and SHA-256 (mpq/profile.py)."""


def expected() -> dict[str, tuple[int, str]]:
    """Each movie's size and SHA-256 in the reference install (defs/campaign/cinematics.json `smk`)."""
    raw = json.loads((DEFS_ROOT / "campaign" / "cinematics.json").read_text())["smk"]
    return {name: (size, sha256) for name, (size, sha256) in raw.items()}


def require() -> None:
    """CinematicsToolsError unless FFmpeg (with its libvpx-vp9 encoder), ffprobe and oggenc are there."""
    ffmpeg.require(CinematicsToolsError, "transcode the 1.17 install's cinematics")
    if shutil.which("oggenc") is None:
        raise CinematicsToolsError("oggenc is needed to encode the cinematics' sound (vorbis-tools)")
    encoders = subprocess.run(["ffmpeg", "-hide_banner", "-encoders"], capture_output=True, text=True, check=False)
    if not re.search(rf"\s{re.escape(VIDEO_ENCODER)}\s", encoders.stdout):
        raise CinematicsToolsError(f"FFmpeg has no {VIDEO_ENCODER} encoder: the cinematics need an FFmpeg built with "
                                   "libvpx")


def serial(name: str) -> int:
    """The Ogg stream serial of a movie's sound."""
    return sound.serial_of(name.lower())


def _ffmpeg(src: Path, args: tuple[str, ...], out: Path, what: str) -> bytes:
    run = subprocess.run(["ffmpeg", "-v", "error", "-y", "-i", str(src), *args, *BITEXACT, str(out)],
                         capture_output=True, check=False)
    if run.returncode or not out.is_file():
        raise CinematicsError(f"ffmpeg ({what}): {run.stderr.decode(errors='replace').strip()}")
    return out.read_bytes()


def _is_ivf(data: bytes) -> bool:
    try:
        return bool(ivf.parse(data).frames)
    except ivf.IvfError:
        return False


def transcode(name: str, src: Path, cache: Path | None) -> Movie:
    """A Smacker movie's file as the pack's clip: its VP9 packets, its exact frame rate and its Ogg Vorbis (b"":
    silent). CinematicsError for a movie the player could not keep in step: a ring frame, another count of frames than
    the header's, a sound of another length than the video."""
    with src.open("rb") as f:
        head = f.read(smk.HEADER_SIZE)
    if head[:4] == kpw.MAGIC:
        raise CinematicsError(f"{name}: not a Smacker movie that plays once")
    try:
        h = smk.header(head)
    except smk.SmkError as e:
        raise CinematicsError(f"{name}: {e}") from e
    if h.ring_frame:
        raise CinematicsError(f"{name}: not a Smacker movie that plays once")
    rate = frame_rate(h.rate)
    length = h.frames / rate
    ogg_serial = serial(name)
    key = bytes.fromhex(sha256_file(src)) + SETTINGS
    with tempfile.TemporaryDirectory() as tmp:

        def encode_sound() -> bytes:
            pcm = _ffmpeg(src, SOUND_ARGS, Path(tmp) / "sound.wav", "sound")
            info = wav.read_info(pcm)
            heard = Fraction(info.data_length, info.avg_bytes_per_sec)
            if abs(heard - length) * rate > PTS_DRIFT_FRAMES:
                raise CinematicsError(f"{name}: {float(heard):.3f} s of sound to {float(length):.3f} s of video")
            try:
                return sound.ogg_bytes(pcm, ogg_serial, OGG_QUALITY)
            except sound.SoundError as e:
                raise CinematicsError(f"{name}: {e}") from e

        video = cached(cache, b"video" + key, ".ivf", lambda: _ffmpeg(src, VIDEO_ARGS, Path(tmp) / "v.ivf", "video"),
                       _is_ivf)
        silent = ffmpeg.probe(str(src), "a:0", ("codec_name",), CinematicsError) is None
        ogg = b"" if silent else cached(cache, b"sound" + key + struct.pack("<I", ogg_serial), ".ogg", encode_sound,
                                        lambda d: d[:4] == b"OggS")
    packets = ivf.parse(video)
    if (packets.width, packets.height) != (h.width, h.height):
        raise CinematicsError(f"{name}: encoded {packets.width}x{packets.height}, the movie is {h.width}x{h.height}")
    if len(packets.frames) != h.frames or not packets.frames[0].key:
        raise CinematicsError(f"{name}: {len(packets.frames)} frames encoded of {h.frames}, or no key frame first")
    check_times(packets, rate)
    return Movie(packets, rate, ogg)


def _copy(source: Source, name: str, size: int, sha256: str, folder: Path) -> tuple[str, Path] | None:
    """(archive name, file) of a movie's checked copy written into folder, None when no archive holds it; its bytes
    are let go on return."""
    path = SMK_PATH.format(name=name)
    try:
        archive, data = source.read_expected(path, size, sha256)
    except MpqFileNotFound:
        log.warning("cinematics sd: %s is in none of the archives", path)
        return None
    copy = folder / f"{name}.smk"
    copy.write_bytes(data)
    return archive, copy


def movies(source: Source, cache: Path | None) -> Iterator[Movie | None]:
    """Each movie of the table in its order, None for one no archive holds. All the copies are read and checked first
    (a copy unlike the reference install's: ProfileError before any encoding), then encoded side by side. A caller
    that may stop before the end closes the iterator (contextlib.closing): that cancels what is not encoded yet and
    waits for the encodings under way."""
    require()
    known = expected()
    names = table()[0]
    if cache is not None:
        cache.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".copies-", dir=cache) as tmp:  # beside the cache: not a RAM-backed /tmp
        copies = [_copy(source, name, *known[name], Path(tmp)) for name in names]

        def one(name: str, copy: tuple[str, Path] | None) -> Movie | None:
            if copy is None:
                return None
            start = time.monotonic()
            movie = transcode(name, copy[1], cache)
            log.info("cinematics sd: %s from %s in %.1f s", name, copy[0], time.monotonic() - start)
            return movie

        pool = ThreadPoolExecutor(max_workers=os.cpu_count())  # each FFmpeg one thread of encoding
        try:
            yield from pool.map(one, names, copies)
        finally:
            pool.shutdown(wait=True, cancel_futures=True)
