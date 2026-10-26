"""Shared helpers of the golden-oracle exporters (tools/oracle/export_*.py).

Paths, the 1.17.0.1 profile identity, hashing of evidence files, and rendering of plain Python data as the goldens'
canonical JSON (golden_json), which the C suites (tests/c) and the Python checkers read: write_golden writes a golden
and golden_stale checks it.
"""
from __future__ import annotations

import hashlib
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
EVIDENCE = ROOT / "docs" / "research" / "evidence"
GOLDEN = ROOT / "tests" / "golden" / "oracle"
PROFILE = ROOT / "defs" / "profile" / "bw-1.17.0.1.json"
TARGET_SHA256 = "94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0"

class ExportError(SystemExit):
    """Raised when evidence does not satisfy the expectations of an exporter."""


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_file(path: Path) -> str:
    return sha256_bytes(path.read_bytes())


def canonical_sha256(value: object) -> str:
    """SHA-256 of the canonical JSON form (sorted keys, no whitespace)."""
    return sha256_bytes(json.dumps(value, sort_keys=True, separators=(",", ":")).encode())


def load_evidence(name: str) -> tuple[dict, str]:
    """Load an evidence JSON; reject it when it names another executable than the profile's."""
    path = EVIDENCE / name
    raw = path.read_bytes()
    data = json.loads(raw)
    target = data.get("target_sha256")
    if target is not None and target != TARGET_SHA256:
        raise ExportError(f"{name}: target {target} is not the 1.17.0.1 profile executable")
    return data, sha256_bytes(raw)


def cases_module(header: dict, cases: list) -> dict:
    """A golden of ``header`` fields plus a ``cases`` array (golden_json writes a case per line)."""
    return {**header, "cases": cases}


_NUMERIC_KEY = re.compile(r"^-?[0-9]+$")


def _key_order(key: str) -> tuple:
    """Numeric keys by value first, then the others by name."""
    return (0, int(key), "") if _NUMERIC_KEY.match(key) else (1, 0, key)


def _members(value: dict) -> list[tuple[str, object]]:
    items = [(str(k), v) for k, v in value.items() if v is not None]
    return sorted(items, key=lambda kv: _key_order(kv[0]))


def _compact(value: object) -> str:
    if value is None:
        return "null"
    if isinstance(value, bool):
        return "true" if value else "false"
    if isinstance(value, int):
        return str(value)
    if isinstance(value, float):
        return json.dumps(value)
    if isinstance(value, str):
        return json.dumps(value)
    if isinstance(value, (list, tuple)):
        return "[" + ",".join(_compact(v) for v in value) + "]"
    if isinstance(value, dict):
        return "{" + ",".join(json.dumps(k) + ":" + _compact(v) for k, v in _members(value)) + "}"
    raise TypeError(f"unsupported value {value!r}")


def _container(value: object) -> bool:
    return isinstance(value, (list, tuple, dict))


def _member(value: object) -> str:
    """A top-level member: a non-empty list or object of containers one element per line, anything else compact."""
    if isinstance(value, (list, tuple)) and value and all(_container(v) for v in value):
        return "[\n" + ",\n".join(_compact(v) for v in value) + "\n]"
    if isinstance(value, dict) and _members(value) and all(_container(v) for _, v in _members(value)):
        return "{\n" + ",\n".join(json.dumps(k) + ": " + _compact(v) for k, v in _members(value)) + "\n}"
    return _compact(value)


def golden_json(value: object) -> str:
    """A golden's canonical JSON (the C suites' and the Python checkers' form): an object's members in key order
    (numeric keys by value, then the others by name), None members left out; the top level one member (a list: one
    element) per line, and a member whose value is a list or object of containers one element per line, everything
    else compact. Strings are ASCII: other characters (a string's bytes) are \\u00XX escapes. Integer keys become
    their decimal text (a run's sparse ``steps[n]`` become ``"n"``)."""
    if isinstance(value, dict):
        members = _members(value)
        if not members:
            return "{}\n"
        return "{\n" + ",\n".join(json.dumps(k) + ": " + _member(v) for k, v in members) + "\n}\n"
    if isinstance(value, (list, tuple)):
        if not value:
            return "[]\n"
        return "[\n" + ",\n".join(_compact(v) for v in value) + "\n]\n"
    return _compact(value) + "\n"


def golden_stale(path: Path, data: object) -> bool:
    """Whether the golden at ``path`` is missing or differs from ``data``'s canonical JSON."""
    return not path.exists() or path.read_text() != golden_json(data)


def write_golden(path: Path, data: object) -> None:
    """Writes a golden: ``data``'s canonical JSON."""
    path.write_text(golden_json(data))
