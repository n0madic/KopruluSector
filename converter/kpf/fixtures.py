"""Cross-language fixtures: containers written here are read by game/data/kpf.lua.

Run `python3 -m converter.kpf.fixtures [out_dir]`; the default output is
tests/fixtures/generated/kpf (gitignored). The Lua suite tests/lua/test_kpf.lua
reads the files and the generated expect.lua.
"""
from __future__ import annotations

import hashlib
import logging
import struct
import sys
from pathlib import Path

from . import container as kpf
from . import kpd, kpm

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_OUT = ROOT / "tests" / "fixtures" / "generated" / "kpf"

INTS = [0, 1, 127, 128, 255, 256, 32767, 32768, 65535, 65536, 2**31 - 1, 2**31, 2**32 - 1]
LARGE_SIZE = 8 * 1024 * 1024  # multi-megabyte custom resource for the loading spike

log = logging.getLogger(__name__)

# Minimal CHK payloads accepted by the version 59 header, briefing and UMS tables.
V59 = {
    "VER ": struct.pack("<H", 59), "DIM ": struct.pack("<HH", 64, 64), "ERA ": struct.pack("<H", 0),
    "OWNR": bytes(12), "SIDE": bytes(12), "STR ": b"\x01\x00\x04\x00\x00", "SPRP": bytes(4), "FORC": bytes(20),
    "VCOD": b"vcod", "MBRF": b"", "MTXM": bytes(128), "THG2": b"", "MASK": b"", "UNIS": bytes(0xFD0),
    "UPGS": bytes(0x256), "TECS": bytes(0xD8), "PUNI": bytes(0x1644), "UPGR": bytes(0x6D4), "PTEC": bytes(0x390),
    "UNIT": b"", "UPRP": bytes(0x500), "MRGN": bytes(0x500), "TRIG": b"",
}
MISSION_STRINGS = (b"name", b"a.wav", b"b.wav", b"\x03ctl\x01")


def build_chk(sections: dict[str, bytes] | list[tuple[str, bytes]]) -> bytes:
    items = sections.items() if isinstance(sections, dict) else sections
    return b"".join(t.encode("latin1") + struct.pack("<I", len(p)) + p for t, p in items)


def chk_strings(*strings: bytes) -> bytes:
    """STR/TBL layout: u16 count, u16 offsets, NUL-terminated blob."""
    offsets, blob = [], b""
    base = 2 + 2 * len(strings)
    for s in strings:
        offsets.append(base + len(blob))
        blob += s + b"\0"
    return struct.pack(f"<H{len(strings)}H", len(strings), *offsets) + blob


def chk_trigger(actions: list[tuple[int, int]]) -> bytes:
    """One 2400-byte TRIG/MBRF record with (action id, WAV string) actions."""
    rec = bytearray(kpm.TRIGGER_SIZE)
    for i, (action_id, wav) in enumerate(actions):
        act = kpm.ACTIONS_OFFSET + i * kpm.ACTION_SIZE
        struct.pack_into("<I", rec, act + kpm.ACTION_WAV, wav)
        rec[act + kpm.ACTION_ID] = action_id
    return bytes(rec)


def iscript_bin(entries: list[tuple[int, int]]) -> bytes:
    """iscript.bin with one SCPE header per distinct header index; entries are (id, header index)."""
    count = max(h for _, h in entries) + 1
    headers = b"".join(b"SCPE" + bytes([0, 0, 0, 0]) + struct.pack("<2H", 0, 0) for _ in range(count))
    index_offset = 4 + len(headers)
    index = b"".join(struct.pack("<HH", i, 4 + 12 * h) for i, h in entries) + struct.pack("<HH", 0xFFFF, 0)
    return struct.pack("<I", index_offset) + headers + index


def ai_bin(records: list[tuple[bytes, int]], stride: int, code_size: int = 64) -> bytes:
    """AI binary with `code_size` bytes of code and a directory of (id, offset) records."""
    directory = b"".join(sid + struct.pack("<I", off) + bytes(stride - 8) for sid, off in records) + bytes(4)
    return struct.pack("<I", 4 + code_size) + bytes(code_size) + directory


AI_RECORDS = [(b"Ter5", 16), (b"Redi", 0), (b"Both", 20)]
BW_RECORDS = [(b"Redi", 24), (b"BwOn", 28), (b"Both", 32), (b"Zero", 0)]


def dat_fixture(source_hash: bytes) -> tuple[bytes, list[tuple[str, str, int, int, int]]]:
    """dat.kpd with every native layout filled by a byte counter, plus probes
    (table, column, id, element, expected value) for the Lua suite."""
    from ..formats import dat

    layouts = dat.load_layouts()
    files = {f.path: bytes((i * 7 + len(f.path)) % 256 for i in range(f.size)) for f in layouts}
    data = kpd.build_dat(files, layouts, profile_id="bw-1.17.0.1", data_set_hash=source_hash).to_bytes()
    probes = []
    for f in layouts:
        for c in f.columns:
            last = c.first_id + (c.count - 1) // c.per_id
            for record, element in ((c.first_id, 0), (last, c.per_id - 1)):
                probes.append((f.table, c.name, record, element, dat.value(f, files[f.path], c.name, record, element)))
    return data, probes


def mission_fixture(source_hash: bytes) -> bytes:
    """8x4 version 205 mission with colors, forces, overrides and two WAV strings (one missing)."""
    from ..formats import chk

    sections = {k: v for k, v in V59.items() if k not in ("UNIS", "UPGS", "TECS", "UPGR", "PTEC")}
    sections.update({
        "VER ": struct.pack("<H", 205), "DIM ": struct.pack("<HH", 8, 4), "ERA ": struct.pack("<H", 13),
        "OWNR": bytes(range(12)), "SIDE": bytes(range(20, 32)), "SPRP": struct.pack("<HH", 1, 4),
        "FORC": bytes(range(1, 21)), "MTXM": struct.pack("<32H", *range(1000, 1032)), "MASK": bytes(range(32)),
        "STR ": chk_strings(*MISSION_STRINGS), "TRIG": chk_trigger([(8, 2), (7, 3)]), "MBRF": chk_trigger([(2, 2)]),
        "UNIx": bytes(0x1048), "UPGx": bytes(0x31A), "TECx": bytes(0x18C), "PUPx": bytes(0x90E),
        "PTEx": bytes(0x688), "MRGN": bytes(0x13EC), "COLR": bytes([7, 6, 5, 4, 3, 2, 1, 0]),
    })
    scenario = chk.load(build_chk(sections), chk.LoaderTables.load(), vcod=b"vcod")
    sounds = kpm.mission_sounds(scenario, "campaign\\fixture", lambda p: p.endswith("a.wav"))
    return kpm.build(scenario, sounds, profile_id="bw-1.17.0.1", data_set_hash=source_hash).to_bytes()


def _large_payload() -> bytes:
    # Deterministic, poorly compressible bytes (SHA-256 counter stream).
    out = bytearray()
    counter = 0
    while len(out) < LARGE_SIZE:
        out += hashlib.sha256(counter.to_bytes(8, "little")).digest()
        counter += 1
    return bytes(out[:LARGE_SIZE])


def _lua_bytes(data: bytes) -> str:
    return '"' + "".join(f"\\{b}" for b in data) + '"'


def build_fixtures(out: Path = DEFAULT_OUT) -> list[Path]:
    out.mkdir(parents=True, exist_ok=True)
    source_hash = hashlib.sha256(b"fixture source").digest()
    basic = kpf.Container(b"TEST", schema_version=3, converter_version=7, profile_id="bw-1.17.0.1",
                          source_hash=source_hash, provenance_hash=hashlib.sha256(b"provenance").digest())
    basic.add("INTS", struct.pack(f"<{len(INTS)}I", *INTS))
    basic.add("BYTE", bytes(range(256)), schema_version=2)
    basic.add("ODD ", b"abc")
    basic.add("EMPT", b"")
    basic.add_derived("WALK", bytes([0x80, 1, 2, 4, 8]) * 7, algorithm_id="WLK1", algorithm_version=5,
                      inputs_hash=hashlib.sha256(b"walk inputs").digest(), flags=0x8001)
    large = kpf.Container(b"TEST", schema_version=3, converter_version=7, profile_id="bw-1.17.0.1",
                          source_hash=source_hash)
    large_payload = _large_payload()
    large.add("BLOB", large_payload)

    dat_kpd, dat_probes = dat_fixture(source_hash)
    files = {"basic.kpd": basic.to_bytes(), "large.kpd": large.to_bytes(), "mission.kpm": mission_fixture(source_hash),
             "dat.kpd": dat_kpd,
             "iscript.kpd": kpd.build_iscript(iscript_bin([(5, 0), (3, 1), (5, 1)]), profile_id="bw-1.17.0.1",
                                              data_set_hash=source_hash).to_bytes(),
             "aiscript.kpd": kpd.build_aiscript(ai_bin(AI_RECORDS, 16), ai_bin(BW_RECORDS, 8),
                                                profile_id="bw-1.17.0.1", data_set_hash=source_hash).to_bytes()}
    written = []
    for name, data in files.items():
        path = out / name
        path.write_bytes(data)
        written.append(path)

    expect = [
        "-- Generated by converter/kpf/fixtures.py; do not edit.",
        "return {",
        f"\tsource_hash = {_lua_bytes(source_hash)},",
        "\tints = {" + ", ".join(str(v) for v in INTS) + "},",
        f"\tbasic_size = {len(files['basic.kpd'])},",
        f"\tlarge_size = {len(files['large.kpd'])},",
        f"\tlarge_payload_size = {len(large_payload)},",
        f"\tlarge_crc32 = {large.section('BLOB').crc32},",
        f"\tlarge_tail = {_lua_bytes(large_payload[-16:])},",
        "\tdat_probes = {" + ", ".join(f'{{"{t}", "{c}", {r}, {e}, {v}}}' for t, c, r, e, v in dat_probes) + "},",
        "\tmission_strings = {" + ", ".join(_lua_bytes(x) for x in MISSION_STRINGS) + "},",
        "}",
        "",
    ]
    expect_path = out / "expect.lua"
    expect_path.write_text("\n".join(expect))
    written.append(expect_path)
    return written


def main() -> int:
    logging.basicConfig(level=logging.INFO, format="%(message)s")
    out = Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_OUT
    for path in build_fixtures(out):
        log.info("wrote %s", path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
