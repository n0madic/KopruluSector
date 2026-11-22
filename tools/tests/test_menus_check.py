"""tools/menus_check.py: a network player's menus run as kp_oracle menus_replay's lines (phase N, stage N4), on a
synthetic input log: the games' records left out up to their score screens, each score screen's line with its own
data, the player's Storm ID and the local result ComputeGameResults gave."""
from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import menus_check  # noqa: E402
import room_check  # noqa: E402

MAIN, SCORE, CONSOLE = 1, 2, 5  # the owners: gluMain.bin, gluScore.bin and a game console's timer owner


def score(local_result: int, count: int) -> dict:
    roster = [{"name": "Alpha", "force": "", "index": 0, "storm": 0, "type": 2, "race": 1},
              {"name": "Beta", "force": "", "index": 1, "storm": 1, "type": 2, "race": 1}]
    return {"roster": roster, "count": count, "arrays": {}, "colours": [], "player_colours": [],
            "results": [1, local_result, 0, 0, 0, 0, 0, 0], "local_result": local_result}


def game(start: int, end: int, data: dict, state: int) -> list[dict]:
    """A network game from tick start (its console's first timer), then its score screen in glue state `state`."""
    return [{"tset": [CONSOLE, 1, 42, 0], "t": start}, {"ev": [4, 1, 1], "t": start + 500},
            {"score": data, "t": end, "glue": state}, {"tset": [SCORE, 1, 100, 0], "t": end, "glue": state},
            {"elapsed": 17, "glue": state}, {"ev": [5, 2, 2], "t": end + 500, "glue": state},
            {"tset": [MAIN, 1, 100, 0], "t": end + 1000, "glue": 3}]


def write_log(path: Path) -> None:
    log = [{"owner": MAIN, "ref": ["rez\\gluMain.bin", 0]}, {"owner": SCORE, "ref": ["rez\\gluScore.bin", 0]},
           {"owner": CONSOLE, "ref": ["rez\\gameconsole.bin", 0]},
           {"tset": [MAIN, 1, 100, 0], "t": 1000, "glue": 0}, {"ev": [4, 10, 10], "t": 1100, "glue": 3}]
    log += game(2000, 3000, score(3, 2), 16)  # a draw: the defeat's screen (Terran, state 16)
    log += game(5000, 6000, score(1, 3), 17)  # a victory (state 17)
    path.write_text("".join(json.dumps(r) + "\n" for r in log))


def score_lines(lines: list[str]) -> list[dict]:
    return [json.loads(line[len("score "):]) for line in lines if line.startswith("score ")]


class MenusLinesTest(unittest.TestCase):
    def test_a_network_players_games_are_left_out_to_their_score_screens(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "input.jsonl"
            write_log(path)
            lines, records, gaps = menus_check.menus_lines(path, "Beta")
        self.assertEqual(gaps, [(2000, 3000), (5000, 6000)])
        self.assertEqual(lines[0], "start 1000")
        self.assertFalse([r for r in records if r.get("t") in (2000, 2500, 5000, 5500)])  # the games' records
        first, second = score_lines(lines)
        # the local result as ComputeGameResults gave it (a draw), not the glue state's defeat; Beta's Storm ID
        self.assertEqual((first["result"], first["race"], first["local_storm"], first["network"]), (3, 1, 1, 1))
        self.assertEqual((first["count"], first["glue_state"], first["elapsed"], first["t"]), (2, 16, 17, 3000))
        # the second game's screen has its own data
        self.assertEqual((second["result"], second["count"], second["glue_state"], second["t"]), (1, 3, 17, 6000))

    def test_a_player_missing_from_the_roster_takes_the_first_human(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "input.jsonl"
            write_log(path)
            with self.assertLogs("menus_check", "WARNING"):
                (line,) = menus_check.score_line(path, 3000, "Gamma")
        self.assertEqual(json.loads(line[len("score "):])["local_storm"], 0)

    def test_an_offline_run_keeps_its_records(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "input.jsonl"
            write_log(path)
            lines, records, gaps = menus_check.menus_lines(path)
        self.assertEqual(gaps, [])
        self.assertEqual(score_lines(lines), [])
        self.assertTrue([r for r in records if r.get("t") == 2500])


class ScoreScreenDataTest(unittest.TestCase):
    def test_the_score_state_gives_the_result_and_the_race(self):
        d = dict(score(2, 2), glue_state=19)
        self.assertEqual(room_check.score_screen_data(d)["result"], 1)  # odd: a victory
        self.assertEqual(room_check.score_screen_data(d)["race"], 2)
        self.assertEqual(room_check.score_screen_data(d)["local_storm"], 0)  # offline: the first human's
        self.assertEqual(room_check.score_screen_data(d, 1)["local_storm"], 1)


if __name__ == "__main__":
    unittest.main()
