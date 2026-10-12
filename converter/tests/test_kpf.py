from __future__ import annotations

import struct

import pytest

from converter.kpf import container as kpf
from converter.kpf.fixtures import build_fixtures


def sample() -> kpf.Container:
    c = kpf.Container(b"TEST", schema_version=3, converter_version=7, profile_id="bw-1.17.0.1",
                      source_hash=bytes(range(32)), provenance_hash=bytes(range(32, 64)))
    c.add("AAAA", b"x", schema_version=2)
    c.add("EMPT", b"")
    c.add_derived("WALK", bytes(range(256)) * 3, algorithm_id="WLK1", algorithm_version=5,
                  inputs_hash=b"\xab" * 32, flags=0x8001)
    return c


def test_round_trip():
    original = sample()
    data = original.to_bytes()
    parsed = kpf.parse(data)
    assert parsed.kind == b"TEST"
    assert parsed.schema_version == 3 and parsed.converter_version == 7
    assert parsed.profile_id == "bw-1.17.0.1"
    assert parsed.source_hash == bytes(range(32))
    assert [s.fourcc for s in parsed.sections] == [b"AAAA", b"EMPT", b"WALK"]
    walk = parsed.section("WALK")
    assert walk.section_class == kpf.CLASS_DERIVED
    assert (walk.algorithm_id, walk.algorithm_version, walk.flags) == (b"WLK1", 5, 0x8001)
    assert walk.inputs_hash == b"\xab" * 16
    assert walk.payload == bytes(range(256)) * 3
    assert parsed.section("AAAA").schema_version == 2
    assert parsed.section("EMPT").payload == b""


def test_alignment_and_size():
    data = sample().to_bytes()
    assert len(data) % kpf.ALIGN == 0
    for s in kpf.parse(data).sections:
        assert s.offset % kpf.ALIGN == 0


def test_deterministic():
    assert sample().to_bytes() == sample().to_bytes()


def test_rejects_duplicate():
    c = sample()
    with pytest.raises(kpf.ContainerError):
        c.add("AAAA", b"")


def test_rejects_corruption():
    data = bytearray(sample().to_bytes())
    data[-5] ^= 0xFF
    with pytest.raises(kpf.ContainerError, match="crc"):
        kpf.parse(bytes(data))
    with pytest.raises(kpf.ContainerError, match="size"):
        kpf.parse(bytes(data[:-4]))
    bad = bytearray(sample().to_bytes())
    struct.pack_into("<H", bad, 8, 99)
    with pytest.raises(kpf.ContainerError, match="version"):
        kpf.parse(bytes(bad))


def test_fixtures_parse(tmp_path):
    written = build_fixtures(tmp_path)
    assert written
    for path in written:
        if path.suffix == ".kpd":
            kpf.parse(path.read_bytes())
