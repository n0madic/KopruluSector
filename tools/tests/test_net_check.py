"""tools/net_check.py: the network oracle's recordings as kp_snet's replay goldens (phase N, stage N2), on synthetic
recordings: the call tree, the calls' entries, which arrival each pass took, and a whole game written with its
index."""
from __future__ import annotations

import json
import logging
import struct
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import net_check  # noqa: E402

HOST, JOINER = "10.0.0.1", "10.0.0.2"


def pcap(payloads: list[bytes]) -> bytes:
    """A VirtualLan pcap: IPv4 and UDP headers (only the UDP length is read) before each payload."""
    out = struct.pack("<IHHiIII", net_check.PCAP_MAGIC, 2, 4, 0, 0, 65535, net_check.PCAP_LINKTYPE_IPV4)
    for p in payloads:
        packet = bytes(20) + struct.pack(">HHHH", 6112, 6112, 8 + len(p), 0) + p
        out += struct.pack("<IIII", 0, 0, len(packet), len(packet)) + packet
    return out


def call(seq: int, end: int, fn: str, result: int = 1, **extra) -> dict:
    return {"seq": seq, "end": end, "t_us": seq * 100, "fn": fn, "result": result, **extra}


def pump(seq: int, received: list[dict]) -> dict:
    return {"seq": seq, "t_us": seq * 100, "fn": "pump", "signalled": 1, "wait": 50, "received": received,
            "clock": [["0x15027a10", seq * 100 + 50]]}


class TreeTest(unittest.TestCase):
    def test_calls_nest_by_their_order(self):
        recs = [
            call(1, 1, "SNetInitializeProvider", program="00" * 20),
            call(2, 5, "SNetReceiveMessage", sender=1, data="4a"),
            {"seq": 3, "t_us": 300, "fn": "event", "id": 1, "player": 1, "data": "00"},
            call(4, 4, "SNetGetPlayerName", args=["1"], name="Beta"),
            pump(6, []),
            call(7, 9, "SNetEnumProviders"),  # not replayed: what runs inside it is its caller's
            call(8, 8, "SNetGetNumPlayers", num=[0, 1, 2]),
            call(10, 10, "SNetGetTurnsInTransit", in_transit=2),
        ]
        top, passes, program = net_check.tree("game/Alpha", recs)
        self.assertEqual(program, "00" * 20)
        self.assertEqual([c["fn"] for c in top], ["SNetInitializeProvider", "SNetReceiveMessage", "pump",
                                                  "SNetGetNumPlayers", "SNetGetTurnsInTransit"])
        self.assertEqual([c["fn"] for c in top[1]["inner"]], ["event", "SNetGetPlayerName"])
        self.assertEqual(top[1]["inner"][1]["args"], [1])
        self.assertNotIn("inner", top[0])  # an empty one is pruned
        self.assertEqual(len(passes), 1)
        self.assertIs(passes[0], top[2])

    def test_a_call_without_its_end_is_refused(self):
        with self.assertRaises(ValueError):
            net_check.tree("game/Alpha", [{"seq": 1, "t_us": 100, "fn": "SNetSendTurn", "result": 1}])

    def test_a_take_outside_the_passes_is_refused(self):
        with self.assertRaises(ValueError):
            net_check.tree("game/Alpha", [{"seq": 1, "t_us": 100, "fn": "taken"}])


class CallEntryTest(unittest.TestCase):
    def test_a_failed_receive_keeps_its_arguments_and_status(self):
        rec = call(1, 1, "SNetReceiveTurns", result=0, args=["0", "8"], status=["0x70000", "0x10000"],
                   players=["deadbeef"])
        entry = net_check.call_entry(rec)
        self.assertEqual(entry["args"], [0, 8])
        self.assertEqual(entry["status"], ["0x70000", "0x10000"])
        self.assertNotIn("players", entry)  # a failed call's outputs are not the original's

    def test_a_receive_without_its_arguments_is_refused(self):
        with self.assertRaises(ValueError):
            net_check.call_entry(call(1, 1, "SNetReceiveTurns", result=0))

    def test_sends_keep_what_they_were_given(self):
        entry = net_check.call_entry(call(1, 1, "SNetSendTurn", data="05", args=["1"]))
        self.assertEqual(entry["given"], "05")

    def test_a_successful_call_keeps_its_outputs(self):
        entry = net_check.call_entry(call(1, 1, "SNetGetPlayerName", args=["0"], name="Alpha"))
        self.assertEqual(entry["name"], "Alpha")


class TakenByTest(unittest.TestCase):
    def arrivals(self) -> list[dict]:
        return [{"t": 1, "socket": 1, "from": f"{JOINER}:6112", "data": "aa"},
                {"t": 2, "socket": 0, "from": f"{JOINER}:6111", "data": "bb"},
                {"t": 3, "socket": 1, "from": f"{JOINER}:6112", "data": "cc"},
                {"t": 4, "socket": 1, "from": f"{JOINER}:6112", "data": "dd"}]

    def test_passes_take_the_queue_in_order_or_nearly(self):
        arrivals = self.arrivals()
        passes = [{"t": 5, "received": [{"from": f"{JOINER}:6112", "data": "cc"},
                                        {"from": f"{JOINER}:6112", "data": "aa"}]}]
        with self.assertLogs("net_check", logging.INFO):
            net_check.taken_by("game/Alpha", passes, arrivals)
        self.assertEqual(passes[0]["take"], [2, 0])
        self.assertEqual([a.get("pass") for a in arrivals], [0, None, 0, -1])

    def test_a_datagram_that_never_arrived_is_refused(self):
        passes = [{"t": 5, "received": [{"from": f"{JOINER}:6112", "data": "ee"}]}]
        with self.assertRaises(ValueError):
            net_check.taken_by("game/Alpha", passes, self.arrivals())


CONNECT, ANSWER = bytes.fromhex("0000" + "0c00" + "00" * 8), bytes.fromhex("1111" + "0c00" + "01" * 8)


def write_join(game):
    """A recorded game: Beta's connect reaches Alpha, whose broadcast answers."""
    datagrams = [
        {"n": 0, "t": 1000, "len": len(CONNECT), "from": "Beta", "src": f"{JOINER}:6112", "dst": f"{HOST}:6112",
         "to": [{"host": "Alpha", "at": 3000}]},
        {"n": 1, "t": 4000, "len": len(ANSWER), "from": "Alpha", "src": f"{HOST}:6111",
         "dst": "255.255.255.255:6111", "to": [{"host": "Beta", "at": 6000}]},
    ]
    recs = [call(1, 1, "SNetInitializeProvider", program="ab" * 20),
            pump(40, [{"from": f"{JOINER}:6112", "data": CONNECT.hex()}])]
    (game / "Alpha").mkdir(parents=True)
    preload = [["C:\\Starcraft\\maps\\netsave.mlt", "saves/netsave.mlt"]]
    (game / "spec.json").write_text(json.dumps({"players": [{"name": "Alpha", "ip": HOST, "preload": preload}],
                                                "loss": 0.02, "seed": 7,
                                                "map_path": "maps/Test Map.scm", "expansion": False}))
    (game / "lan.jsonl").write_text("".join(json.dumps(d) + "\n" for d in datagrams))
    (game / "lan.pcap").write_bytes(pcap([CONNECT, ANSWER]))
    (game / "net.json").write_text(json.dumps({"reason": "every driver finished"}))
    (game / "Alpha" / "snet.jsonl").write_text("".join(json.dumps(r) + "\n" for r in recs))
    score = {"roster": [], "count": 2, "arrays": {}, "results": [2, 1, 0, 0, 0, 0, 0, 0], "local_result": 2}
    log = [{"f": 9, "tset": [1, 7, 20, 1], "t": 100, "glue": 3}, {"f": 9, "score": score, "t": 200, "glue": 17},
           {"f": 9, "music": [0, 15], "t": 200, "glue": 18}, {"f": 9, "elapsed": 17, "glue": 18}]
    (game / "Alpha" / "input.jsonl").write_text("".join(json.dumps(r) + "\n" for r in log))


class GameTest(unittest.TestCase):
    def test_a_game_becomes_a_golden_and_joins_the_index(self):
        with tempfile.TemporaryDirectory() as tmp:
            root, out = Path(tmp) / "net", Path(tmp) / "golden"
            write_join(root / "join")
            out.mkdir()
            (out / "index.json").write_text(json.dumps({"version": 4, "games": ["gone"]}))
            with self.assertLogs("net_check", logging.INFO):
                self.assertEqual(net_check.main(["--root", str(root), "--out", str(out), "join"]), 0)
            index = json.loads((out / "index.json").read_text())
            golden = json.loads((out / "join.json").read_text())
        self.assertEqual(index, {"version": net_check.GOLDEN_VERSION, "games": ["join"]})  # "gone" has no golden
        self.assertEqual((golden["version"], golden["loss"], golden["seed"]), (net_check.GOLDEN_VERSION, 0.02, 7))
        # net.py's map path is relative to tools/research, the golden's to the repository's root
        self.assertEqual((golden["map"], golden["expansion"]), ("tools/research/maps/Test Map.scm", False))
        alpha = golden["players"][0]
        self.assertEqual(alpha["program"], "ab" * 20)
        self.assertEqual((alpha["has_map"], alpha["actions"]), (True, []))  # a spec without them: the map, no inputs
        # a preloaded file: its guest path as given, its file relative to the repository's root
        self.assertEqual(alpha["preload"], [{"guest": "C:\\Starcraft\\maps\\netsave.mlt",
                                             "file": "tools/research/saves/netsave.mlt"}])
        self.assertEqual(alpha["sends"], [{"t": 4000, "socket": 0, "to": "255.255.255.255:6111",
                                           "data": ANSWER.hex()}])
        # the connect at its delivery, the host's own broadcast at once
        self.assertEqual([(a["t"], a["socket"]) for a in alpha["arrivals"]], [(3000, 1), (4000, 0)])
        self.assertEqual(alpha["arrivals"][0]["pass"], 0)
        self.assertEqual(alpha["calls"][1]["take"], [0])
        # the score screen after the game: its data, its seconds and the score state that followed (not the record's)
        self.assertEqual((alpha["score"]["results"], alpha["score"]["local_result"]), ([2, 1, 0, 0, 0, 0, 0, 0], 2))
        self.assertEqual((alpha["score"]["elapsed"], alpha["score"]["glue_state"]), (17, 18))

    def test_a_player_without_a_score_screen_has_none(self):
        with tempfile.TemporaryDirectory() as tmp:
            log = Path(tmp) / "input.jsonl"
            self.assertIsNone(net_check.score_of(log))  # no input log
            log.write_text(json.dumps({"f": 1, "tset": [1, 7, 20, 1], "t": 100, "glue": 3}) + "\n")
            self.assertIsNone(net_check.score_of(log))  # a game that ended without one

    def test_the_index_keeps_only_its_own_versions_goldens(self):
        with tempfile.TemporaryDirectory() as tmp:
            root, out = Path(tmp) / "net", Path(tmp) / "golden"
            write_join(root / "join")
            out.mkdir()
            for name in ("kept", "old"):
                (out / f"{name}.json").write_text("{}")
            for version, games, expected in ((net_check.GOLDEN_VERSION, ["kept", "gone"], ["join", "kept"]),
                                             (net_check.GOLDEN_VERSION - 1, ["kept", "old"], ["join"])):
                (out / "index.json").write_text(json.dumps({"version": version, "games": games}))
                with self.assertLogs("net_check", logging.INFO):
                    self.assertEqual(net_check.main(["--root", str(root), "--out", str(out), "join"]), 0)
                with self.subTest(version=version):
                    self.assertEqual(json.loads((out / "index.json").read_text())["games"], expected)

    def test_a_cut_pcap_fails_its_game_alone(self):
        whole = pcap([b"\x00" * 12])
        with tempfile.TemporaryDirectory() as tmp:
            for size in (0, 30, len(whole) - 3):  # no header, a cut record header, a cut packet
                path = Path(tmp) / f"cut{size}.pcap"
                path.write_bytes(whole[:size])
                with self.assertRaises(ValueError):
                    net_check.read_pcap(path)

    def test_a_recording_without_the_passes_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            game = Path(tmp) / "net" / "old"
            (game / "Alpha").mkdir(parents=True)
            (game / "spec.json").write_text(json.dumps({"players": [{"name": "Alpha", "ip": HOST}]}))
            (game / "lan.jsonl").write_text("")
            (game / "lan.pcap").write_bytes(pcap([]))
            (game / "net.json").write_text("{}")
            (game / "Alpha" / "snet.jsonl").write_text(
                json.dumps(call(1, 1, "SNetInitializeProvider", program="00")) + "\n")
            with self.assertLogs("net_check", logging.ERROR):
                self.assertEqual(net_check.main(["--root", str(Path(tmp) / "net"), "--out", str(Path(tmp) / "g")]), 1)


if __name__ == "__main__":
    unittest.main()
