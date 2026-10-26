"""The command line of the oracle tools for the Python checkers: kp_oracle's subcommands
(native/kp_core/tools/kp_oracle_main.c; build/cmake/kp_oracle, or the KP_ORACLE environment variable). They read the
goldens' JSON (tests/golden/oracle/**/*.json).
"""
from __future__ import annotations

import os
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
KP_ORACLE = ROOT / "build" / "cmake" / "kp_oracle"


def command(name: str) -> list[str]:
    """The command of an oracle tool: [kp_oracle, name]."""
    return [os.environ.get("KP_ORACLE", str(KP_ORACLE)), name]
