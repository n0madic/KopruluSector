"""Validate the M0E golden manifest (tests/golden/oracle/m0e/manifest.json).

Structure, statuses and input kinds; every linked ledger row exists in the completion ledger;
every atlas link names an existing function entry of the stated package; golden data files exist
with the recorded hashes. Evidence hashes are checked only where the (gitignored) evidence
directory is present. Parsing here is independent of tools/oracle/export_m0e_cases.py.
"""
from __future__ import annotations

import hashlib
import json
import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
PRIVATE = "the golden M0E manifest or the research atlas: private inputs, not in the repository"
M0E = ROOT / "tests" / "golden" / "oracle" / "m0e"
RESEARCH = ROOT / "docs" / "research"
STAGES = ["E0", "E1", "E2", "E3", "E4"]
HEX64 = re.compile(r"^[0-9a-f]{64}$")


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def atlas_entries(package: str) -> dict[int, str]:
    text = (RESEARCH / "atlas" / f"{package.lower()}.md").read_text()
    found = {int(a, 16): n for a, n in re.findall(r"^### 0x([0-9a-f]{8}) `([^`]+)`", text, re.M)}
    found.update({int(a, 16): n for a, n in re.findall(r"^\| 0x([0-9a-f]{6,8}) \| `([^`]+)` \|", text, re.M)})
    return found


@unittest.skipUnless(M0E.exists() and (RESEARCH / "atlas").exists(), PRIVATE)
class GoldenM0eTest(unittest.TestCase):
    manifest: dict
    cases: list[dict]

    @classmethod
    def setUpClass(cls) -> None:
        cls.manifest = json.loads((M0E / "manifest.json").read_text())
        cls.cases = [c for stage in cls.manifest["stages"] for c in stage["cases"]]

    def test_profile_pinned(self) -> None:
        profile_path = ROOT / self.manifest["profile"]["file"]
        profile = json.loads(profile_path.read_text())
        assert self.manifest["profile"]["id"] == profile["id"] == "bw-1.17.0.1"
        assert self.manifest["profile"]["sha256"] == sha256(profile_path)
        assert self.manifest["profile"]["executable_sha256"] == profile["executable"]["sha256"]

    def test_stages_and_case_structure(self) -> None:
        assert [s["id"] for s in self.manifest["stages"]] == STAGES
        ids = [c["id"] for c in self.cases]
        assert len(ids) == len(set(ids))
        for stage in self.manifest["stages"]:
            assert stage["cases"], stage["id"]
            assert stage["plan_row"]
            for c in stage["cases"]:
                assert c["id"].startswith(stage["id"] + ".")
                assert c["status"] in self.manifest["statuses"], c["id"]
                assert c["inputs"]["kind"] in self.manifest["input_kinds"], c["id"]
                for key in ("title", "scope", "research", "ledger", "atlas", "missing", "observed", "evidence"):
                    assert key in c, (c["id"], key)
                assert c["scope"] and c["ledger"] and c["atlas"], c["id"]
                if c["status"] != "implementation-continuity" and c["status"] != "static":
                    assert c["evidence"] or c.get("emulator_source"), c["id"]
                if c["status"] == "emulator-observed":
                    assert HEX64.match(c["emulator_source"]["sha256"]), c["id"]

    def test_ledger_rows_exist(self) -> None:
        rows = set(re.findall(r"^\| (R\d\d) \|", (RESEARCH / "completion-ledger.md").read_text(), re.M))
        assert len(rows) == 37
        for c in self.cases:
            for r in c["ledger"]:
                assert r in rows, (c["id"], r)

    def test_atlas_entries_exist(self) -> None:
        cache: dict[str, dict[int, str]] = {}
        for c in self.cases:
            for e in c["atlas"]:
                entries = cache.setdefault(e["package"], atlas_entries(e["package"]))
                address = int(e["address"], 16)
                assert address in entries, (c["id"], e)
                assert entries[address] == e["name"], (c["id"], e)

    def test_research_documents_exist(self) -> None:
        for c in self.cases:
            for doc in c["research"]:
                assert (ROOT / doc).is_file(), (c["id"], doc)

    def test_golden_files_match(self) -> None:
        listed = set()
        for c in self.cases:
            assert set(c.get("golden_sha256", {})) == set(c.get("golden", [])), c["id"]
            for path, digest in c.get("golden_sha256", {}).items():
                assert sha256(ROOT / path) == digest, (c["id"], path)
                listed.add(Path(path).name)
        index = json.loads((M0E / "index.json").read_text())["files"]
        data_files = {p.name for p in M0E.glob("*.json")} - {"index.json", "manifest.json"}
        assert {n for n in listed if n in data_files} == data_files
        assert set(index) == data_files

    def test_e3_tuples_are_labelled(self) -> None:
        total = 0
        for c in self.cases:
            for sub in c["observed"].get("subcases", []):
                assert len(sub["actual_fields"]) == len(sub["actual"]), sub["case"]
                assert re.match(r"^tools/research/probe_\w+\.py:\d+$", sub["actual_source"]), sub["case"]
                total += 1
        assert total == 62

    def test_evidence_references(self) -> None:
        for c in self.cases:
            for ref in c["evidence"]:
                assert ref["file"] in self.manifest["evidence"], (c["id"], ref["file"])
                assert ref["sha256"] == self.manifest["evidence"][ref["file"]]["sha256"]
        for path, info in self.manifest["evidence"].items():
            assert HEX64.match(info["sha256"]) and info["target_sha256"] == self.manifest["profile"]["executable_sha256"]
            if "probe" in info:
                assert sha256(ROOT / info["probe"]) == info["probe_sha256"], path

    @unittest.skipIf(not (RESEARCH / "evidence").is_dir(), "evidence is a private input")
    def test_evidence_hashes(self) -> None:
        for path, info in self.manifest["evidence"].items():
            assert sha256(ROOT / path) == info["sha256"], path


def m0e_file(name: str) -> dict:
    """A golden file of index.json."""
    return json.loads((M0E / name).read_text())


def m0e_files(prefix: str) -> list[tuple[str, dict]]:
    index = json.loads((M0E / "index.json").read_text())["files"]
    return [(name, m0e_file(name)) for name in index if name.startswith(prefix)]


@unittest.skipUnless(M0E.exists(), PRIVATE)
class GoldenM0eDataTest(unittest.TestCase):
    """The recorded original observations of the M0E goldens (tools/oracle/export_m0e_cases.py) are internally
    consistent: conservation, counts, field labels. They guard the exported data, not an implementation."""

    def test_all_files_load(self) -> None:
        files = json.loads((M0E / "index.json").read_text())["files"]
        assert len(files) >= 10, "index lists the golden files"
        for name in files:
            assert isinstance(m0e_file(name), dict), name

    def test_e1_conservation(self) -> None:
        """E1: initial - current resource amounts = cumulative deposits + carried cargo, after every callback; every
        genuine worker delivers at least twice."""
        scenes = m0e_files("e1_")
        assert len(scenes) == 4
        for name, g in scenes:
            amounts = list(g["baseline_amounts"])
            base = total = sum(amounts)
            ai = si = cumulative = 0
            run_pos = [0] * len(g["workers"])
            for w in range(len(g["workers"])):
                assert len(g["positions"][w]) == 2 * g["callbacks"], (name, "positions")
            for cb in range(1, g["callbacks"] + 1):
                while ai < len(g["amounts"]) and g["amounts"][ai][0] == cb:
                    _, k, value = g["amounts"][ai]
                    total += value - amounts[k - 1]
                    amounts[k - 1] = value
                    ai += 1
                while si < len(g["stock"]) and g["stock"][si][0] == cb:
                    cumulative = g["stock"][si][2]
                    si += 1
                carried = 0
                for w, runs in enumerate(g["runs"]):
                    while run_pos[w] + 1 < len(runs) and runs[run_pos[w] + 1][0] <= cb:
                        run_pos[w] += 1
                    carried += runs[run_pos[w]][4]
                assert base - total == cumulative + carried, (name, "conservation at callback", cb)
            assert ai == len(g["amounts"]), (name, "amount changes consumed")
            per: dict[int, int] = {}
            for d in g["deposits"]:
                per[d[1]] = per.get(d[1], 0) + 1
            for w in g["workers"]:
                assert per.get(w, 0) >= 2, (name, "worker", w, "deposits twice")

    def test_e2_final_state(self) -> None:
        """E2: the final state agrees with the recorded actual tuple and completion callbacks."""
        profiles = 0
        for name, g in m0e_files("e2_"):
            if "profile" not in g:
                continue
            profiles += 1
            fin, actual = g["final"], g["actual"]
            assert fin["stocks"][0] == actual[1][0], (name, "minerals")
            assert fin["stocks"][1] == actual[1][1], (name, "gas")
            assert fin["town"][0] == actual[2], (name, "Town workers")
            completed = {t: n for t, n in fin["completed"]}
            for ty, n in actual[0].items():
                assert completed.get(int(ty)) == n, (name, "completed", ty)
            stamps = [e["c"] for e in g["timeline"]]
            assert all(b > a for a, b in zip([0] + stamps, stamps)), (name, "timeline order")
            for c in g["completion_callbacks"]:
                assert c[1] <= g["callbacks"], (name, "completion in window")
        assert profiles == 3

    def test_e3_labels(self) -> None:
        """E3: every recorded tuple carries one label per value."""
        n = 0
        for name, g in m0e_files("e3_"):
            if "family" not in g:
                continue
            for c in g["cases"]:
                assert len(c["fields"]) == len(c["actual"]), (name, c["case"])
                n += 1
        assert n == 62

    def test_e3_extractor_completion(self) -> None:
        """E3 emulator: completion stamping returned, and each later delivery adds 8 gas taken from the geyser."""
        g = m0e_file("e3_extractor_completion_emulator.json")
        assert g["completed_state"][0] == 149
        assert g["completed_state"][3] == 750 * 256
        assert len(g["creep_stamp_return_frames"]) == 1
        assert g["creep_stamp_return_frames"][0] == g["creep_stamp_call_frames"][0]
        assert len(g["deliveries"]) >= 1, "gas delivered"
        gas = 0
        for d in g["deliveries"]:
            assert d[1] == gas + 8
            assert d[2] == g["geyser_before"][5] - d[1]
            gas = d[1]

    def test_e0_clock(self) -> None:
        """E0 emulator: the checkpoint RNG states are the per-frame states at those frames (rng_by_frame from frame 1)."""
        g = m0e_file("e0_clock_emulator.json")
        assert g["elapsed_period_frames"] == 16
        for c in g["checkpoints"]:
            assert g["rng_by_frame"][c[0] - 1] == c[1], ("rng at frame", c[0])
