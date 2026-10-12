"""RIFF parsing by the native walker and sounds.kpd; original-data checks when available."""
from __future__ import annotations

import json
import os
import struct
from pathlib import Path

import pytest

from converter.formats import wav
from converter.kpf import container as kpf
from converter.kpf import kpd

ROOT = Path(__file__).resolve().parents[2]
ORIGINAL = os.environ.get("KOPRULU_ORIGINAL")
FMT = struct.pack("<HHIIHH", 1, 1, 22050, 44100, 2, 16)


def _riff(*chunks: bytes) -> bytes:
    return b"RIFF" + struct.pack("<I", 0) + b"WAVE" + b"".join(chunks)


def _chunk(tag: bytes, payload: bytes, pad: bool = False) -> bytes:
    return tag + struct.pack("<I", len(payload)) + payload + (b"\0" if pad and len(payload) % 2 else b"")


def test_native_walker_rules():
    ok = _riff(_chunk(b"fmt ", FMT), _chunk(b"JUNK", b"x"), _chunk(b"data", bytes(8)))
    assert (wav.read_info(ok).data_offset, wav.read_info(ok).data_length) == (53, 8)
    with pytest.raises(wav.WavError):  # a standard pad byte misaligns the native walker
        wav.read_info(_riff(_chunk(b"fmt ", FMT), _chunk(b"JUNK", b"x", pad=True), _chunk(b"data", bytes(8))))
    with pytest.raises(wav.WavError):  # data is only searched after fmt
        wav.read_info(_riff(_chunk(b"data", bytes(8)), _chunk(b"fmt ", FMT)))
    with pytest.raises(wav.WavError):  # data larger than the rest of the file
        wav.read_info(_riff(_chunk(b"fmt ", FMT), _chunk(b"data", bytes(8)))[:-1])
    assert wav.read_info(_riff(_chunk(b"fmt ", FMT + b"ext"), _chunk(b"data", b""))).data_length == 0


def test_duration_wraps_like_native():
    info = wav.WavInfo(1, 1, 22050, 44100, 2, 16, 44, 5_000_000)
    assert info.duration_ms == ((5_000_000 * 1000) & 0xFFFFFFFF) // 44100


def test_sound_records():
    data = _riff(_chunk(b"fmt ", FMT), _chunk(b"data", bytes(4410)))
    rec = kpd.SOUND_RECORD.unpack(kpd.sound_record(data))
    assert rec == (3, 1, 1, 22050, 44100, 2, 16, 4410)
    assert kpd.SOUND_RECORD.unpack(kpd.sound_record(None))[0] == 0
    # No fmt chunk: ParseWaveFile rejects it, the file is present but not parsed.
    rejected = _riff(_chunk(b"data", bytes(4)))
    assert kpd.SOUND_RECORD.unpack(kpd.sound_record(rejected)) == (kpd.SOUND_PRESENT, 0, 0, 0, 0, 0, 0, 0)


@pytest.mark.skipif(not ORIGINAL, reason="KOPRULU_ORIGINAL not set")
def test_native_parser_cases():
    from converter.mpq.profile import Resolver

    evidence = json.loads((ROOT / "docs/research/evidence/native-audiovisual-probes.json").read_text())
    cases = [c for c in evidence["cases"] if c["case"] == "stock_wav_parser_lengths"]
    assert len(cases) == 60
    with Resolver(ORIGINAL, full_hash=False) as r:
        for c in cases:
            data = r.read(c["inputs"]["path"])[1][: c["inputs"]["input_length"]]
            want = c["actual"]
            if want["payload_offset"]:
                info = wav.read_info(data)
                assert (info.data_offset, info.data_length) == (want["payload_offset"], want["remaining"]), c["inputs"]
            else:
                with pytest.raises(wav.WavError):
                    wav.read_info(data)
        container = kpf.parse(kpd.build_sounds(lambda p: r.read(p)[1], r.has, profile_id="p",
                                               data_set_hash=bytes(32)).to_bytes())
    payload = container.section("SFX ").payload
    count = struct.unpack_from("<H", payload, 0)[0]
    present = [kpd.SOUND_RECORD.unpack_from(payload, 2 + i * kpd.SOUND_RECORD.size)[0] for i in range(count)]
    absent = [i for i, flags in enumerate(present) if not flags]
    assert count == 1144 and absent == [0, 86, 104, 105, 1066]
