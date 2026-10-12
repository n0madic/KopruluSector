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
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
M0E = ROOT / "tests" / "golden" / "oracle" / "m0e"
RESEARCH = ROOT / "docs" / "research"
STAGES = ["E0", "E1", "E2", "E3", "E4"]
HEX64 = re.compile(r"^[0-9a-f]{64}$")


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


@pytest.fixture(scope="module")
def manifest() -> dict:
    return json.loads((M0E / "manifest.json").read_text())


@pytest.fixture(scope="module")
def cases(manifest: dict) -> list[dict]:
    return [c for stage in manifest["stages"] for c in stage["cases"]]


def atlas_entries(package: str) -> dict[int, str]:
    text = (RESEARCH / "atlas" / f"{package.lower()}.md").read_text()
    found = {int(a, 16): n for a, n in re.findall(r"^### 0x([0-9a-f]{8}) `([^`]+)`", text, re.M)}
    found.update({int(a, 16): n for a, n in re.findall(r"^\| 0x([0-9a-f]{6,8}) \| `([^`]+)` \|", text, re.M)})
    return found


def test_profile_pinned(manifest: dict) -> None:
    profile_path = ROOT / manifest["profile"]["file"]
    profile = json.loads(profile_path.read_text())
    assert manifest["profile"]["id"] == profile["id"] == "bw-1.17.0.1"
    assert manifest["profile"]["sha256"] == sha256(profile_path)
    assert manifest["profile"]["executable_sha256"] == profile["executable"]["sha256"]


def test_stages_and_case_structure(manifest: dict, cases: list[dict]) -> None:
    assert [s["id"] for s in manifest["stages"]] == STAGES
    ids = [c["id"] for c in cases]
    assert len(ids) == len(set(ids))
    for stage in manifest["stages"]:
        assert stage["cases"], stage["id"]
        assert stage["plan_row"]
        for c in stage["cases"]:
            assert c["id"].startswith(stage["id"] + ".")
            assert c["status"] in manifest["statuses"], c["id"]
            assert c["inputs"]["kind"] in manifest["input_kinds"], c["id"]
            for key in ("title", "scope", "research", "ledger", "atlas", "missing", "observed", "evidence"):
                assert key in c, (c["id"], key)
            assert c["scope"] and c["ledger"] and c["atlas"], c["id"]
            if c["status"] != "implementation-continuity" and c["status"] != "static":
                assert c["evidence"] or c.get("emulator_source"), c["id"]
            if c["status"] == "emulator-observed":
                assert HEX64.match(c["emulator_source"]["sha256"]), c["id"]


def test_ledger_rows_exist(cases: list[dict]) -> None:
    rows = set(re.findall(r"^\| (R\d\d) \|", (RESEARCH / "completion-ledger.md").read_text(), re.M))
    assert len(rows) == 37
    for c in cases:
        for r in c["ledger"]:
            assert r in rows, (c["id"], r)


def test_atlas_entries_exist(cases: list[dict]) -> None:
    cache: dict[str, dict[int, str]] = {}
    for c in cases:
        for e in c["atlas"]:
            entries = cache.setdefault(e["package"], atlas_entries(e["package"]))
            address = int(e["address"], 16)
            assert address in entries, (c["id"], e)
            assert entries[address] == e["name"], (c["id"], e)


def test_research_documents_exist(cases: list[dict]) -> None:
    for c in cases:
        for doc in c["research"]:
            assert (ROOT / doc).is_file(), (c["id"], doc)


def test_golden_files_match(cases: list[dict]) -> None:
    listed = set()
    for c in cases:
        assert set(c.get("golden_sha256", {})) == set(c.get("golden", [])), c["id"]
        for path, digest in c.get("golden_sha256", {}).items():
            assert sha256(ROOT / path) == digest, (c["id"], path)
            listed.add(Path(path).name)
    index = (M0E / "index.lua").read_text()
    lua_files = {p.name for p in M0E.glob("*.lua")} - {"index.lua"}
    assert {n for n in listed if n in lua_files} == lua_files
    for name in lua_files:
        assert f'"{name}"' in index, name


def test_e3_tuples_are_labelled(cases: list[dict]) -> None:
    total = 0
    for c in cases:
        for sub in c["observed"].get("subcases", []):
            assert len(sub["actual_fields"]) == len(sub["actual"]), sub["case"]
            assert re.match(r"^tools/research/probe_\w+\.py:\d+$", sub["actual_source"]), sub["case"]
            total += 1
    assert total == 62


def test_evidence_references(manifest: dict, cases: list[dict]) -> None:
    for c in cases:
        for ref in c["evidence"]:
            assert ref["file"] in manifest["evidence"], (c["id"], ref["file"])
            assert ref["sha256"] == manifest["evidence"][ref["file"]]["sha256"]
    for path, info in manifest["evidence"].items():
        assert HEX64.match(info["sha256"]) and info["target_sha256"] == manifest["profile"]["executable_sha256"]
        if "probe" in info:
            assert sha256(ROOT / info["probe"]) == info["probe_sha256"], path


@pytest.mark.skipif(not (RESEARCH / "evidence").is_dir(), reason="evidence is a private input")
def test_evidence_hashes(manifest: dict) -> None:
    for path, info in manifest["evidence"].items():
        assert sha256(ROOT / path) == info["sha256"], path
