"""The interface's tables of StarCraft.exe and Local.dll (`defs/ui/tables-<profile>.json`).

Plan phase 9 (docs/phase9.md): the interface's rules live in kp_core's kp_ui layer, which dispatches a button's
condition and action, a status layout's refresh and draw, by the original's function address. The manifest keeps the
executable's tables as data, checked against the executable by converter/tests/test_ui_tables.py; `extract` reads
them from the binaries (`python3 -m converter.ui_tables EXE [LOCAL_DLL]` rewrites the manifest), `load` reads the
manifest. Function names come from the research atlas (docs/research/atlas, every function there is named).

Tables:
- button sets `0x50ae98[250]` (12 bytes: count, buttons, linked set) with their 20-byte buttons (atlas P35);
- the status panel's unit-type layouts `0x50ba50[228]` (12 bytes: id, needsRefresh, draw; P36);
- the console's fluff rectangles `0x50d7b0[4]` (Zerg, Terran, Protoss, replay; records {u16 x, y, w, h, u32 dialog},
  x = 0xffff ends a list; P36 `0x4a2660`);
- the cursors `0x5031c0[19]` (cursor\\<name>) and the edge-scroll table `0x50320c[9]` (P13);
- fixed screen rectangles: the command card `0x5033d0` (P16) and the minimap `0x508bb0` (P13);
- the music tracks `0x508c58[25]` (8 bytes: file, kind, fade-in, next; P24);
- the attenuation table `0x4efc00[100]` (P35);
- the accelerator tables of Local.dll (101, 102, 103) and the executable (113) (RT_ACCELERATOR, 8-byte entries
  {u16 flags, u16 key, u16 command, u16 pad}; P16, L01);
- the campaign missions' establishing texts `0x4ebba8[64]` and the finale text `0x4ebda8[1]` ({char *name, u16 mission
  key}: rez\\<name>.txt of RunCreditsScreen 0x415010; P05 `0x414f70`, `0x414fc0`).
"""
from __future__ import annotations

import json
import re
import struct
import sys
from pathlib import Path

from .pe import PeImage
from .provenance import DEFS_ROOT

BUTTON_SETS, BUTTON_SET_COUNT = 0x50AE98, 250
BUTTON = struct.Struct("<HHIIHHHH")  # position, icon, condition, action, condition value, action value, strings
STATUS_LAYOUTS, STATUS_LAYOUT_COUNT = 0x50BA50, 228
FLUFF_LISTS, FLUFF_LIST_COUNT, FLUFF = 0x50D7B0, 4, struct.Struct("<HHHHI")
CURSORS, CURSOR_COUNT, EDGE_SCROLL = 0x5031C0, 19, 0x50320C
RECTS = {"command_card": 0x5033D0, "minimap": 0x508BB0}
MUSIC, MUSIC_COUNT = 0x508C58, 25
ATTENUATION, ATTENUATION_COUNT = 0x4EFC00, 100
RT_ACCELERATOR = 9
ESTABLISHING, ESTABLISHING_COUNT, FINALE = 0x4EBBA8, 64, 0x4EBDA8
ACCELERATORS = (("Local.dll", 101), ("Local.dll", 102), ("Local.dll", 103), ("StarCraft.exe", 113))
ATLAS = Path(__file__).resolve().parents[1] / "docs" / "research" / "atlas"
PROFILE = "bw-1.17.0.1"  # the profile whose executable the manifest describes (defs/ui/bw-1.17.0.1.json likewise)


def atlas_names() -> dict[int, str]:
    names = {}
    for f in sorted(ATLAS.glob("*.md")):
        for m in re.finditer(r"^### 0x00([0-9a-f]{6}) `([A-Za-z0-9_]+)`", f.read_text(), re.M):
            names[int(m.group(1), 16)] = m.group(2)
    return names


def _cstr(pe: PeImage, va: int) -> str:
    raw = pe.read_va(va, 260)
    return raw[:raw.index(0)].decode("latin-1")


def _functions(addresses, names: dict[int, str]) -> list[list]:
    return [[f"{a:#x}", names.get(a, "")] for a in sorted(set(addresses))]


def extract(exe: PeImage, local_dll: PeImage | None) -> dict:
    names = atlas_names()
    sets, conditions, actions = [], [], []
    for k in range(BUTTON_SET_COUNT):
        count, array, link = struct.unpack("<III", exe.read_va(BUTTON_SETS + 12 * k, 12))
        buttons = [list(BUTTON.unpack(exe.read_va(array + BUTTON.size * i, BUTTON.size))) for i in range(count)]
        for b in buttons:
            conditions.append(b[2])
            actions.append(b[3])
            b[2], b[3] = f"{b[2]:#x}", f"{b[3]:#x}"
        sets.append({"array": f"{array:#x}", "link": link, "buttons": buttons})
    layouts, refresh, draw = [], [], []
    for k in range(STATUS_LAYOUT_COUNT):
        type_id, r, d = struct.unpack("<III", exe.read_va(STATUS_LAYOUTS + 12 * k, 12))
        refresh.append(r)
        draw.append(d)
        layouts.append([type_id, f"{r:#x}", f"{d:#x}"])
    fluff = []
    for p in struct.unpack(f"<{FLUFF_LIST_COUNT}I", exe.read_va(FLUFF_LISTS, 4 * FLUFF_LIST_COUNT)):
        rects = []
        while True:
            x, y, w, h, _ = FLUFF.unpack(exe.read_va(p + FLUFF.size * len(rects), FLUFF.size))
            if x == 0xFFFF:
                break
            rects.append([x, y, w, h])
        fluff.append({"va": f"{p:#x}", "rects": rects})
    cursors = [_cstr(exe, va) for va in struct.unpack(f"<{CURSOR_COUNT}I", exe.read_va(CURSORS, 4 * CURSOR_COUNT))]
    def texts(va: int, count: int) -> list:
        out = []
        for k in range(count):
            name, key = struct.unpack("<IH", exe.read_va(va + 8 * k, 6))
            if name:
                out.append([_cstr(exe, name), key])
        return out

    music = []
    for k in range(MUSIC_COUNT):
        f, kind, fade, nxt, _ = struct.unpack("<IBBBB", exe.read_va(MUSIC + 8 * k, 8))
        music.append([_cstr(exe, f) if f else "", kind, fade, nxt])
    accelerators = []
    for module, rid in ACCELERATORS:
        pe = exe if module == "StarCraft.exe" else local_dll
        if pe is None:
            continue
        raw = pe.resource(RT_ACCELERATOR, rid)
        accelerators.append({"module": module, "id": rid,
                             "entries": [list(struct.unpack_from("<HHH", raw, o)) for o in range(0, len(raw), 8)]})
    return {
        "id": "ui-tables",
        "provenance": [
            "StarCraft.exe 1.17.0.1 and Local.dll (L01) tables, read by converter/ui_tables.py and checked against "
            "the executable by converter/tests/test_ui_tables.py; function names from docs/research/atlas"],
        "button_sets_va": f"{BUTTON_SETS:#x}",
        "button": ["position", "icon", "condition", "action", "condition value", "action value", "enabled string",
                   "disabled string"],
        "button_sets": sets,
        "conditions": _functions(conditions, names),
        "actions": _functions(actions, names),
        "status_layouts_va": f"{STATUS_LAYOUTS:#x}",
        "status_layout": ["id", "needsRefresh", "draw"],
        "status_layouts": layouts,
        "status_refresh": _functions(refresh, names),
        "status_draw": _functions(draw, names),
        "fluff_lists_va": f"{FLUFF_LISTS:#x}",
        "fluff_races": ["zerg", "terran", "protoss", "replay"],
        "fluff": fluff,
        "cursors": cursors,
        "edge_scroll": list(struct.unpack("<9H", exe.read_va(EDGE_SCROLL, 18))),
        "rects": {k: list(struct.unpack("<4i", exe.read_va(va, 16))) for k, va in RECTS.items()},
        "music": music,
        "attenuation": list(struct.unpack(f"<{ATTENUATION_COUNT}i", exe.read_va(ATTENUATION, 4 * ATTENUATION_COUNT))),
        "accelerators": accelerators,
        "establishing": texts(ESTABLISHING, ESTABLISHING_COUNT),
        "finale": texts(FINALE, 1),
    }


def manifest_path(profile_id: str) -> Path:
    return DEFS_ROOT / "ui" / f"tables-{profile_id}.json"


def load(profile_id: str) -> dict:
    return json.loads(manifest_path(profile_id).read_text())


def dump(tables: dict) -> str:
    """The manifest's text: one line per button set, layout and table row, so that diffs stay readable."""
    out = ["{"]
    items = list(tables.items())
    for i, (k, v) in enumerate(items):
        sep = "," if i + 1 < len(items) else ""
        if isinstance(v, list) and v and isinstance(v[0], (list, dict)):
            out.append(f" {json.dumps(k)}: [")
            out += [f"  {json.dumps(x)}{',' if j + 1 < len(v) else ''}" for j, x in enumerate(v)]
            out.append(f" ]{sep}")
        else:
            out.append(f" {json.dumps(k)}: {json.dumps(v)}{sep}")
    out.append("}")
    return "\n".join(out) + "\n"


def main(argv: list[str]) -> int:
    exe = PeImage.load(argv[0])
    local_dll = PeImage.load(argv[1]) if len(argv) > 1 else None
    manifest_path(PROFILE).write_text(dump({**extract(exe, local_dll), "profile": PROFILE}))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
