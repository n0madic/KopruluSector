#!/usr/bin/env python3
"""The network oracle's recordings as goldens of kp_snet's replay (phase N, stage N2; tests/c/test_net_trace.c).

A recording is a game of emulated originals on the virtual LAN (tools/research/sc_emulator/net.py, stage N1):
lan.jsonl and lan.pcap hold every datagram with its deliveries, each player's snet.jsonl the executable's calls of
Storm's SNet exports, the events Storm gave its handlers and the passes Storm made, each record with its order of
entry (`seq`) and a call with the order at its return (`end`). Each player becomes one golden entry:

- program: SNetInitializeProvider's program data (hex): the program id at +0xC, the version at +0x10;
- calls: the records in their order, `t` the virtual microsecond each began. A call has its arguments as the replay
  needs them and its result and outputs as the original returned them, and `inner`, what happened while it ran: the
  events it dispatched, each followed by the calls the executable's handler made (with their own `inner`), and
  SNetJoinGame's passes. A pass of Storm's network thread (fn "pump", from where it holds Storm's lock) has whether
  its event was signalled and the wait it returned; one of SNetJoinGame's (fn "join_pass", PumpUntilControl's loop)
  whether it was the first, which no Sleep(10) preceded. Each pass has `take`, the arrivals its ReceivePackets took
  from the provider, and `clock`, its reads of the clock ([caller, the virtual microsecond read]);
- arrivals: the datagrams that reached the player, at their arrival time, by socket (0: port 6111, 1: port 6112), with
  the sender's address; a player's own broadcasts and its datagrams to itself arrive at once, as the stack delivers
  them inside the instance. One of port 6112 has `pass`: the index of the pass that took it among all the passes
  (the provider's threads had queued it by then, as the emulator's scheduler decided, and the replay queues it
  there), -1 for one no pass took; the provider answers port 6111's itself, at once;
- sends: the datagrams the player sent, at their send time, by socket, to the address given.

The replay (test_net_trace) plays one player: Koprulu's SNet and UDP provider get the arrivals and the calls at their
times, on the emulator's clock (each GetTickCount charges TIME_QUERY_COST_US first), its pump runs at the original's
passes, and what it sends, returns, waits and dispatches is compared with the original's.

    python3 tools/net_check.py [--root koprulu-private/emu/net-n2] [GAME ...]

writes tests/golden/oracle/net/<game>.json and the index of the games there.
"""
from __future__ import annotations

import argparse
import json
import logging
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_RECORDINGS = ROOT / "koprulu-private" / "emu" / "net-n2"
GOLDEN = ROOT / "tests" / "golden" / "oracle" / "net"
GOLDEN_VERSION = 5
PCAP_MAGIC, PCAP_LINKTYPE_IPV4 = 0xA1B2C3D4, 228
PCAP_HEADER, PCAP_RECORD, IPV4_UDP_HEADERS = 24, 16, 28
LIST_PORT, GAME_PORT = 6111, 6112
BROADCAST = "255.255.255.255"
# the calls the replay repeats or checks; the others (SNetEnumProviders, SNetEnumDevices, SNetRegisterEventHandler,
# SNetGetPlayerNames, SNetReportGameResult) neither change Storm's state over UDP nor have outputs kp_snet keeps
CALLS = {
    "SNetInitializeProvider", "SNetDestroy", "SNetGetProviderCaps", "SNetEnumGamesEx", "SNetCreateLadderGame",
    "SNetJoinGame", "SNetLeaveGame", "SNetSendTurn", "SNetReceiveTurns", "SNetGetTurnsInTransit", "SNetSendMessage",
    "SNetReceiveMessage", "SNetGetGameInfo", "SNetGetNumPlayers", "SNetGetPlayerCaps", "SNetGetPlayerName",
    "SNetSetGameMode", "SNetDropPlayer", "SNetDisconnectAll",
}
OUTPUTS = ("players", "sender", "data", "info", "num", "name", "in_transit", "caps", "player_id")

log = logging.getLogger("net_check")


def read_pcap(path: Path) -> list[bytes]:
    """The payloads of a VirtualLan pcap (IPv4, UDP), in record order."""
    raw = path.read_bytes()
    if len(raw) < PCAP_HEADER:
        raise ValueError(f"{path}: {len(raw)} bytes, no pcap header (a game still recording?)")
    magic, _, _, _, _, _, link = struct.unpack_from("<IHHiIII", raw, 0)
    if magic != PCAP_MAGIC or link != PCAP_LINKTYPE_IPV4:
        raise ValueError(f"{path}: not a VirtualLan pcap")
    out, pos = [], PCAP_HEADER
    while pos < len(raw):
        if pos + PCAP_RECORD > len(raw):
            raise ValueError(f"{path}: a record header cut at byte {pos}")
        _, _, incl, _ = struct.unpack_from("<IIII", raw, pos)
        pkt = raw[pos + PCAP_RECORD:pos + PCAP_RECORD + incl]
        if len(pkt) != incl or incl < IPV4_UDP_HEADERS:
            raise ValueError(f"{path}: a packet of {incl} bytes cut or too short at byte {pos}")
        pos += PCAP_RECORD + incl
        ulen = struct.unpack_from(">H", pkt, 24)[0]
        out.append(pkt[28:20 + ulen])
    return out


def socket_of(address: str) -> int:
    port = int(address.rsplit(":", 1)[1])
    if port not in (LIST_PORT, GAME_PORT):
        raise ValueError(f"a datagram of port {port}")
    return 0 if port == LIST_PORT else 1


def int_args(rec: dict) -> list[int]:
    return [int(a, 16) for a in rec.get("args", [])]


def call_entry(rec: dict) -> dict:
    """A call as the replay needs it: the integer arguments, strings and data given, the result and outputs."""
    out = {"t": rec["t_us"], "fn": rec["fn"], "result": rec["result"]}
    if "args" in rec:
        out["args"] = int_args(rec)
    for key in ("strings", "template", "program"):
        if key in rec:
            out[key] = rec[key]
    if rec["fn"] == "SNetReceiveTurns" and "args" not in rec:
        raise ValueError(f"SNetReceiveTurns at {rec['t_us']} us without its arguments (net.py before N2's replay)")
    if rec["fn"] in ("SNetSendTurn", "SNetSendMessage"):
        out["given"] = rec.get("data", "")
    elif not rec["result"] and "status" in rec:
        out["status"] = rec["status"]
    elif rec["result"]:
        for key in OUTPUTS:
            if key in rec:
                out[key] = rec[key]
    return out


def player_golden(d: Path, name: str, ip: str, datagrams: list[dict], payloads: list[bytes]) -> dict:
    who = f"{d.name}/{name}"
    recs = [json.loads(line) for line in (d / name / "snet.jsonl").read_text().splitlines()]
    if any("seq" not in r for r in recs):
        raise ValueError(f"{who}: the records' order is not recorded (net.py before N2's replay)")
    recs.sort(key=lambda r: r["seq"])
    calls, passes, program = tree(who, recs)
    if not program:
        raise ValueError(f"{who}: SNetInitializeProvider's program data is not recorded")
    if not any(p["fn"] == "pump" for p in passes):
        raise ValueError(f"{who}: the network thread's passes are not recorded")
    arrivals, sends = [], []
    for rec, data in zip(datagrams, payloads):
        hexdata = data.hex()
        if len(data) != rec["len"]:
            raise ValueError(f"{d.name}: datagram {rec['n']} is {len(data)} bytes in the pcap, {rec['len']} recorded")
        if rec["from"] == name:
            sends.append({"t": rec["t"], "socket": socket_of(rec["src"]), "to": rec["dst"], "data": hexdata})
            dst_ip = rec["dst"].rsplit(":", 1)[0]
            if rec.get("local") or dst_ip in (BROADCAST, ip):
                arrivals.append({"t": rec["t"], "socket": socket_of(rec["dst"]), "from": rec["src"], "data": hexdata})
        for to in rec.get("to", []):
            if to.get("host") == name and "at" in to:
                arrivals.append({"t": to["at"], "socket": socket_of(rec["dst"]), "from": rec["src"], "data": hexdata})
    arrivals.sort(key=lambda a: a["t"])  # stable: by arrival, then by send order
    taken_by(who, passes, arrivals)
    return {"name": name, "ip": ip, "program": program, "calls": calls, "arrivals": arrivals, "sends": sends}


def tree(who: str, recs: list[dict]) -> tuple[list[dict], list[dict], str]:
    """The records (in their order) as the replay runs them: the top level's calls and passes, each call with what
    happened inside it (a record whose seq lies between the call's seq and end); every pass in order; the program
    data."""
    top: list[dict] = []
    open_calls: list[tuple[int, list]] = []  # (end, where what happens inside goes) of the calls running
    passes: list[dict] = []
    program = ""
    for rec in recs:
        while open_calls and open_calls[-1][0] < rec["seq"]:
            open_calls.pop()
        where = open_calls[-1][1] if open_calls else top
        fn = rec["fn"]
        if fn == "event":
            where.append({"t": rec["t_us"], "fn": "event", "id": rec["id"], "player": rec["player"],
                          "data": rec["data"]})
        elif fn in ("pump", "join_pass"):
            item = {"t": rec["t_us"], "fn": fn}
            if fn == "pump":
                item.update(signalled=rec["signalled"], wait=rec["wait"])
            else:
                item["first"] = rec["first"]
            item["received"] = rec["received"]
            item["clock"] = rec.get("clock", [])
            where.append(item)
            passes.append(item)
        elif fn == "taken":
            raise ValueError(f"{who}: Storm took a datagram outside its passes at {rec['t_us']} us")
        elif "end" not in rec:
            raise ValueError(f"{who}: {fn} at {rec['t_us']} us has no end")
        elif fn in CALLS:
            if fn == "SNetInitializeProvider":
                program = rec.get("program", "")
            if fn == "SNetCreateLadderGame" and "template" not in rec:
                raise ValueError(f"{who}: SNetCreateLadderGame's template is not recorded")
            item = call_entry(rec)
            item["inner"] = []
            where.append(item)
            open_calls.append((rec["end"], item["inner"]))
        else:  # not replayed: what happened inside it belongs to its caller
            open_calls.append((rec["end"], where))
    prune(top)
    return top, passes, program


def prune(items: list[dict]) -> None:
    for c in items:
        if "inner" in c:
            prune(c["inner"])
            if not c["inner"]:
                del c["inner"]


def taken_by(who: str, passes: list[dict], arrivals: list[dict]) -> None:
    """Which arrival each datagram a pass took from the provider was: the pass gets `take` (arrival indices), the
    arrival `pass` (the pass's index); a port-6112 arrival no pass took gets -1. The provider's queue is first in,
    first out, so they are the port-6112 arrivals in order (two receive threads could queue two close datagrams the
    other way round: the next few are searched)."""
    queue = [k for k, a in enumerate(arrivals) if a["socket"] == 1]
    taken = [False] * len(queue)
    first = 0
    for n, p in enumerate(passes):
        p["take"] = []
        for r in p.pop("received"):
            while first < len(queue) and taken[first]:
                first += 1
            k = next((k for k in range(first, min(first + 4, len(queue))) if not taken[k] and
                      arrivals[queue[k]]["from"] == r["from"] and arrivals[queue[k]]["data"] == r["data"]), -1)
            if k < 0:
                raise ValueError(f"{who}: Storm took {r['data'][:24]}... from {r['from']} at {p['t']} us, which is not "
                                 f"among the next arrivals")
            if k != first:
                log.info("%s: the provider queued arrival %d before the earlier %d", who, queue[k], queue[first])
            taken[k] = True
            arrivals[queue[k]]["pass"] = n
            p["take"].append(queue[k])
    for k, a in enumerate(queue):
        if not taken[k]:
            arrivals[a]["pass"] = -1


def count(items: list[dict], fn: str) -> int:
    return sum((c["fn"] == fn) + count(c.get("inner", []), fn) for c in items)


def game_golden(d: Path) -> dict:
    spec = json.loads((d / "spec.json").read_text())
    datagrams = [json.loads(line) for line in (d / "lan.jsonl").read_text().splitlines()]
    payloads = read_pcap(d / "lan.pcap")
    if len(datagrams) != len(payloads):
        raise ValueError(f"{d.name}: {len(datagrams)} datagrams in lan.jsonl, {len(payloads)} in lan.pcap")
    result = json.loads((d / "net.json").read_text())
    players = [player_golden(d, p["name"], p["ip"], datagrams, payloads) for p in spec["players"]]
    return {"version": GOLDEN_VERSION, "game": d.name, "loss": spec.get("loss", 0.0), "seed": spec.get("seed", 1),
            "reason": result.get("reason", ""), "players": players}


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("games", nargs="*", help="the recordings' directories under --root (default: every one)")
    ap.add_argument("--root", type=Path, default=DEFAULT_RECORDINGS)
    ap.add_argument("--out", type=Path, default=GOLDEN)
    a = ap.parse_args(argv)
    logging.basicConfig(level=logging.INFO, format="%(message)s")
    dirs = [a.root / g for g in a.games] if a.games else sorted(p for p in a.root.iterdir() if (p / "net.json").is_file())
    a.out.mkdir(parents=True, exist_ok=True)
    written, failed = [], 0
    for d in dirs:
        try:
            g = game_golden(d)
        except (OSError, ValueError, KeyError) as e:
            log.error("%s: %s", d.name, e)
            failed += 1
            continue
        (a.out / f"{d.name}.json").write_text(json.dumps(g, separators=(",", ":")) + "\n")
        written.append(d.name)
        log.info("%s: %s", d.name, ", ".join(f"{p['name']} {len(p['calls'])} records, {len(p['arrivals'])} arrivals, "
                                             f"{len(p['sends'])} sends, {count(p['calls'], 'event')} events"
                                             for p in g["players"]))
    index = a.out / "index.json"
    old = json.loads(index.read_text()) if index.is_file() else {}
    # an older version's goldens are not kept: the replay would refuse them
    games = set(written) | set(old.get("games", []) if old.get("version") == GOLDEN_VERSION else [])
    games = [g for g in games if (a.out / f"{g}.json").is_file()]
    index.write_text(json.dumps({"version": GOLDEN_VERSION, "games": sorted(games)}, indent=1) + "\n")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
