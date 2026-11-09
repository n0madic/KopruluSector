"""The 1.17 install's cinematics (converter/emit/cinematics_smk.py): synthetic Smacker movies (written here: one fill
code, a palette record and a PCM sound chunk in each frame) transcoded by FFmpeg and oggenc (skipped without them);
with KOPRULU_ORIGINAL, the real install's 28 movies checked against defs/campaign/cinematics.json."""
from __future__ import annotations

import os
import struct
import subprocess
import tempfile
import threading
import unittest
from fractions import Fraction
from pathlib import Path
from unittest import mock

from converter.__main__ import main
from converter.emit import cinematics, cinematics_smk, sound
from converter.formats import ivf, smk
from converter.kpf.container import Container, parse
from converter.mpq.profile import Profile, ProfileError, Resolver
from converter.mpq.stormlib import MpqFileNotFound

ORIGINAL = os.environ.get("KOPRULU_ORIGINAL")
WIDTH, HEIGHT = 64, 48
CINEMATIC_INTERVAL = -6666  # the cinematics' frame rate field: 66.66 ms
SOUND_RATE = 22050
FILL_INDEX = 1  # the palette index every frame is filled with
ESCAPES = (0x1234, 0x2345, 0x3456)  # the big tree's escape values: none is the fill code
RING_FLAG = 1


def _bits(value: int, count: int) -> list[int]:
    return [(value >> i) & 1 for i in range(count)]


def _pack_bits(bits: list[int]) -> bytes:
    """Smacker's bit stream: each byte from its lowest bit."""
    out = bytearray((len(bits) + 7) // 8)
    for i, bit in enumerate(bits):
        out[i // 8] |= bit << (i % 8)
    return bytes(out)


def smk_movie(colours: list[tuple[int, int, int]], *, sound: bool = True, sound_frames: int | None = None,
              flags: int = 0) -> bytes:
    """An SMK2 of WIDTH x HEIGHT at the cinematics' rate whose frame n is all palette index FILL_INDEX, set by that
    frame's palette record to colours[n] (6-bit R, G, B); with sound, a 22,050 Hz 16-bit stereo PCM track lasting
    `sound_frames` frames (by default all). The header trees: MMAP, MCLR and FULL absent, TYPE one leaf, the fill
    code (block type 3, the longest run, the colour in its high byte), so the frames' video needs no bits."""
    fill = 3 | (0x3F << 2) | (FILL_INDEX << 8)
    bits = [0, 0, 0, 1]
    for byte in (fill & 0xFF, fill >> 8):  # the low and high byte trees: present, one leaf, the closing bit
        bits += [1, 0] + _bits(byte, 8) + [0]
    for escape in ESCAPES:
        bits += _bits(escape, 16)
    bits += [0, 0]  # the big tree's one leaf, the closing bit
    trees = _pack_bits(bits)
    trees += bytes(-len(trees) % 4)
    frames = len(colours)
    heard = frames if sound_frames is None else sound_frames
    samples = round(heard * Fraction(-CINEMATIC_INTERVAL, 100000) * SOUND_RATE)
    rates = [SOUND_RATE | (0x40 | 0x20 | 0x10) << 24 if sound else 0] + [0] * 6  # sound, 16 bits, stereo; PCM
    bodies, types = [], []
    for n, (r, g, b) in enumerate(colours):
        palette = bytes((0x80, r, g, b, 0xFF, 0xFD))  # skip entry 0, entry 1 new, skip 128, skip 126
        record = bytes((0,)) + palette
        record += bytes(-len(record) % 4)
        body = bytes((len(record) // 4,)) + record[1:]
        frame_type = 1
        if sound:
            first, last = n * samples // frames, (n + 1) * samples // frames
            pcm = b"".join(struct.pack("<hh", 600 * ((i % 40) - 20), 0) for i in range(first, last))
            body += struct.pack("<I", 4 + len(pcm)) + pcm
            frame_type |= 2
        body += bytes(4)  # the frame's video
        bodies.append(body + bytes(-len(body) % 4))
        types.append(frame_type)
    head = b"SMK2" + struct.pack("<IIIiI", WIDTH, HEIGHT, frames, CINEMATIC_INTERVAL, flags) + bytes(28)
    head += struct.pack("<5I", len(trees), 0, 0, 0, 4) + struct.pack("<7I", *rates) + bytes(4)
    assert len(head) == smk.HEADER_SIZE
    sizes = b"".join(struct.pack("<I", len(body) | (n == 0)) for n, body in enumerate(bodies))
    return head + sizes + bytes(types) + trees + b"".join(bodies)


def colours(frames: int) -> list[tuple[int, int, int]]:
    return [((7 * n) % 64, 63 - (5 * n) % 64, (11 * n + 20) % 64) for n in range(frames)]


def expanded(c: int) -> int:
    """A palette record's 6-bit value as 8 bits (Smacker's table: the top bits repeated)."""
    return c * 4 + c // 16


def transcode(name: str, data: bytes, cache: Path | None = None) -> cinematics.Movie:
    """cinematics_smk.transcode of a movie's bytes, through a file as movies() hands them."""
    with tempfile.TemporaryDirectory() as tmp:
        src = Path(tmp) / f"{name}.smk"
        src.write_bytes(data)
        return cinematics_smk.transcode(name, src, cache)


def ogg_serial(ogg: bytes) -> int:
    """The stream serial of an Ogg's first page."""
    return struct.unpack_from("<I", ogg, 14)[0]


def pack_head(source_hash: bytes, *, schema: int = cinematics.SCHEMA, kind: bytes = cinematics.CINE_KIND) -> bytes:
    """A pack of the source `source_hash` with a header and no clip: what remastered_pack reads."""
    c = Container(kind, schema, 1, "bw-1.17.0.1", source_hash=source_hash)
    c.add("HEAD", b"")
    return c.to_bytes()


def encoders() -> bool:
    try:
        cinematics_smk.require()
    except cinematics.CinematicsError:
        return False
    return True


needs_encoders = unittest.skipUnless(encoders(), "FFmpeg with libvpx-vp9, ffprobe and oggenc are needed")


def decoded_rgb(packets: ivf.Ivf) -> list[bytes]:
    """FFmpeg's RGB frames of the packets."""
    data = ivf.HEADER.pack(ivf.MAGIC, 0, ivf.HEADER.size, packets.fourcc, packets.width, packets.height,
                           packets.timebase.denominator, packets.timebase.numerator, len(packets.frames))
    data += b"".join(ivf.FRAME.pack(len(f.data), f.pts) + f.data for f in packets.frames)
    run = subprocess.run(["ffmpeg", "-v", "error", "-f", "ivf", "-i", "pipe:", "-f", "rawvideo", "-pix_fmt", "rgb24",
                          "pipe:"], input=data, capture_output=True, check=True)
    size = packets.width * packets.height * 3
    return [run.stdout[at:at + size] for at in range(0, len(run.stdout), size)]


def sound_seconds(ogg: bytes) -> float:
    run = subprocess.run(["ffmpeg", "-v", "error", "-i", "pipe:", "-f", "s16le", "pipe:"], input=ogg,
                         capture_output=True, check=True)
    return len(run.stdout) / 4 / SOUND_RATE


class HeaderTest(unittest.TestCase):
    def test_frame_rate_is_exact(self):
        assert smk.header(smk_movie(colours(2))).rate == Fraction(50000, 3333)
        head = bytearray(smk_movie(colours(2)))
        for interval, rate in ((100, Fraction(10)), (0, Fraction(10)), (-10000, Fraction(10)), (40, Fraction(25))):
            struct.pack_into("<i", head, 16, interval)
            h = smk.header(bytes(head))
            assert h.rate == rate and h.ms_per_frame == 1000 / rate
            assert cinematics.frame_rate(h.rate) == rate  # checked as it is, no text between

    def test_the_reference_copies_cover_the_table(self):
        known = cinematics_smk.expected()
        assert list(known) == cinematics.table()[0]
        assert all(size > smk.HEADER_SIZE and len(sha) == 64 for size, sha in known.values())


@needs_encoders
class TranscodeTest(unittest.TestCase):
    FRAMES = 12

    def test_keeps_the_size_the_frames_the_rate_and_the_colours(self):
        movie = transcode("Terran1", smk_movie(colours(self.FRAMES)))
        packets = movie.packets
        assert (packets.width, packets.height) == (WIDTH, HEIGHT) and len(packets.frames) == self.FRAMES
        assert movie.rate == Fraction(50000, 3333) and packets.frames[0].key
        for n, (frame, colour) in enumerate(zip(decoded_rgb(packets), colours(self.FRAMES), strict=True)):
            want = [expanded(c) for c in colour]
            got = [sum(frame[i::3]) / (WIDTH * HEIGHT) for i in range(3)]
            assert all(abs(g - w) <= 3 for g, w in zip(got, want)), (n, got, want)

    def test_the_sound_lasts_as_the_video(self):
        movie = transcode("Terran1", smk_movie(colours(self.FRAMES)))
        assert movie.ogg[:4] == b"OggS" and ogg_serial(movie.ogg) == cinematics_smk.serial("Terran1")
        assert abs(sound_seconds(movie.ogg) - float(self.FRAMES / movie.rate)) < float(1 / movie.rate) / 2

    def test_a_silent_movie_has_no_sound(self):
        assert transcode("Terran1", smk_movie(colours(3), sound=False)).ogg == b""

    def test_the_same_bytes_each_time_and_from_the_cache(self):
        data = smk_movie(colours(self.FRAMES))
        first = transcode("Zerg1", data)
        again = transcode("Zerg1", data)
        assert again == first
        with tempfile.TemporaryDirectory() as tmp:
            cache = Path(tmp)
            assert transcode("Zerg1", data, cache) == first
            assert sorted(p.suffix for p in cache.iterdir()) == [".ivf", ".ogg"]
            with mock.patch.object(cinematics_smk, "_ffmpeg", side_effect=AssertionError("encoded again")):
                assert transcode("Zerg1", data, cache) == first

    def test_the_cache_keeps_each_names_sound(self):
        # the Ogg's stream serial is the movie's name: the same bytes under another name are not its sound
        data = smk_movie(colours(self.FRAMES))
        with tempfile.TemporaryDirectory() as tmp:
            first = transcode("Zerg1", data, Path(tmp))
            other = transcode("Zerg2", data, Path(tmp))
            assert len(list(Path(tmp).glob("*.ivf"))) == 1 and len(list(Path(tmp).glob("*.ogg"))) == 2
        assert other.packets == first.packets and other == transcode("Zerg2", data)
        assert (ogg_serial(first.ogg), ogg_serial(other.ogg)) == (cinematics_smk.serial("Zerg1"),
                                                                  cinematics_smk.serial("Zerg2"))

    def test_an_oggenc_failure_names_the_movie(self):
        with mock.patch.object(sound, "ogg_bytes", side_effect=sound.SoundError("oggenc: refused")), \
                self.assertRaisesRegex(cinematics.CinematicsError, "Terran1: oggenc: refused"):
            transcode("Terran1", smk_movie(colours(3)))

    def test_refuses_a_ring_movie_and_a_sound_of_another_length(self):
        with self.assertRaisesRegex(cinematics.CinematicsError, "plays once"):
            transcode("Terran1", smk_movie(colours(3), flags=RING_FLAG))
        with self.assertRaisesRegex(cinematics.CinematicsError, "of sound to"):
            transcode("Terran1", smk_movie(colours(self.FRAMES), sound_frames=self.FRAMES - 2))


class Install:
    """A Resolver's read_expected over a few movies by name; the others in none of the archives. Records the threads
    that read it."""

    def __init__(self, movies: dict[str, bytes], refused: str = ""):
        self.movies, self.refused, self.asked = movies, refused, []
        self.threads: set[threading.Thread] = set()

    def read_expected(self, name: str, size: int, sha256: str) -> tuple[str, bytes]:
        self.asked.append((name, size, sha256))
        self.threads.add(threading.current_thread())
        movie = name.removeprefix("smk\\").removesuffix(".smk")
        if movie == self.refused:
            raise ProfileError(f"{name} differs")
        if movie not in self.movies:
            raise MpqFileNotFound(name)
        return "StarCraft.mpq", self.movies[movie]


@needs_encoders
class MoviesTest(unittest.TestCase):
    def test_each_movie_checked_in_the_tables_order_into_the_sd_pack(self):
        install = Install({"blizzard": smk_movie(colours(4)), "Terran1": smk_movie(colours(6))})
        with tempfile.TemporaryDirectory() as tmp, self.assertLogs("converter.emit.cinematics_smk", "WARNING"):
            info = cinematics.generate(cinematics_smk.movies(install, None), Path(tmp), "sd", "bw-1.17.0.1",
                                       bytes(32))
            pack = parse(Path(info["path"]).read_bytes())
        known = cinematics_smk.expected()
        assert install.asked == [(f"smk\\{n}.smk", *known[n]) for n in cinematics.table()[0]]
        assert install.threads == {threading.main_thread()}  # the encodings never touch the archives
        assert info["clips"] == 2 and info["subtitles"] == ""
        assert all(s.fourcc != b"SUBS" for s in pack.sections)
        clip = pack.section("CLIP").payload
        for n, (number, frames) in enumerate(((0, 4), (6, 6))):
            fields = cinematics.CLIP_FORMAT.unpack_from(clip, n * cinematics.CLIP_FORMAT.size)
            assert fields[1] == number and fields[4:8] == (WIDTH, HEIGHT, WIDTH, HEIGHT)
            assert fields[8:11] == (50000, 3333, frames)

    def test_a_copy_unlike_the_reference_stops_before_any_encoding(self):
        install = Install({"blizzard": smk_movie(colours(2))}, refused="starXintr")  # late in the table
        with mock.patch.object(cinematics_smk, "transcode", side_effect=AssertionError("encoded")), \
                self.assertRaisesRegex(ProfileError, "starXintr"):
            list(cinematics_smk.movies(install, None))

    def test_closing_early_cancels_the_rest_and_removes_the_copies(self):
        install = Install({"blizzard": smk_movie(colours(2)), "Terran1": smk_movie(colours(3))})
        with tempfile.TemporaryDirectory() as tmp, self.assertLogs("converter.emit.cinematics_smk", "WARNING"):
            cache = Path(tmp)
            it = cinematics_smk.movies(install, cache)
            assert next(it) is not None  # blizzard
            assert [p.name.startswith(".copies-") for p in cache.iterdir() if p.is_dir()] == [True]
            it.close()
            assert not [p for p in cache.iterdir() if p.is_dir()]


class CommandTest(unittest.TestCase):
    def test_the_1_17_install_has_only_the_sd_set_and_no_subtitles(self):
        for extra, message in ((["--variant", "hd2"], "only the SD movies"), (["--subtitles", "ruRU"], "no subtitles")):
            with self.subTest(extra=extra), self.assertLogs("converter", "ERROR") as logs:
                assert main(["cinematics", "--data", "/nonexistent", *extra]) != 0
            assert any(message in line for line in logs.output)

    def test_optional_without_the_tools_writes_nothing(self):
        missing = cinematics.CinematicsToolsError("oggenc is needed")
        for source, module in (("--data", cinematics_smk), ("--scr", cinematics)):
            with self.subTest(source=source), tempfile.TemporaryDirectory() as tmp, \
                    mock.patch.object(module, "require", side_effect=missing):
                with self.assertLogs("converter", "WARNING") as logs:
                    assert main(["cinematics", source, "/nonexistent", "--optional", "--out", tmp]) == 0
                assert any("none written: oggenc is needed" in line for line in logs.output)
                assert not any(Path(tmp).iterdir())
                with self.assertLogs("converter", "ERROR"):  # asked for, the cinematics fail without their tools
                    assert main(["cinematics", source, "/nonexistent", "--out", tmp]) != 0

    def test_remastereds_sd_set_is_kept(self):
        classic = Profile.load().data_set_hash()
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp)
            path = cinematics.pack_path(out, "sd")
            path.parent.mkdir()
            data = pack_head(b"\x5a" * 32)
            path.write_bytes(data)
            with mock.patch.object(cinematics_smk, "require", side_effect=AssertionError("encoded")), \
                    self.assertLogs("converter", "INFO") as logs:
                assert main(["cinematics", "--data", "/nonexistent", "--out", tmp]) == 0
            assert any("StarCraft: Remastered's set, kept" in line for line in logs.output)
            assert path.read_bytes() == data
            assert cinematics.remastered_pack(out, "hd2", classic) is None  # none there
            # the 1.17 install's own pack, one of another schema or kind and a damaged one are replaced
            for replaced in (pack_head(classic), pack_head(b"\x5a" * 32, schema=cinematics.SCHEMA - 1),
                             pack_head(b"\x5a" * 32, kind=b"TEST"), data[:-4]):
                path.write_bytes(replaced)
                assert cinematics.remastered_pack(out, "sd", classic) is None

    def test_optional_keeps_the_other_errors(self):
        with tempfile.TemporaryDirectory() as tmp, mock.patch.object(cinematics_smk, "require"), \
                self.assertLogs("converter", "ERROR"):
            assert main(["cinematics", "--data", "/nonexistent", "--optional", "--out", tmp]) != 0


@unittest.skipUnless(ORIGINAL, "KOPRULU_ORIGINAL not set")
class OriginalTest(unittest.TestCase):
    def test_the_install_holds_the_reference_copies(self):
        with Resolver(ORIGINAL) as resolver:
            archives = {n: resolver.read_expected(f"smk\\{n}.smk", size, sha)[0]
                        for n, (size, sha) in cinematics_smk.expected().items()}
            briefing = resolver.read_expected("smk\\PreTerran_1.smk", *cinematics_smk.expected()["PreTerran_1"])[1]
        assert archives["blizzard"] == "StarDat.mpq" and archives["xZerg"] == "BroodWar.mpq"
        assert archives["Terran1"] == "StarCraft.mpq"
        if encoders():
            movie = transcode("PreTerran_1", briefing)
            assert (movie.packets.width, movie.packets.height, len(movie.packets.frames)) == (640, 308, 95)
            assert movie.ogg[:4] == b"OggS"


if __name__ == "__main__":
    unittest.main()
