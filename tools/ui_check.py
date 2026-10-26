"""The console's panel oracle of plan phase 9 (docs/phase9.md, level 2): kp_ui's panels against the emulator's screens.

Each scenario run under --root (the emulator's `scenario` runs with screen captures, SC_SCREENS: <name>/input.jsonl
and <name>/screens/screen<F>.cap) is replayed by kp_oracle hud_check from its golden
(tests/golden/oracle/scenarios/<name>.json) with kp_ui's console over the game, the interface's work where the run's
input log has it (tools/hud_log.py); every dialog Koprulu paints and the text lines' layer are compared with the
capture's crop at the dialog's rectangle, in palette indices. A dialog whose flags have 0x2000 (index 0 transparent,
BlitDialogToScreen 0x4c5630) is compared where Koprulu's pixel is not 0; the game view shows through the rest. Exit
status 1 when a panel differs.

Usage: python3 tools/ui_check.py --root DIR [--scenario NAME ...] [--png OUT]
"""
from __future__ import annotations

import argparse
import logging
import subprocess
import sys
import tempfile
import zlib
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from tools import oracle_cli  # noqa: E402
from tools.hud_log import parse_file, to_text  # noqa: E402
from tools.research.sc_emulator.uiscreen import ScreenCapture  # noqa: E402

log = logging.getLogger("ui_check")

TRANSPARENT = 0x2000
TRANSLUCENT = 0x40000  # a modeless dialog composited onto the game with its colour A blended (BlitDialogToScreen)
BACKDROP = 0x10000000  # composited onto the backdrop: the other dialogs (the console's) paint over it


def read_panels(path: Path) -> tuple[dict[int, list[dict]], dict[int, dict]]:
    """hud_check's output: {frame: [{bin, left, top, w, h, flags, pixels}, ...]} and the modeless captures'
    dialogs {k: {bin, left, top, w, h, flags, pixels, colour, row}}."""
    data, out, modeless, at, frame, mk = path.read_bytes(), {}, {}, 0, None, None
    while at < len(data):
        nl = data.index(b"\n", at)
        words = data[at:nl].decode().split(" ")
        at = nl + 1
        if words[0] == "frame":
            frame, mk = int(words[1]), None
            out[frame] = []
            continue
        if words[0] == "modeless":
            mk = int(words[1])
            continue
        if words[0] == "blend":
            modeless[mk].update(colour=int(words[1]), row=data[at:at + 256])
            at += 256
            continue
        if words[0] == "console":
            cw, chh = int(words[1]), int(words[2])
            modeless[mk].update(console=(cw, chh, data[at:at + cw * chh]))
            at += cw * chh
            continue
        left, top, w, h, flags = (int(x) for x in words[2:7])
        if mk is not None:
            modeless[mk] = {"bin": words[1], "left": left, "top": top, "w": w, "h": h, "flags": flags,
                            "pixels": data[at:at + w * h]}
            at += w * h
            continue
        out[frame].append({"bin": words[1], "left": left, "top": top, "w": w, "h": h, "flags": flags,
                           "pixels": data[at:at + w * h]})
        at += w * h
    return out, modeless


def tooltip_rect(cap: ScreenCapture) -> tuple[int, int, int, int] | None:
    """The tooltip box the capture shows (layer 1 enabled, the box visible): x, y, w, h."""
    t = cap.header.get("tooltip") or {}
    return tuple(t["rect"]) if t.get("enabled") and t.get("visible") and t["rect"][2] > 0 else None


def compare_modeless(panel: dict, cap: ScreenCapture) -> tuple[int, int, tuple[int, int] | None]:
    """A modeless dialog over the game against its capture: a pixel of its translucent colour must be a value of the
    blend row (the original blends it into the screen bitmap at every repaint, over whatever the paused view and the
    menus before left there, which no capture records), its other pixels are its own; the cursor's rectangle and, for
    a dialog composited onto the backdrop, the console's art (its dialogs paint over it) are left out."""
    w, h, px, colour, row = panel["w"], panel["h"], panel["pixels"], panel.get("colour"), panel.get("row")
    translucent = colour is not None and panel["flags"] & TRANSLUCENT
    blends = set(row) if translucent else set()
    console = panel.get("console") if panel["flags"] & BACKDROP else None
    cur = cap.header.get("cursor") or {}
    cx, cy, cw, ch = cur.get("rect", [0, 0, 0, 0]) if cur.get("enabled") else (0, 0, 0, 0)
    diff, total, first = 0, 0, None
    for y in range(h):
        sy = panel["top"] + y
        for x in range(w):
            sx = panel["left"] + x
            v = px[y * w + x]
            if not (0 <= sx < cap.width and 0 <= sy < cap.height) or v == 0 or (cx <= sx < cx + cw and cy <= sy < cy + ch):
                continue
            if console and sx < console[0] and sy < console[1] and console[2][sy * console[0] + sx]:
                continue  # the console's art: its dialogs paint over a dialog composited onto the backdrop
            got = cap.pixels[sy * cap.width + sx]
            total += 1
            if translucent and v == colour:  # a value of the blend row (result_replay's rule)
                ok = got in blends
            else:
                ok = got == v
            if not ok:
                diff += 1
                first = first or (x, y)
    return diff, total, first


def compare(panel: dict, cap: ScreenCapture) -> tuple[int, int, tuple[int, int] | None]:
    """(differing pixels, compared pixels, first difference's panel coordinates). The cursor's rectangle is left out:
    the capture's surface keeps the cursor drawn (uiscreen.py); so is the tooltip box's over the other panels (it is
    the panel "tooltip" of its own)."""
    w, h, px = panel["w"], panel["h"], panel["pixels"]
    keyed = bool(panel["flags"] & TRANSPARENT)
    cur = cap.header.get("cursor") or {}
    cx, cy, cw, ch = cur.get("rect", [0, 0, 0, 0]) if cur.get("enabled") and panel["bin"] != "cursor" else (0, 0, 0, 0)
    tip = tooltip_rect(cap) if panel["bin"] not in ("tooltip", "cursor") else None
    tx, ty, tw, th = tip or (0, 0, 0, 0)
    diff, total, first = 0, 0, None
    for y in range(h):
        sy = panel["top"] + y
        if not 0 <= sy < cap.height:
            continue
        for x in range(w):
            sx = panel["left"] + x
            v = px[y * w + x]
            if (not 0 <= sx < cap.width or (keyed and v == 0) or (cx <= sx < cx + cw and cy <= sy < cy + ch)
                    or (tx <= sx < tx + tw and ty <= sy < ty + th)):
                continue
            total += 1
            if cap.pixels[sy * cap.width + sx] != v:
                diff += 1
                first = first or (x, y)
    return diff, total, first


def write_png(path: Path, w: int, h: int, pixels: bytes, palette: bytes) -> None:
    rgb = bytearray()
    for v in pixels:
        rgb += palette[4 * v:4 * v + 3]
    raw = b"".join(b"\0" + bytes(rgb[y * w * 3:(y + 1) * w * 3]) for y in range(h))

    def chunk(t: bytes, d: bytes) -> bytes:
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
                     + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))


def check_scenario(run: Path, tool: list[str], png: Path | None) -> int:
    name = run.name
    golden = ROOT / "tests" / "golden" / "oracle" / "scenarios" / f"{name}.json"
    caps = {int(p.stem[len("screen"):]): ScreenCapture(p) for p in sorted((run / "screens").glob("screen*.cap"))}
    modeless_caps = {int(p.stem[len("modeless-"):]): ScreenCapture(p)
                     for p in (run / "screens").glob("modeless-*.cap")}
    if not golden.exists() or not caps:
        log.warning("%s: no golden or no captures, skipped", name)
        return 0
    frames = [str(f) for f in sorted(caps)]
    if modeless_caps:
        sys.path.insert(0, str(ROOT / "tools" / "research"))
        from sc_emulator.hle.kernel32 import TICK_BASE_MS  # noqa: E402
        frames += ["--modeless", ",".join(f"{modeless_caps[k].header['clock_ms'] + TICK_BASE_MS}:{modeless_caps[k].header['frame']}"
                                          for k in sorted(modeless_caps))]
    with tempfile.TemporaryDirectory() as tmp:
        out, hud = Path(tmp) / "panels.bin", Path(tmp) / "hud.txt"
        hud.write_text(to_text(parse_file(run / "input.jsonl")))
        own = ["--run", str(run)] if (run / "commands.jsonl").exists() and (run / "ui.jsonl").exists() else []
        subprocess.run(tool + [str(golden), str(hud), str(out), *frames, *own], cwd=ROOT, check=True)
        panels, modeless = read_panels(out)
    failures = 0
    for k, cap in sorted(modeless_caps.items()):  # the menus and the result dialogs over the game
        want = next((d["ref"][0] for d in cap.header.get("dialogs", []) if isinstance(d["ref"], list)), None)
        p = modeless.get(k)
        label = f"{name} modeless {k} {want}"
        if p is None or want is None or p["bin"].lower() != want.lower():
            failures += 1
            log.error("%s: Koprulu's dialog %s", label, p and p["bin"])
            continue
        diff, total, first = compare_modeless(p, cap)
        if diff:
            failures += 1
            log.error("%s: %d of %d pixels differ (first at %s)", label, diff, total, first)
            if png:
                png.mkdir(parents=True, exist_ok=True)
                stem = f"{name}-modeless{k}-{Path(p['bin'].replace(chr(92), '/')).stem}"
                write_png(png / f"{stem}-got.png", p["w"], p["h"], p["pixels"], cap.palette)
                crop = cap.crop(p["left"], p["top"], p["left"] + p["w"] - 1, p["top"] + p["h"] - 1)
                write_png(png / f"{stem}-want.png", p["w"], p["h"], crop, cap.palette)
        else:
            log.info("%s: %d pixels equal", label, total)
    for frame, cap in sorted(caps.items()):
        cur = cap.header.get("cursor") or {}
        if "tooltip" in cap.header:  # a newer capture says where the tooltip is: Koprulu's must be there too
            want = tooltip_rect(cap)
            got = next(((p["left"], p["top"], p["w"], p["h"]) for p in panels.get(frame, []) if p["bin"] == "tooltip"),
                       None)
            if want != got:
                failures += 1
                log.error("%s frame %d tooltip: %s, the original's %s", name, frame, got, want)
        for p in panels.get(frame, []):
            if p["bin"] == "cursor":  # the cursor's frame where the capture's cursor layer is
                if not cur.get("enabled"):
                    continue
                x, y, w, h = cur["rect"]
                if (w, h) != (p["w"], p["h"]):
                    failures += 1
                    log.error("%s frame %d cursor: %dx%d, the original's %dx%d", name, frame, p["w"], p["h"], w, h)
                    continue
                p = {**p, "left": x, "top": y}
            diff, total, first = compare(p, cap)
            label = f"{name} frame {frame} {p['bin']}"
            if diff:
                failures += 1
                log.error("%s: %d of %d pixels differ (first at %s)", label, diff, total, first)
                if png:
                    png.mkdir(parents=True, exist_ok=True)
                    stem = f"{name}-{frame}-{Path(p['bin'].replace(chr(92), '/')).stem}"
                    write_png(png / f"{stem}-got.png", p["w"], p["h"], p["pixels"], cap.palette)
                    want = cap.crop(p["left"], p["top"], p["left"] + p["w"] - 1, p["top"] + p["h"] - 1)
                    write_png(png / f"{stem}-want.png", p["w"], p["h"], want, cap.palette)
            else:
                log.info("%s: %d pixels equal", label, total)
    return failures


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--root", required=True, type=Path, help="the emulator's scenario runs with screen captures")
    ap.add_argument("--scenario", action="append", default=[], help="only these runs")
    ap.add_argument("--png", type=Path, help="write the differing panels (got/want) here")
    a = ap.parse_args(argv)
    logging.basicConfig(level=logging.INFO, format="%(levelname)s %(message)s")
    runs = [a.root / n for n in a.scenario] if a.scenario else sorted(p for p in a.root.iterdir()
                                                                         if (p / "screens").is_dir())
    tool = oracle_cli.command("hud_check")
    failures = sum(check_scenario(r, tool, a.png) for r in runs)
    print(f"ui_check: {len(runs)} runs, {failures} panels differ")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
