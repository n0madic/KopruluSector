"""Original files rebuilt from StarCraft: Remastered's own formats (ADR 0013, stage R1).

Remastered keeps the glue and console art as RGBA images (`SD/…/*.DDS`, uncompressed or DXT) where 1.17 has
palette-indexed PCX and GRP files. The palette is the one of a PCX Remastered keeps exactly (a donor the
manifest names); each pixel takes the palette index of its colour:
- alpha 0 is index 0 (transparent);
- an opaque pixel takes the lowest index of its colour, a translucent one (the popups' 170) the highest, which
  is how 1.17's files use their duplicate colours (measured on 173 images: 154 exact);
- a colour the palette lacks (DXT's rounding) takes the nearest one.
The manifest's hashes then say whether the result is the original (`pixels`), shows the same colours (`rgb`)
or neither.
"""
from __future__ import annotations

import base64
import difflib
import hashlib
import re
import struct
import zlib

from dataclasses import dataclass

from ..errors import ConverterError
from ..formats import dds, dialog, grp, pcx, smk

TRANSPARENT = 0
OPAQUE = 255
Palette = tuple[tuple[int, int, int], ...]


def plane_hash(width: int, height: int, plane: bytes) -> str:
    return hashlib.sha256(struct.pack("<HH", width, height) + plane).hexdigest()


def rgb_plane(pixels: bytes, palette: Palette) -> bytes:
    return b"".join(bytes(palette[i]) for i in pixels)


class IndexMap:
    """RGBA -> palette index by the rule above, memoised per colour."""

    def __init__(self, palette: Palette):
        self.palette = palette
        self.lowest: dict[tuple[int, int, int], int] = {}
        self.highest: dict[tuple[int, int, int], int] = {}
        for i, c in enumerate(palette):
            self.lowest.setdefault(c, i)
            self.highest[c] = i
        self.nearest: dict[tuple[int, int, int], int] = {}

    def __call__(self, r: int, g: int, b: int, a: int) -> int:
        if a == 0:
            return TRANSPARENT
        c = (r, g, b)
        found = (self.lowest if a == OPAQUE else self.highest).get(c)
        if found is not None:
            return found
        near = self.nearest.get(c)
        if near is None:
            near = self.nearest[c] = min(range(len(self.palette)), key=lambda i: (
                (self.palette[i][0] - r) ** 2 + (self.palette[i][1] - g) ** 2 + (self.palette[i][2] - b) ** 2, i))
        return near


def indices(image: dds.Image, palette: Palette) -> bytes:
    index = IndexMap(palette)
    px = image.rgba
    return bytes(index(px[i], px[i + 1], px[i + 2], px[i + 3]) for i in range(0, len(px), 4))


def pcx_file(width: int, height: int, pixels: bytes, palette: Palette, header: bytes) -> bytes:
    """A PCX as 1.17's files are written: `header` holds their bytes 12–15 and 68–69 (DPI and palette info),
    which differ between files; the RLE is the standard one (pcx.encode)."""
    data = bytearray(pcx.encode(width, height, pixels))
    data[12:16], data[68:70] = header[:4], header[4:6]
    data[-768:] = b"".join(bytes(c) for c in palette)
    return bytes(data)


FIT_SAMPLE, FIT_CROP = "sample", "crop"


def fit_mode(w: int, h: int, width: int, height: int) -> str | None:
    """How `fit` brings a w × h image to width × height: an HD image of a whole multiple of the size sampled
    (FIT_SAMPLE), a block-compressed image's padding cut or a shorter image kept (FIT_CROP), or None (it cannot)."""
    if w > width and w % width == 0 and h % height == 0 and w // width == h // height:
        return FIT_SAMPLE
    if width <= w < width + 4 and h < height + 4:
        return FIT_CROP
    return None


def fits(w: int, h: int, width: int, height: int) -> bool:
    """Whether `fit` brings a w × h image to width × height."""
    return fit_mode(w, h, width, height) is not None


def fit(image: dds.Image, width: int, height: int) -> tuple[dds.Image, int]:
    """`image` brought to `width` columns and at most `height` rows, and the rows above it: a block-compressed
    image's padding to multiples of 4 is cut, an HD image of a whole multiple of the size is sampled at each
    block's top left, a shorter image (the consoles: their rows of a 640 × 480 screen) is the bottom."""
    w, h, px = image.width, image.height, image.rgba
    mode = fit_mode(w, h, width, height)
    if mode == FIT_SAMPLE:
        k = w // width
        px = b"".join(px[((y * k) * w + x * k) * 4:((y * k) * w + x * k) * 4 + 4]
                      for y in range(height) for x in range(width))
        w, h = width, height
    elif mode == FIT_CROP:
        rows = min(h, height)
        px = b"".join(px[y * w * 4:(y * w + width) * 4] for y in range(rows))
        w, h = width, rows
    if w != width or h > height:
        raise dds.DdsError(f"image {image.width} x {image.height} does not fit {width} x {height}")
    return dds.Image(w, h, px), height - h


def pcx_from_dds(dds_file: bytes, size: tuple[int, int], palette: Palette, header: bytes) -> bytes:
    """The PCX of `size` from Remastered's image of it (rows above a shorter image are transparent)."""
    width, height = size
    image, above = fit(dds.decode(dds_file), width, height)
    pixels = bytes(width * above) + indices(image, palette)
    return pcx_file(width, height, pixels, palette, header)


def describe_pcx(data: bytes) -> dict:
    """The manifest's extras of an original PCX: its pixels' and colours' hashes and its varying header bytes."""
    image = pcx.decode(data)
    palette = b"".join(bytes(c) for c in image.palette)
    return {"pixels": plane_hash(image.width, image.height, image.pixels),
            "rgb": plane_hash(image.width, image.height, rgb_plane(image.pixels, image.palette)),
            "palette": hashlib.sha256(palette).hexdigest(),
            "size": [image.width, image.height], "header": (data[12:16] + data[68:70]).hex(),
            "_palette_rgb": palette}


# String tables. A TBL is a u16 count, u16 offsets and the strings' spans, each up to the next offset: a span may
# hold NULs inside (a hotkey letter, then its label) and ends with one. Remastered keeps only stat_txt as a TBL
# (without 1.17's last 8 strings); the others are XML (`<string><id/><value/></string>`) in its own order, with
# 0x07 for a NUL inside a span (0x07 is also a colour code, so the manifest names the NULs' positions).
TBL_NUL_IN_XML = b"\x07"
XML_STRING = re.compile(rb"<string>\s*<id>(.*?)</id>\s*<value>(.*?)</value>\s*</string>", re.S)
XML_ENTITIES = {b"&lt;": b"<", b"&gt;": b">", b"&amp;": b"&", b"&quot;": b'"', b"&apos;": b"'"}


def tbl_spans(data: bytes) -> list[bytes]:
    """Each string's bytes up to the next one (the last to the end), its final NUL removed."""
    count = struct.unpack_from("<H", data)[0]
    offsets = struct.unpack_from(f"<{count}H", data, 2)
    if list(offsets) != sorted(offsets) or (count and offsets[0] != 2 + 2 * count):
        raise ValueError("TBL strings are not laid out in order")
    ends = list(offsets[1:]) + [len(data)]
    return [data[a:b].removesuffix(b"\0") for a, b in zip(offsets, ends)]


def tbl_encode(spans: list[bytes]) -> bytes:
    offsets, at = [], 2 + 2 * len(spans)
    for s in spans:
        offsets.append(at)
        at += len(s) + 1
    return struct.pack(f"<H{len(spans)}H", len(spans), *offsets) + b"".join(s + b"\0" for s in spans)


def xml_values(raw: bytes) -> list[tuple[str, bytes]]:
    """(id, value) of Remastered's string XML, the value in the file's UTF-8 with its entities undone."""
    return [(m.group(1).decode("ascii"), re.sub(rb"&(lt|gt|amp|quot|apos);", lambda k: XML_ENTITIES[k.group(0)], m.group(2)))
            for m in XML_STRING.finditer(raw)]


def xml_strings(raw: bytes) -> list[tuple[str, bytes]]:
    """(id, value) of Remastered's string XML, the value in Windows-1252 as the TBLs are."""
    return [(key, value.decode("utf-8").encode("cp1252", "replace")) for key, value in xml_values(raw)]


def span_in_xml(span: bytes) -> bytes:
    return span.replace(b"\0", TBL_NUL_IN_XML)


def span_from_xml(value: bytes, nuls: list[int]) -> bytes:
    out = bytearray(value)
    for i in nuls:
        if i < len(out) and out[i:i + 1] == TBL_NUL_IN_XML:
            out[i] = 0
    return bytes(out)


def align_ids(spans: list[bytes], strings: list[tuple[str, bytes]], fill: bool = True,
              min_ratio: float = 0.0) -> list[str | None]:
    """The XML id of each TBL string: the longest common runs of equal text in order, then (with `fill`) each string
    left over takes the most similar unused id between its matched neighbours, at least `min_ratio` similar
    (Remastered edited some texts)."""
    values = [span_in_xml(s) for s in spans]
    xml_values = [v for _, v in strings]
    ids: list[str | None] = [None] * len(spans)
    taken: list[int | None] = [None] * len(spans)
    for a, b, n in difflib.SequenceMatcher(None, values, xml_values, autojunk=False).get_matching_blocks():
        for k in range(n):
            ids[a + k], taken[a + k] = strings[b + k][0], b + k
    used = {t for t in taken if t is not None}
    for i, t in enumerate(taken):
        if t is not None:
            continue
        lo = max((x for x in taken[:i] if x is not None), default=-1)
        hi = min((x for x in taken[i + 1:] if x is not None), default=len(strings))
        choices = [j for j in range(lo + 1, hi) if j not in used] if fill else []
        if choices:
            def ratio(j: int) -> float:
                return difflib.SequenceMatcher(None, values[i], xml_values[j]).ratio()

            j = max(choices, key=lambda j: (ratio(j), -j))
            if ratio(j) >= min_ratio:
                ids[i], taken[i] = strings[j][0], j
                used.add(j)
    return ids


def tbl_from_remastered(ids: list[str | None], nuls: dict[int, list[int]], strings: list[tuple[str, bytes]],
                        base: list[bytes] | None = None) -> bytes:
    """A TBL of len(ids) strings: those of `base` (Remastered's own TBL spans) where it has them, the others from
    the XML by id; an id it lacks gives an empty string."""
    by_id = dict(strings)
    spans = []
    for i, key in enumerate(ids):
        if base is not None and i < len(base):
            spans.append(base[i])
        else:
            spans.append(span_from_xml(by_id.get(key, b""), nuls.get(i, [])) if key else b"")
    return tbl_encode(spans)


# Dialog BINs. Remastered has none; the manifest keeps each one's bytes with every string zeroed (records, SMK nodes:
# numbers) and where its strings were: the pointer fields that name them, their length and where to find the text
# (a Remastered string id, or the string itself when it is a name: a dialog's or a file's).
@dataclass(frozen=True)
class DialogText:
    offset: int
    length: int
    pointers: tuple[int, ...]  # the record (+0x14) and SMK node (+0xA) fields that hold `offset`
    source: str  # "id:<file>/<id>" or "name:<text>"
    hotkey: bool  # stored with an upper case first letter that a hotkey record's loader lowercases


def dialog_strings(data: bytes) -> list[tuple[int, int, list[int], bool, bool]]:
    """(offset, length, pointer fields, hotkey, is a name) of every string a dialog BIN names."""
    found: dict[int, list] = {}
    for at, _ in dialog._order(data):
        v = dialog.RECORD.unpack_from(data, at)
        string, flags, kind = v[8], v[9], v[12]
        if string:
            name = at == 0 or kind == dialog.TYPE_IMAGE
            e = found.setdefault(string, [string, 0, [], False, name])
            e[2].append(at + 0x14)
            e[3] = e[3] or bool(flags & dialog.FLAG_HOTKEY and data[string] in dialog.LOWERCASE)
        if kind == dialog.TYPE_BUTTON:
            node = v[22]
            while node:
                n = dialog.SMK.unpack_from(data, node)
                if n[3]:
                    e = found.setdefault(n[3], [n[3], 0, [], False, True])
                    e[2].append(node + 0xA)
                node = n[0]
    out = []
    for e in found.values():
        e[1] = data.index(b"\0", e[0]) - e[0]
        out.append((e[0], e[1], e[2], e[3], e[4]))
    return sorted(out)


def dialog_skeleton(data: bytes) -> bytes:
    out = bytearray(data)
    for offset, length, *_ in dialog_strings(data):
        out[offset:offset + length] = bytes(length)
    return bytes(out)


def dialog_rebuild(skeleton: bytes, texts: list[tuple[DialogText, bytes]]) -> bytes:
    """The BIN with each string written back: in its place when it has its length, else appended at the end with
    its pointers moved there (the original's slot stays zero)."""
    out = bytearray(skeleton)
    for t, text in texts:
        if t.hotkey and text[:1].islower():
            text = text[:1].upper() + text[1:]  # stored upper case: the loader lowercases it (dialog.parse)
        if len(text) == t.length:
            out[t.offset:t.offset + t.length] = text
            continue
        at = len(out)
        out += text + b"\0"
        for p in t.pointers:
            struct.pack_into("<I", out, p, at)
    return bytes(out)


BYTE_MAX = 0xFF


class EscapeError(ConverterError, ValueError):
    """A string's escape names no byte."""


def unescape_rc3(value: bytes, byte=lambda code: bytes([code])) -> bytes:
    """RC3.xml (the dialogs' texts) writes control bytes as `\\\\N` (or `\\N`) and line breaks and tabs alike, as
    `\\\\n` (or `\\n`): the second backslash goes too (a line ending in a stray backslash otherwise). A code has up to
    3 digits (`\\\\0041`: byte 4, then the text's 1); `byte` gives the bytes of a code. EscapeError past a byte."""
    def code_bytes(m: re.Match[bytes]) -> bytes:
        code = int(m.group(1))
        if code > BYTE_MAX:
            raise EscapeError(f"the escape {m.group(0)!r} is past a byte")
        return byte(code)

    value = re.sub(rb"\\\\?(\d{1,3})", code_bytes, value)
    return re.sub(rb"\\\\?([nt])", lambda m: b"\n" if m.group(1) == b"n" else b"\t", value)


def unescape_utf8(value: bytes) -> bytes:
    """A language's XML value (every table's is escaped as RC3.xml's): a code past ASCII (`\\\\169`, the copyright sign)
    is its Windows-1252 character in UTF-8."""
    return unescape_rc3(value, lambda code: bytes([code]) if code < 0x80 else bytes([code]).decode("cp1252", "replace").encode())


GRP_HEADER = struct.Struct("<3H")
GRP_FRAME = struct.Struct("<BBBBI")


def uncompressed_grp_layout(data: bytes) -> dict | None:
    """The frame table of a dialog GRP whose frames are plain rows (the glue's Dlg.grp; frames may share rows),
    or None for a row-compressed GRP."""
    count, width, height = GRP_HEADER.unpack_from(data)
    frames = [GRP_FRAME.unpack_from(data, GRP_HEADER.size + GRP_FRAME.size * i) for i in range(count)]
    if not frames or any(off + w * h > len(data) for _, _, w, h, off in frames):
        return None
    try:
        grp.decode(data)
        return None
    except grp.GrpError:
        return {"box": [width, height], "frames": [list(f) for f in frames]}


def uncompressed_grp(layout: dict, frames: list[bytes], size: int) -> bytes:
    """A plain-rows GRP of `layout` whose frame k holds `frames[k]` (frames sharing rows keep the first's)."""
    out = bytearray(size)
    rows = layout["frames"]
    GRP_HEADER.pack_into(out, 0, len(rows), *layout["box"])
    written = set()
    for k, (x, y, w, h, off) in enumerate(rows):
        GRP_FRAME.pack_into(out, GRP_HEADER.size + GRP_FRAME.size * k, x, y, w, h, off)
        if off not in written:
            out[off:off + w * h] = frames[k][:w * h]
            written.add(off)
    return bytes(out)


def describe(name: str, data: bytes) -> dict | None:
    """What the manifest keeps of an original file besides its hash, so that it can be rebuilt and judged."""
    ext = name.lower().rsplit(".", 1)[-1]
    if ext == "pcx":
        return describe_pcx(data)
    if ext == "grp":
        layout = uncompressed_grp_layout(data)
        if layout is not None:
            layout["pixels"] = hashlib.sha256(data[GRP_HEADER.size + GRP_FRAME.size * len(layout["frames"]):]).hexdigest()
        return layout
    if ext == "smk":
        h, frames = smk.decode(data)
        used = set()
        for f in frames:
            used.update(f.indices)
        out = {"smk": [h.width, h.height, h.frames, int(h.ring_frame), struct.unpack_from("<i", data, 16)[0]]}
        if len({f.palette for f in frames}) == 1:  # one palette: maybe its screen's (screen_palette)
            out["_palette_rgb"], out["_used"] = frames[0].palette, sorted(used)
        return out
    return None


SCREEN_PALETTE_TOLERANCE = 4  # a glue movie's palette is its screen's but for rounding (6-bit entries)


def screen_palette(palette: bytes, used: list[int], palettes: dict[str, bytes]) -> str | None:
    """The PCX palette (by hash) a movie's indices are drawn with: the one whose colours at the indices the movie uses
    differ least from its own, if by no more than SCREEN_PALETTE_TOLERANCE (a portrait has a palette of its own)."""
    best, best_diff = None, SCREEN_PALETTE_TOLERANCE + 1
    for sha, other in sorted(palettes.items()):
        diff = max((abs(palette[3 * i + c] - other[3 * i + c]) for i in used for c in range(3)), default=0)
        if diff < best_diff:
            best, best_diff = sha, diff
    return best


# The manifest's text tables, made once from both installs (scr-manifest --scr): string ids and NUL positions of the
# TBLs, the dialogs' skeletons and where their texts are in Remastered. Ids and names only, no text.
RC3 = "SD/rez/RC3.xml"


def remastered_xml(read, path: str) -> list[tuple[str, bytes]]:
    strings = xml_strings(read(path))
    if path.lower().endswith("/rc3.xml"):
        strings = [(k, unescape_rc3(v)) for k, v in strings]
    return strings


def string_table(spans: list[bytes], xml_path: str, strings: list[tuple[str, bytes]], base: str | None) -> dict:
    nuls = {str(i): [k for k, ch in enumerate(s) if ch == 0] for i, s in enumerate(spans) if b"\0" in s}
    return {"xml": xml_path, "base": base, "ids": align_ids(spans, strings), "nuls": nuls}


DIALOG_TEXT_RATIO = 0.6  # an edited label at least this similar is taken; a replaced one is not ("Collections")


def dialog_table(data: bytes, own: list[tuple[str, bytes]], everywhere: dict[bytes, str]) -> dict:
    """A dialog's skeleton and texts: names as they are; the rest equal texts of its own RC3 entries in order, else
    of any Remastered string, else its own entry in that place if similar enough (an edit, not a new label); a
    text found nowhere stays empty."""
    strings = dialog_strings(data)
    wanted = [(i, data[off:off + length]) for i, (off, length, _, _, name) in enumerate(strings) if not name]
    dialog.parse(data)  # rejects a malformed BIN
    texts_only = [t for _, t in wanted]
    equal = align_ids(texts_only, own, fill=False) if own else [None] * len(wanted)
    similar = align_ids(texts_only, own, min_ratio=DIALOG_TEXT_RATIO) if own else [None] * len(wanted)
    sources: dict[int, str] = {}
    for (i, text), key, near in zip(wanted, equal, similar):
        if key is not None:
            sources[i] = f"id:{RC3}#{key}"
        elif text in everywhere:
            sources[i] = everywhere[text]
        elif near is not None:
            sources[i] = f"id:{RC3}#{near}"
    texts = []
    for i, (off, length, pointers, hotkey, name) in enumerate(strings):
        source = f"name:{data[off:off + length].decode('latin-1')}" if name else sources.get(i, "")
        texts.append([off, length, pointers, hotkey, source])
    return {"skeleton": base64.b64encode(zlib.compress(dialog_skeleton(data), 9)).decode("ascii"), "texts": texts}


def dialog_from_table(table: dict, text_of) -> bytes:
    """The BIN of a dialog table; `text_of(source)` gives a Remastered text (b"" when it has none)."""
    skeleton = zlib.decompress(base64.b64decode(table["skeleton"]))
    texts = []
    for off, length, pointers, hotkey, source in table["texts"]:
        t = DialogText(off, length, tuple(pointers), source, hotkey)
        texts.append((t, source[5:].encode("latin-1") if source.startswith("name:") else text_of(source)))
    return dialog_rebuild(skeleton, texts)


def remap_grp(image: grp.Grp, source_palette: bytes, palette: Palette) -> bytes:
    """A row-compressed GRP drawn in another palette: each opaque pixel takes its colour's index in `palette`
    (`source_palette`: 256 entries of R, G, B and a fourth byte, a WPE)."""
    index = IndexMap(palette)
    colours = [tuple(source_palette[4 * i:4 * i + 3]) for i in range(256)]
    frames = []
    for f in image.frames:
        indices = bytes(index(*colours[i], OPAQUE) if m == grp.OPAQUE else 0 for i, m in zip(f.indices, f.mask))
        frames.append(grp.Frame(f.x, f.y, f.width, f.height, indices, f.mask))
    return grp.encode(image.width, image.height, frames)
