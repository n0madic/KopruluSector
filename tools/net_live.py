#!/usr/bin/env python3
"""The mixed lobby (phase N, stage N3.5): Koprulu and the emulated 1.17 in one lobby over the network oracle's loopback
bridge, to the same start.

Each run starts the emulator (tools/research/sc_emulator net --bridge BASE --realtime) and Koprulu's live test
(kp_test test_net_live, tests/c/test_net_live.c) at once:

- join: the emulated host Alpha and joiner Gamma; Koprulu joins as Beta without the map and downloads it from Alpha
  (the original's upload). Alpha waits JOIN_HOST_WAIT_MS in the full room before its Start, for the transfer;
- host: Koprulu hosts as Beta; the emulated Alpha joins without the map and downloads it from Koprulu.

Then the starts are compared: Koprulu's (the test's KOPRULU_NET_LIVE_OUT) with each emulated player's (net.py's
setup.json, taken at StartGame 0x443f40):

- the seed (SetInitialSeed 0x4781f0, from the host's 0x48) and the template (the header's +0x6d);
- the game header, all but its +0 game ID and its +0x24 player count (a joiner's are those of its game list's row
  at its join);
- the eight slot records; the four after them only between seats of the same role (the host fills them, a joiner
  leaves them zero);
- the map each seat plays: Koprulu's download (its size and the transfer's CRC-32, ComputeBlockChecksum 0x413ab0:
  zlib's without the final inversion) and each emulated download (net.py keeps a player's written maps in files/)
  against the map file.

Koprulu leaves Storm's game after the start (its network game is stage N4's), so the emulated players play on alone to
GAME_FRAMES. The recordings stay in koprulu-private/emu/net-live/ (never committed).

    python3 tools/net_live.py [--map MAP] [--base PORT] [join] [host]
"""
from __future__ import annotations

import argparse
import json
import logging
import os
import subprocess
import sys
import time
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
EMULATOR_CWD = ROOT / "tools" / "research"
DEFAULT_MAP = ROOT / "koprulu-private" / "full" / "app" / "maps" / "(4)Lost Civilization.scm"  # room for three
DEFAULT_OUT = ROOT / "koprulu-private" / "emu" / "net-live"
DEFAULT_PYTHON = ROOT / "koprulu-private" / "venv" / "bin" / "python"
DEFAULT_KP_TEST = ROOT / "build" / "cmake" / "kp_test"
DEFAULT_BASE = 41000
GAME_TYPE = "Melee"
GAME_FRAMES = 24              # the emulated players' game after the start: its first frames
JOIN_HOST_WAIT_MS = 15000     # the emulated host's wait in the full room before its Start (the map's transfer)
LOBBY_TIMEOUT_S = 600         # a lobby step of the emulator's driver, in virtual seconds
MAX_WALL_S = 1800
BRIDGE_WAIT_S = 120           # the emulator's spec.json (its bridge bound) within
EMULATOR_EXIT_S = 900         # the emulator done after Koprulu's test
TEMPLATE_AT, TEMPLATE_SIZE = 0x6D, 32
HEADER_JOINER_OWN = (0, 1, 2, 3, 0x24)  # the game ID and the player count: a joiner's game list row's at its join
SLOTS = 8                     # the slot records every seat shares
NAME = "Beta"                 # Koprulu's player (test_net_live.c)
OFFERED_PREFIX = "!"          # a host offers the map as "!<name>" (net.py's preload, test_net_live.c's host): the
                              # install holds it under its own name, which a joiner would find and not download

RUNS = {
    "join": {"players": "Alpha@10.0.0.1:host,Gamma@10.0.0.2:join,Beta@10.0.1.1:external",
             "actions": f"Alpha:lobby:wait:{JOIN_HOST_WAIT_MS}", "no_map": "", "hosts": 2, "test": "lobby_join",
             "room": 3},
    "host": {"players": "Beta@10.0.1.1:external,Alpha@10.0.0.1:join", "actions": "", "no_map": "Alpha", "hosts": 1,
             "test": "lobby_host", "room": 2},
}

log = logging.getLogger("net_live")


def transfer_crc(data: bytes) -> int:
    """The map transfer's CRC-32 (ComputeBlockChecksum 0x413ab0, kp_transfer_crc): zlib's without the final inversion."""
    return zlib.crc32(data) ^ 0xFFFFFFFF


def compare_starts(koprulu: dict, emulated: dict[str, dict], roles: dict[str, str], map_bytes: bytes,
                   downloads: dict[str, bytes | None]) -> list[str]:
    """The differences of Koprulu's start from each emulated player's (the module's docstring); [] when none.
    emulated: each player's setup.json start_game; roles: each emulated player's role; downloads: each emulated
    player's downloaded map (None: it had the map)."""
    out = []
    header = bytes.fromhex(koprulu["game_header"])
    template = header[TEMPLATE_AT:TEMPLATE_AT + TEMPLATE_SIZE].hex()
    if koprulu.get("download_size"):
        if koprulu["download_size"] != len(map_bytes) or koprulu["download_crc"] != transfer_crc(map_bytes):
            out.append(f"Koprulu's download: {koprulu['download_size']} bytes, CRC {koprulu['download_crc']:#x}; the map "
                       f"{len(map_bytes)} bytes, CRC {transfer_crc(map_bytes):#x}")
    for name, start in emulated.items():
        if "seed" not in start:
            out.append(f"{name}: no seed recorded (net.py's start snapshot)")
        elif start["seed"] != koprulu["seed"]:
            out.append(f"{name}: seed {start['seed']:#x}, Koprulu's {koprulu['seed']:#x}")
        if start["template"] != template:
            out.append(f"{name}: template {start['template']}, Koprulu's {template}")
        theirs = bytes.fromhex(start["game_header"])
        at = next((k for k in range(max(len(theirs), len(header))) if k not in HEADER_JOINER_OWN and
                   (k >= len(theirs) or k >= len(header) or theirs[k] != header[k])), None)
        if at is not None:
            out.append(f"{name}: game header differs at +{at:#x}: {theirs[at:at + 8].hex()}, Koprulu's "
                       f"{header[at:at + 8].hex()}")
        count = len(start["players"]) if roles[name] == koprulu["role"] else SLOTS
        for k in range(count):
            if start["players"][k] != koprulu["players"][k]:
                out.append(f"{name}: player record {k} {start['players'][k]}, Koprulu's {koprulu['players'][k]}")
        got = downloads.get(name)
        if got is not None and got != map_bytes:
            out.append(f"{name}: its download ({len(got)} bytes) differs from the map ({len(map_bytes)} bytes)")
    return out


def emulated_starts(game_dir: Path, map_name: str) -> tuple[dict, dict, dict, list[str]]:
    """Each emulated player's start, role and downloaded map from a run's recording; the players without a start."""
    spec = json.loads((game_dir / "spec.json").read_text())
    starts, roles, downloads, missing = {}, {}, {}, []
    for p in spec["players"]:
        if p["role"] == "external":
            continue
        setup = game_dir / p["name"] / "setup.json"
        start = json.loads(setup.read_text()).get("start_game") if setup.exists() else None
        if start is None:
            missing.append(p["name"])
            continue
        starts[p["name"]], roles[p["name"]] = start, p["role"]
        if not p["has_map"]:
            f = game_dir / p["name"] / "files" / map_name
            downloads[p["name"]] = f.read_bytes() if f.exists() else b""
    return starts, roles, downloads, missing


def run(name: str, a) -> list[str]:
    """One run: the emulator and Koprulu's test at once, then the comparison; the differences ([] none)."""
    r = RUNS[name]
    game = f"live-{name}"
    game_dir = Path(a.out_root) / game
    koprulu_out = game_dir / "koprulu.json"
    game_dir.mkdir(parents=True, exist_ok=True)
    for stale in (game_dir / "spec.json", koprulu_out):
        stale.unlink(missing_ok=True)
    cmd = [str(a.python), "-m", "sc_emulator", "net", str(Path(a.map).resolve()), game, "--players", r["players"],
           "--type", GAME_TYPE, "--bridge", str(a.base), "--realtime", "--frames", str(GAME_FRAMES),
           "--max-wall", str(MAX_WALL_S), "--lobby-timeout", str(LOBBY_TIMEOUT_S), "--out-root", str(a.out_root)]
    if r["actions"]:
        cmd += ["--actions", r["actions"]]
    if r["no_map"]:
        cmd += ["--no-map", r["no_map"]]
    env = dict(os.environ, KOPRULU_NET_BRIDGE=f"{a.base}:{r['hosts']}", KOPRULU_NET_LIVE_MAP=str(Path(a.map).resolve()),
               KOPRULU_NET_LIVE_OUT=str(koprulu_out), KOPRULU_NET_LIVE_PLAYERS=str(r["room"]))
    with open(game_dir / "emulator.log", "w") as emu_log, open(game_dir / "koprulu.log", "w") as kp_log:
        log.info("%s: the emulator (%s)", name, r["players"])
        emu = subprocess.Popen(["nice", "-n", "10", *cmd], cwd=EMULATOR_CWD, stdout=emu_log, stderr=subprocess.STDOUT)
        try:
            until = time.monotonic() + BRIDGE_WAIT_S
            while not (game_dir / "spec.json").exists():  # run_game writes it, then binds the bridge
                if emu.poll() is not None or time.monotonic() > until:
                    return [f"{name}: the emulator did not start (exit {emu.poll()}; {game_dir / 'emulator.log'})"]
                time.sleep(0.5)
            time.sleep(2)
            log.info("%s: Koprulu's test_net_live:%s", name, r["test"])
            try:
                kp = subprocess.run([str(a.kp_test), f"test_net_live:{r['test']}"], cwd=ROOT, env=env,
                                    stdout=kp_log, stderr=subprocess.STDOUT, timeout=MAX_WALL_S)
            except subprocess.TimeoutExpired:
                return [f"{name}: Koprulu's test ran past {MAX_WALL_S} s ({game_dir / 'koprulu.log'})"]
            log.info("%s: Koprulu's test exited %d; waiting for the emulator", name, kp.returncode)
            try:
                emu.wait(timeout=EMULATOR_EXIT_S)
            except subprocess.TimeoutExpired:
                return [f"{name}: the emulator ran {EMULATOR_EXIT_S} s past Koprulu's test ({game_dir / 'emulator.log'})"]
        finally:
            if emu.poll() is None:
                emu.terminate()
                emu.wait()
    if kp.returncode != 0:
        return [f"{name}: Koprulu's test failed ({game_dir / 'koprulu.log'})"]
    koprulu = json.loads(koprulu_out.read_text())
    starts, roles, downloads, missing = emulated_starts(game_dir, OFFERED_PREFIX + Path(a.map).name)
    out = [f"{name}: {p} has no start ({game_dir / p / 'run.log'})" for p in missing]
    out += [f"{name}: {d}" for d in compare_starts(koprulu, starts, roles, Path(a.map).read_bytes(), downloads)]
    if not out:
        log.info("%s: the same start for Koprulu and %s: seed %#x, %d slot records", name, ", ".join(starts),
                 koprulu["seed"], SLOTS)
    return out


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=(__doc__ or "").split("\n\n")[0])
    ap.add_argument("runs", nargs="*", choices=[[], *RUNS], default=[], help="join, host (default both)")
    ap.add_argument("--map", default=str(DEFAULT_MAP), help="the map file both sides play")
    ap.add_argument("--base", type=int, default=DEFAULT_BASE, help="the bridge's first real port")
    ap.add_argument("--out-root", default=str(DEFAULT_OUT))
    ap.add_argument("--python", default=str(DEFAULT_PYTHON), help="the emulator's Python")
    ap.add_argument("--kp-test", default=str(DEFAULT_KP_TEST))
    a = ap.parse_args(argv)
    logging.basicConfig(level=logging.INFO, format="%(message)s")
    failures = []
    for name in a.runs or list(RUNS):
        failures += run(name, a)
    for f in failures:
        log.error("%s", f)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
