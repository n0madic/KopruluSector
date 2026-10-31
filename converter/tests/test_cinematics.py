"""The cinematics pack (converter/emit/cinematics.py, formats/ivf.py, ADR 0015): the IVF reader and the VP9 key frame
test on synthetic bytes; the split of a WebM, with FFmpeg and its libvpx-vp9 (skipped without them), and the pack's
tables written from a storage holding a movie; with KOPRULU_SCR, a real install's SD set."""
from __future__ import annotations

import os
import struct
import subprocess
import tempfile
import unittest
from unittest import mock
from fractions import Fraction
from pathlib import Path

from converter.emit import cinematics
from converter.formats import ffmpeg, ivf
from converter.kpf import fixtures
from converter.kpf.container import parse

SCR = os.environ.get("KOPRULU_SCR")


def ivf_file(frames: list[bytes], width: int = 16, height: int = 16) -> bytes:
    head = ivf.HEADER.pack(ivf.MAGIC, 0, 32, b"VP90", width, height, 1000, 1, len(frames))
    return head + b"".join(ivf.FRAME.pack(len(f), i * 66) + f for i, f in enumerate(frames))


def synthetic_webm() -> bytes | None:
    """A 2 second VP9 + Vorbis WebM from FFmpeg's test sources, or None where FFmpeg lacks the encoders."""
    return fixtures.cine_webm("testsrc", "yuv420p", duration=2, gop=10)


def remux(webm: bytes, *args: str) -> bytes | None:
    """The WebM through `ffmpeg -i - <args> -f webm -`, or None where FFmpeg cannot."""
    run = subprocess.run(["ffmpeg", "-v", "error", "-i", "pipe:", *args, "-f", "webm", "pipe:"], input=webm,
                         capture_output=True, check=False)
    return run.stdout if run.returncode == 0 and run.stdout else None


class Storage:
    """A CascStorage's has and read over one movie."""

    def __init__(self, names: dict[str, bytes]):
        self.names = {n.lower(): d for n, d in names.items()}

    def has(self, name: str) -> bool:
        return name.lower() in self.names

    def read(self, name: str) -> bytes:
        return self.names[name.lower()]


class IvfTest(unittest.TestCase):
    def test_key_frames_by_their_header_bits(self):
        assert ivf.is_key(b"\x82\x49\x83")  # profile 0, frame_type 0
        assert not ivf.is_key(b"\x86\x00")  # frame_type 1: an inter frame
        assert not ivf.is_key(b"\x88\x00")  # show_existing_frame: shows an earlier frame
        assert ivf.is_key(b"\xa2\x49")  # profile 1 (the SD movies' 4:4:4), a key frame
        assert not ivf.is_key(b"\xa6\x49")
        assert not ivf.is_key(b"\x02\x00") and not ivf.is_key(b"")  # no frame marker, no data
        assert ivf.is_key(b"\xb0\x82") and not ivf.is_key(b"\xb4\x00")  # profile 3: its reserved bit comes first

    def test_parse_reads_every_frame(self):
        frames = [b"\x82aaa", b"\x86bb", b"\x86"]
        got = ivf.parse(ivf_file(frames))
        assert (got.fourcc, got.width, got.height) == (b"VP90", 16, 16)
        assert [f.data for f in got.frames] == frames
        assert [f.key for f in got.frames] == [True, False, False]
        assert [f.pts for f in got.frames] == [0, 66, 132]
        assert got.timebase == Fraction(1, 1000)

    def test_parse_refuses_a_zero_time_base(self):
        data = bytearray(ivf_file([b"\x82a"]))
        data[16:20] = bytes(4)
        with self.assertRaises(ivf.IvfError):
            ivf.parse(bytes(data))

    def test_parse_refuses_cut_and_foreign_files(self):
        data = ivf_file([b"\x82aaaa", b"\x86bbbb"])
        for cut in (10, 33, 40, len(data) - 1):
            with self.assertRaises(ivf.IvfError):
                ivf.parse(data[:cut])
        with self.assertRaises(ivf.IvfError):
            ivf.parse(b"RIFF" + data[4:])


class PackTest(unittest.TestCase):
    def test_table_is_the_originals(self):
        names, preludes = cinematics.table()
        assert len(names) == 28 and names[0] == "blizzard" and names[24] == "starXintr"
        assert preludes[0] == (6, 2) and preludes[-1] == (22, 19) and len(preludes) == 10

    def test_shown_size_doubles_the_narrow_movies(self):
        assert cinematics.shown_size(320, 308) == (640, 308)
        assert cinematics.shown_size(640, 308) == (640, 308)
        assert cinematics.shown_size(64, 48) == (64, 48)
        assert cinematics.shown_size(960, 540) == (960, 540)

    def test_frame_rate_within_the_players_terms(self):
        assert cinematics.frame_rate("15/1") == 15
        assert cinematics.frame_rate("24000/1001") == Fraction(24000, 1001)
        for bad in ("0/0", "0/1", "-15/1", "1000001/1", "1/1000001", "x"):
            with self.assertRaises(cinematics.CinematicsError):
                cinematics.frame_rate(bad)

    def test_video_begins_with_the_sound(self):
        cinematics.start_offset("0.012000", "0.000000", Fraction(19997, 1333))  # the real SD movies: 12 ms
        cinematics.start_offset("0.003000", "0.000000", Fraction(30000, 1001))  # and HD2's: 3 ms
        for video, audio in (("0.040000", "0.000000"), ("0.000000", "0.040000"), ("x", "0")):
            with self.assertRaises(cinematics.CinematicsError):
                cinematics.start_offset(video, audio, Fraction(30))  # 40 ms is past half of a 33 ms frame

    def test_times_must_follow_the_rate(self):
        def packets(pts: list[int]) -> ivf.Ivf:
            return ivf.Ivf(b"VP90", 16, 16, Fraction(1, 1000), [ivf.Frame(t, b"", False) for t in pts])

        cinematics.check_times(packets([7, 74, 140, 207]), Fraction(15))  # 15 fps in rounded milliseconds
        with self.assertRaises(cinematics.CinematicsError):
            cinematics.check_times(packets([0, 67, 133, 267]), Fraction(15))  # a frame missing before the last
        with self.assertRaises(cinematics.CinematicsError):
            cinematics.check_times(packets([n * 42 for n in range(100)]), Fraction(25))  # 24 fps taken for 25

    def test_sound_is_vorbis_or_none(self):
        webm = synthetic_webm()
        if webm is None:
            self.skipTest("FFmpeg with libvpx-vp9 and its vorbis encoder is needed")
        silent = remux(webm, "-an", "-c:v", "copy")
        assert silent is not None
        packets, rate, ogg = cinematics.split(silent)
        assert len(packets.frames) == 30 and rate == 15 and ogg == b""
        opus = remux(webm, "-c:v", "copy", "-c:a", "libopus")
        if opus is None:
            self.skipTest("FFmpeg's libopus encoder is needed")
        with self.assertRaisesRegex(cinematics.CinematicsError, "opus"):
            cinematics.split(opus)

    def test_probe_takes_the_fields_by_name(self):
        webm = synthetic_webm()
        if webm is None:
            self.skipTest("FFmpeg with libvpx-vp9 and its vorbis encoder is needed")
        with tempfile.NamedTemporaryFile(suffix=".webm") as f:
            f.write(webm)
            f.flush()
            fields = ffmpeg.probe(f.name, "v:0", ("height", "codec_name", "width"), cinematics.CinematicsError)
            assert fields == ["48", "vp9", "64"]  # not ffprobe's own order (codec_name, width, height)
            assert ffmpeg.probe(f.name, "s:0", ("codec_name",), cinematics.CinematicsError) is None
            with self.assertRaisesRegex(cinematics.CinematicsError, "no_such_field"):
                ffmpeg.probe(f.name, "v:0", ("codec_name", "no_such_field"), cinematics.CinematicsError)

    def test_pack_refuses_an_empty_packet(self):
        webm = synthetic_webm()
        if webm is None:
            self.skipTest("FFmpeg with libvpx-vp9 and its vorbis encoder is needed")
        packets, rate, ogg = cinematics.split(webm)
        frames = list(packets.frames)
        frames[3] = ivf.Frame(frames[3].pts, b"", False)
        empty = ivf.Ivf(packets.fourcc, packets.width, packets.height, packets.timebase, frames)
        storage = Storage({"SD/smk/blizzard.webm": webm})
        with tempfile.TemporaryDirectory() as tmp, mock.patch.object(cinematics, "split", return_value=(empty, rate, ogg)):
            with self.assertRaisesRegex(cinematics.CinematicsError, "packet of 0 bytes"):
                cinematics.generate(storage, Path(tmp), "sd", "bw-1.17.0.1", bytes(32))

    def test_pack_holds_the_packets_and_the_sound(self):
        webm = synthetic_webm()
        if webm is None:
            self.skipTest("FFmpeg with libvpx-vp9 and its vorbis encoder is needed")
        storage = Storage({"SD/smk/blizzard.webm": webm, "SD/smk/Terran1.webm": webm})
        with tempfile.TemporaryDirectory() as tmp:
            info = cinematics.generate(storage, Path(tmp), "sd", "bw-1.17.0.1", bytes(32))
            assert info["clips"] == 2 and info["movies"] == 28
            c = parse(Path(info["path"]).read_bytes())
        assert c.kind == b"CINE"
        variant, zero, clips, preludes = struct.unpack("<BBHH", c.section("HEAD").payload)
        assert (variant, zero, clips, preludes) == (0, 0, 2, 10)
        clip, frames, data = c.section("CLIP").payload, c.section("FRMS").payload, c.section("DATA").payload
        assert len(clip) == 2 * cinematics.CLIP_FORMAT.size
        first = 0
        for n, number in enumerate((0, 6)):
            (name, nid, flags, reserved, w, h, sw, sh, num, den, count, v_at, v_size, frm, a_at, a_size
             ) = cinematics.CLIP_FORMAT.unpack_from(clip, n * cinematics.CLIP_FORMAT.size)
            assert nid == number and (flags, reserved) == (0, 0)
            assert name.rstrip(b"\0") == (b"smk\\blizzard", b"smk\\terran1")[n]
            assert (w, h, sw, sh) == (64, 48, 64, 48)
            assert (num, den) == (15, 1) and count == 30 and frm == first
            sizes = struct.unpack_from(f"<{count}I", frames, 4 * frm)
            assert sum(s & ~cinematics.KEY for s in sizes) == v_size
            assert sizes[0] & cinematics.KEY and not sizes[1] & cinematics.KEY
            assert sum(1 for s in sizes if s & cinematics.KEY) == 3  # keys every 10 frames
            assert data[a_at:a_at + 4] == b"OggS" and a_size > 0
            assert v_at % 4 == 0 and a_at % 4 == 0 and a_at >= v_at + v_size
            first += count
        assert c.section("PREL").payload[:4] == bytes((6, 2, 7, 3))

    @unittest.skipUnless(SCR, "KOPRULU_SCR names a Remastered install")
    def test_real_sd_set(self):
        from converter.casc.storage import CascStorage

        with CascStorage(SCR) as storage, tempfile.TemporaryDirectory() as tmp:
            info = cinematics.generate(storage, Path(tmp), "sd", "bw-1.17.0.1", bytes(32))
        assert info["clips"] == 28


if __name__ == "__main__":
    unittest.main()
