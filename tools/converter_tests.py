#!/usr/bin/env python3
"""The converter's tests for tools/ci.sh: each module in a process of its own, a module that passed on the same inputs
not run again.

    converter_tests.py [--jobs N] [--logs DIR] [--no-cache]

Each module's output goes to DIR/<module>.log; the failed ones' last lines are printed. The key of a pass covers what
the tests read: the repository's files except the native sources and most of the documents, the private evidence, the
converted data and the original installs (by name, size and time), the Python packages, FFmpeg, oggenc and StormLib. A
module's own imports are not traced: a change to any of it reruns them all, and a pass whose inputs changed while it
ran is not remembered. Temporary files go under DIR/tmp, removed even when this run is stopped: test_hd2 writes
gigabytes.
"""
from __future__ import annotations

import argparse
import json
import logging
import os
import platform
import re
import shutil
import signal
import subprocess
import sys
import time
from collections.abc import Callable
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))  # run as a script, only tools/ is on the path
from ci_cache import digest  # noqa: E402
from converter.mpq import stormlib  # noqa: E402

log = logging.getLogger("converter_tests")

PACKAGE = "converter.tests"
DEFAULT_JOBS = 4
DEFAULT_LOGS = Path("build/converter-tests")
STATE_FILE = "passed.json"
FAILED_LINES = 40
POLL_SECONDS = 0.1
# What the converter's tests do not read: a tracked path under one of these leaves the key alone, except in KEPT.
NOT_READ = ("native/", "tests/c/", "docs/", ".github/", "tools/ci.sh")
KEPT = ("docs/research/",)
# Their data outside the repository's tracked files, and where the original installs are named.
UNTRACKED_INPUTS = (Path("generated"), Path("docs/research/evidence"))
INSTALLS = ("KOPRULU_ORIGINAL", "KOPRULU_SCR")
EXTERNAL_TOOLS = ("ffmpeg", "ffprobe", "oggenc")
STOP_SIGNALS = (signal.SIGTERM, signal.SIGINT, signal.SIGHUP)


@dataclass
class Outcome:
    module: str
    status: int  # the process's; 0 passed
    tests: int = 0
    skipped: int = 0
    remembered: bool = False  # it passed on these inputs in an earlier run and did not run


def _packages() -> list[str]:
    """The installed Python packages by their metadata directories' paths, which name their versions. Not through
    importlib.metadata: a half-removed package's directory has no metadata to read, and raises there."""
    return sorted(str(info) for entry in sys.path for info in Path(entry or ".").glob("*.*-info"))


def _programs() -> list[Path]:
    """The external programs and the library the tests use or skip without; resolved: a package manager's links keep
    their names over an upgrade, the files they lead to carry the versions."""
    tools = [found for name in EXTERNAL_TOOLS if (found := shutil.which(name))]
    return [Path(p).resolve() for p in tools + stormlib.library_candidates()]


def inputs_key(root: Path = ROOT) -> str:
    listed = subprocess.run(["git", "ls-files", "-co", "--exclude-standard", "-z"], cwd=root, check=True,
                            capture_output=True).stdout.decode("utf-8", "surrogateescape").split("\0")
    files = [root / name for name in sorted(listed)
             if name and (name.startswith(KEPT) or not name.startswith(NOT_READ))]
    installs = [os.environ[name] for name in INSTALLS if os.environ.get(name)]
    return digest(content=files, meta=[root / p for p in UNTRACKED_INPUTS] + [Path(p) for p in installs] + _programs(),
                  text=[platform.platform(), sys.version, sys.prefix, *_packages(),
                        *[f"{name}={os.environ.get(name, '')}" for name in INSTALLS]])


def _modules(root: Path, package: str) -> list[str]:
    return sorted(p.stem for p in (root / package.replace(".", "/")).glob("test*.py"))


def _read_state(path: Path) -> dict:
    try:
        state = json.loads(path.read_text())
    except FileNotFoundError:
        return {}  # none yet: everything runs
    except (OSError, ValueError) as error:
        log.warning("%s is unreadable, every module runs: %s", path, error)
        return {}
    if not isinstance(state, dict):
        log.warning("%s holds no passes, every module runs", path)
        return {}
    return state


def _write_state(path: Path, state: dict) -> None:
    temporary = path.with_suffix(".tmp")
    temporary.write_text(json.dumps(state, indent=1, sort_keys=True))
    temporary.replace(path)


def _counts(log_text: str) -> tuple[int, int]:
    ran = re.findall(r"^Ran (\d+) tests? in ", log_text, re.M)
    skipped = re.findall(r"^OK \((?:.*, )?skipped=(\d+)", log_text, re.M)
    return int(ran[-1]) if ran else 0, int(skipped[-1]) if skipped else 0


def _kill_group(process: subprocess.Popen) -> None:
    """A module's whole process group goes, and the module is reaped."""
    try:
        os.killpg(process.pid, signal.SIGKILL)
    except (ProcessLookupError, PermissionError):
        pass  # it has ended; macOS answers EPERM for a group of zombies, as a module not polled yet is
    process.wait()


def run_modules(root: Path, package: str, logs: Path, inputs: Callable[[], str], jobs: int,
                use_cache: bool = True) -> list[Outcome]:
    """Runs the modules that have no pass under the key `inputs` gives; the state keeps each pass as it comes, when the
    key still holds at its end: the module may have read files changed since the run began."""
    logs.mkdir(parents=True, exist_ok=True)
    tmp = logs / "tmp"
    shutil.rmtree(tmp, ignore_errors=True)
    tmp.mkdir(exist_ok=True)
    state_path = logs / STATE_FILE
    state = _read_state(state_path)
    key = inputs()
    outcomes = []
    pending = []
    for module in _modules(root, package):
        passed = state.get(module)
        if use_cache and passed and passed.get("key") == key:
            outcomes.append(Outcome(module, 0, passed["tests"], passed["skipped"], True))
        else:
            pending.append(module)
    env = {**os.environ, "TMPDIR": str(tmp.resolve())}
    running: dict[str, subprocess.Popen] = {}
    try:
        while pending or running:
            while pending and len(running) < jobs:
                module = pending.pop(0)
                durations = ["--durations", "5"] if sys.version_info >= (3, 12) else []
                with (logs / f"{module}.log").open("w") as out:  # the process has its own copy
                    running[module] = subprocess.Popen(
                        [sys.executable, "-B", "-m", "unittest", "-q", *durations, f"{package}.{module}"],
                        cwd=root, env=env, stdout=out, stderr=subprocess.STDOUT, start_new_session=True)
            for module in [m for m, process in running.items() if process.poll() is not None]:
                process = running.pop(module)
                tests, skipped = _counts((logs / f"{module}.log").read_text(errors="replace"))
                outcomes.append(Outcome(module, process.returncode, tests, skipped))
                state.pop(module, None)
                if process.returncode == 0:
                    if inputs() == key:
                        state[module] = {"key": key, "tests": tests, "skipped": skipped}
                    else:
                        log.warning("%s: the inputs changed during the run, this pass is not remembered", module)
                _write_state(state_path, state)
            time.sleep(POLL_SECONDS)
    finally:
        for process in running.values():  # a stopped run: the modules go with it
            _kill_group(process)
        shutil.rmtree(tmp, ignore_errors=True)
    return sorted(outcomes, key=lambda o: o.module)


def report(outcomes: list[Outcome], logs: Path, seconds: float) -> int:
    """Logs the failures and the summary; the exit status of the run."""
    failed = [o for o in outcomes if o.status != 0]
    for o in failed:
        path = logs / f"{o.module}.log"
        tail = path.read_text(errors="replace").splitlines()[-FAILED_LINES:]
        log.error("%s failed with status %d (%s):\n%s", o.module, o.status, path, "\n".join(tail))
    passed = sum(o.tests for o in outcomes if o.status == 0)
    log.info("converter tests: %d passed (%d skipped) in %d modules, %d failed, %d remembered from an earlier pass, "
             "%.0f s", passed, sum(o.skipped for o in outcomes), len(outcomes), len(failed),
             sum(o.remembered for o in outcomes), seconds)
    if not outcomes:
        log.error("no test modules found")
    return 1 if failed or not outcomes else 0


def _stop(signum: int, _frame: object) -> None:
    # Once: a signal to the whole process group reaches this run again through tools/ci.sh's trap, and a second
    # SystemExit would cut run_modules's cleanup short.
    for caught in STOP_SIGNALS:
        signal.signal(caught, signal.SIG_IGN)
    raise SystemExit(128 + signum)


def stop_on_signals() -> None:
    """A signal ends the run through run_modules's cleanup: its modules are in sessions of their own, no signal to this
    process or its group reaches them."""
    for caught in STOP_SIGNALS:
        signal.signal(caught, _stop)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--jobs", type=int, default=DEFAULT_JOBS)
    parser.add_argument("--logs", type=Path, default=DEFAULT_LOGS)
    parser.add_argument("--no-cache", action="store_true", help="run every module, whatever passed before")
    args = parser.parse_args(argv)
    if args.jobs < 1:
        parser.error("--jobs takes 1 or more")
    logging.basicConfig(level=logging.INFO, format="%(message)s", stream=sys.stdout)
    stop_on_signals()
    logs = args.logs if args.logs.is_absolute() else ROOT / args.logs
    start = time.monotonic()
    outcomes = run_modules(ROOT, PACKAGE, logs, inputs_key, args.jobs, use_cache=not args.no_cache)
    return report(outcomes, logs, time.monotonic() - start)


if __name__ == "__main__":
    sys.exit(main())
