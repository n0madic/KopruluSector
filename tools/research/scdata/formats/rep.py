"""StarCraft 1.17 replays (`.rep`): the file `WriteReplayFile 0x480080` writes and `ReadReplayFile 0x480690` reads.

A replay is four compressed blocks ([P05](../../../../docs/research/atlas/p05.md) `WriteCompressedBlock 0x4137d0`):
the magic `reRS`, the 633-byte header, the command record (`SaveRecordedCommands 0x478980`: its used byte count,
then that many bytes when it is above 0) and the map (a DWORD CHK size block, then the CHK). A block is
`DWORD crc, DWORD chunks, {DWORD len, bytes}*`: the chunks are 8 KiB of input, each PKWARE DCL imploded or stored raw
when its stored length equals its input length; the crc is CRC-32 over the block's input without the final inversion.

Only the reader's side of PKWARE DCL is here (explode), for binary (uncoded-literal) streams, the only kind the
original writes (type 0, `0x413940`); the writer stores every chunk raw, which the original reads the same way.
"""
from __future__ import annotations

import struct
import zlib
from dataclasses import dataclass, field

from ..errors import ConverterError

MAGIC = 0x53526572  # "reRS"
HEADER_SIZE = 0x279
CHUNK = 0x2000
PLAYER_RECORD = 36
PLAYERS = 12
# Command lengths with their ID byte (0x4ef048, read by 0x4720c0); IDs 9-0xb use 2 + 2*count instead; absent = -1
COMMAND_LENGTHS = {
    5: 1, 6: 33, 7: 33, 8: 1, 0xC: 8, 0xD: 3, 0xE: 5, 0xF: 2, 0x10: 1, 0x11: 1, 0x12: 5, 0x13: 3, 0x14: 10,
    0x15: 11, 0x18: 1, 0x19: 1, 0x1A: 2, 0x1B: 1, 0x1C: 1, 0x1D: 1, 0x1E: 2, 0x1F: 3, 0x20: 3, 0x21: 2, 0x22: 2,
    0x23: 3, 0x24: 1, 0x25: 2, 0x26: 2, 0x27: 1, 0x28: 2, 0x29: 3, 0x2A: 1, 0x2B: 2, 0x2C: 2, 0x2D: 2, 0x2E: 1,
    0x2F: 5, 0x30: 2, 0x31: 1, 0x32: 2, 0x33: 1, 0x34: 1, 0x35: 3, 0x36: 1, 0x37: 1, 0x38: 1, 0x39: 1, 0x3A: 2,
    0x3B: 2, 0x55: 2, 0x56: 10, 0x57: 2, 0x58: 5, 0x5A: 1, 0x5C: 82,
}


class RepError(ConverterError, ValueError):
    """A malformed replay or block."""


def crc(data: bytes) -> int:
    """The blocks' checksum (0x402160 from 0xffffffff, no final inversion): zlib's CRC-32 inverted."""
    return ~zlib.crc32(data) & 0xFFFFFFFF


# --- PKWARE DCL explode (binary literals) ---

def _compact(rep: list[int]) -> list[int]:
    """A code's bit lengths from the compact form: each byte is (repeat - 1) << 4 | length."""
    out: list[int] = []
    for b in rep:
        out += [b & 15] * ((b >> 4) + 1)
    return out


def _huffman(lengths: list[int]) -> tuple[list[int], list[int]]:
    count = [0] * 14
    for n in lengths:
        count[n] += 1
    offs = [0] * 14
    for n in range(1, 13):
        offs[n + 1] = offs[n] + count[n]
    symbol = [0] * len(lengths)
    for s, n in enumerate(lengths):
        if n:
            symbol[offs[n]] = s
            offs[n] += 1
    return count, symbol


_LEN_CODE = _huffman(_compact([2, 35, 36, 53, 38, 23]))
_DIST_CODE = _huffman(_compact([2, 20, 53, 230, 247, 151, 248]))
_LEN_BASE = (3, 2, 4, 5, 6, 7, 8, 9, 10, 12, 16, 24, 40, 72, 136, 264)
_LEN_EXTRA = (0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8)


class _Bits:
    def __init__(self, data: bytes):
        self.data, self.pos, self.buf, self.cnt = data, 2, 0, 0

    def bits(self, need: int) -> int:
        while self.cnt < need:
            if self.pos >= len(self.data):
                raise RepError("imploded data ends early")
            self.buf |= self.data[self.pos] << self.cnt
            self.pos += 1
            self.cnt += 8
        v = self.buf & ((1 << need) - 1)
        self.buf >>= need
        self.cnt -= need
        return v

    def decode(self, code) -> int:
        count, symbol = code
        c = first = index = 0
        for n in range(1, 14):
            c |= self.bits(1) ^ 1  # the codes are stored inverted
            k = count[n]
            if c < first + k:
                return symbol[index + c - first]
            index += k
            first = (first + k) << 1
            c <<= 1
        raise RepError("bad Huffman code")


def explode(data: bytes, size: int) -> bytes:
    """Decompresses one PKWARE DCL stream; refuses one that does not give exactly `size` bytes."""
    if len(data) < 2:
        raise RepError("imploded data shorter than its header")
    lit, dict_bits = data[0], data[1]
    if lit != 0:
        raise RepError("coded literals: the original writes binary streams only")
    if dict_bits not in (4, 5, 6):
        raise RepError(f"bad dictionary size {dict_bits}")
    s, out = _Bits(data), bytearray()
    while True:
        if s.bits(1):
            sym = s.decode(_LEN_CODE)
            length = _LEN_BASE[sym] + s.bits(_LEN_EXTRA[sym])
            if length == 519:
                break
            shift = 2 if length == 2 else dict_bits
            dist = (s.decode(_DIST_CODE) << shift) + s.bits(shift) + 1
            if dist > len(out):
                raise RepError("distance before the start")
            for _ in range(length):
                out.append(out[-dist])
        else:
            out.append(s.bits(8))
        if len(out) > size:
            raise RepError("imploded data longer than its chunk")
    if len(out) != size:
        raise RepError(f"chunk of {len(out)} bytes, {size} expected")
    return bytes(out)


# --- blocks ---

def read_block(data: bytes, pos: int, size: int) -> tuple[bytes, int]:
    """Reads a block of `size` input bytes at `pos` (ReadCompressedBlock 0x413ae0); returns it and the next offset."""
    if pos + 8 > len(data):
        raise RepError("block header past the end")
    want, chunks = struct.unpack_from("<II", data, pos)
    pos += 8
    if chunks != (size + CHUNK - 1) // CHUNK:
        raise RepError(f"{chunks} chunks for {size} bytes")
    out = bytearray()
    for i in range(chunks):
        n = min(size - i * CHUNK, CHUNK)
        if pos + 4 > len(data):
            raise RepError("chunk length past the end")
        stored = struct.unpack_from("<I", data, pos)[0]
        pos += 4
        if stored > n or pos + stored > len(data):
            raise RepError("chunk past the end")
        piece = data[pos:pos + stored]
        pos += stored
        out += piece if stored == n else explode(piece, n)
    if crc(bytes(out)) != want:
        raise RepError("block checksum")
    return bytes(out), pos


def write_block(data: bytes) -> bytes:
    """A block with every chunk stored raw (WriteCompressedBlock 0x4137d0 keeps a chunk raw that does not shrink)."""
    chunks = (len(data) + CHUNK - 1) // CHUNK
    out = bytearray(struct.pack("<II", crc(data), chunks))
    for i in range(chunks):
        piece = data[i * CHUNK:(i + 1) * CHUNK]
        out += struct.pack("<I", len(piece)) + piece
    return bytes(out)


# --- the file ---

@dataclass
class Player:
    slot: int
    storm: int
    type: int
    race: int
    force: int
    name: bytes


@dataclass
class Header:
    raw: bytes
    brood_war: int = 0
    frames: int = 0
    campaign: int = 0
    seed: int = 0
    speed: int = 0
    players: list[Player] = field(default_factory=list)
    colors: tuple = ()
    forces: bytes = b""


def decode_header(raw: bytes) -> Header:
    """The fields of the header (WriteReplayFile 0x480080; the accessors 0x480540-0x480680)."""
    if len(raw) != HEADER_SIZE:
        raise RepError("header size")
    h = Header(raw, raw[0], struct.unpack_from("<I", raw, 1)[0], struct.unpack_from("<H", raw, 5)[0],
               struct.unpack_from("<I", raw, 8)[0], raw[0x3A])
    for i in range(PLAYERS):
        o = 0xA1 + i * PLAYER_RECORD
        slot, storm = struct.unpack_from("<Ii", raw, o)
        h.players.append(Player(slot, storm, raw[o + 8], raw[o + 9], raw[o + 10], raw[o + 11:o + 36].split(b"\0")[0]))
    h.colors = struct.unpack_from("<8I", raw, 0x251)
    h.forces = raw[0x271:0x279]
    return h


def command_length(data: bytes, pos: int) -> int:
    """0x4720c0: a command's length with its ID byte, -1 for an unknown ID."""
    cid = data[pos]
    if 9 <= cid <= 0xB:
        return 2 + 2 * data[pos + 1] if pos + 1 < len(data) else -1
    return COMMAND_LENGTHS.get(cid, -1)


@dataclass
class Frame:
    frame: int
    commands: list[tuple[int, bytes]]  # (Storm ID, command with its ID byte)


def decode_commands(record: bytes) -> list[Frame]:
    """The recorded blocks: DWORD frame, BYTE size, then {Storm ID, command} entries (RecordCommand 0x478340)."""
    out, pos = [], 0
    while pos < len(record):
        if pos + 5 > len(record):
            raise RepError("block header past the end of the record")
        frame, size = struct.unpack_from("<IB", record, pos)
        pos += 5
        end = pos + size
        if end > len(record):
            raise RepError("block past the end of the record")
        cmds = []
        while pos < end:
            storm = record[pos]
            n = command_length(record, pos + 1) if pos + 1 < end else -1
            if n <= 0 or pos + 1 + n > end:
                raise RepError(f"frame {frame}: bad command at {pos}")
            cmds.append((storm, record[pos + 1:pos + 1 + n]))
            pos += 1 + n
        out.append(Frame(frame, cmds))
    return out


@dataclass
class Replay:
    header: Header
    record: bytes
    chk: bytes
    tail: bytes  # bytes after the map's block, which ReadReplayFile never reads


def read(data: bytes) -> Replay:
    magic, pos = read_block(data, 0, 4)
    if struct.unpack("<I", magic)[0] != MAGIC:
        raise RepError("not a replay")
    raw, pos = read_block(data, pos, HEADER_SIZE)
    if raw[0x46] == 0:
        raise RepError("header +0x46 is 0")
    used, pos = read_block(data, pos, 4)
    n = struct.unpack("<i", used)[0]
    record = b""
    if n > 0:
        record, pos = read_block(data, pos, n)
    size, pos = read_block(data, pos, 4)
    chk, pos = read_block(data, pos, struct.unpack("<I", size)[0])
    return Replay(decode_header(raw), record, chk, data[pos:])


def write(r: Replay) -> bytes:
    """The file of WriteReplayFile 0x480080 (chunks raw), with r.tail appended."""
    out = write_block(struct.pack("<I", MAGIC)) + write_block(r.header.raw) + write_block(struct.pack("<i", len(r.record)))
    if r.record:
        out += write_block(r.record)
    return out + write_block(struct.pack("<I", len(r.chk))) + write_block(r.chk) + r.tail
