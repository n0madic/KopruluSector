"""tools/ci_cache.py and tools/python_tests.py, which decide what tools/ci.sh need not run again."""
from __future__ import annotations

import logging
import os
import signal
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import ci_cache  # noqa: E402
import python_tests  # noqa: E402

PASSING = "import unittest\nclass T(unittest.TestCase):\n    def test_a(self): pass\n    def test_b(self): pass\n"
SKIPPING = ("import unittest\nclass T(unittest.TestCase):\n    def test_a(self): pass\n"
            "    @unittest.skip('no data')\n    def test_b(self): pass\n")
FAILING = "import unittest\nclass T(unittest.TestCase):\n    def test_a(self): self.fail('wrong')\n"
BROKEN = "import no_such_module_at_all\n"


def _write(root: Path, **modules: str) -> None:
    package = root / "pkg"
    package.mkdir(exist_ok=True)
    (package / "__init__.py").write_text("")
    for name, text in modules.items():
        (package / f"{name}.py").write_text(text)


class CacheKeyTest(unittest.TestCase):
    def test_key_follows_each_kind_of_input(self):
        with tempfile.TemporaryDirectory() as tmp:
            a = Path(tmp) / "dir" / "a"
            a.parent.mkdir()
            a.write_bytes(b"one")
            base = ci_cache.digest([a.parent], [], ["v1"])
            assert ci_cache.digest([a.parent], [], ["v1"]) == base, "the same inputs, the same key"
            assert ci_cache.digest([a.parent], [], ["v2"]) != base, "a version"
            assert ci_cache.digest([], [a.parent], ["v1"]) != base, "a kind of input is not another"
            assert ci_cache.digest([], [], ["content", str(a.parent), "True"]) != base
            a.write_bytes(b"two")
            assert ci_cache.digest([a.parent], [], ["v1"]) != base, "a file's bytes"
            a.write_bytes(b"one")
            assert ci_cache.digest([a.parent], [], ["v1"]) == base, "by content, not by time"
            (a.parent / "new").write_bytes(b"")
            assert ci_cache.digest([a.parent], [], ["v1"]) != base, "a new empty file"

    def test_meta_follows_size_and_time_without_reading(self):
        with tempfile.TemporaryDirectory() as tmp:
            a = Path(tmp) / "a"
            a.write_bytes(b"one")
            os.utime(a, ns=(1, 1))
            base = ci_cache.digest([], [a])
            a.write_bytes(b"two")
            os.utime(a, ns=(1, 1))
            assert ci_cache.digest([], [a]) == base, "the same size and time: not read"
            os.utime(a, ns=(2, 2))
            assert ci_cache.digest([], [a]) != base, "a time"
            os.utime(a, ns=(1, 1))
            a.write_bytes(b"three")
            os.utime(a, ns=(1, 1))
            assert ci_cache.digest([], [a]) != base, "a size"

    def test_a_missing_path_is_an_input(self):
        with tempfile.TemporaryDirectory() as tmp:
            a = Path(tmp) / "a"
            absent = ci_cache.digest([a])
            a.write_bytes(b"")
            assert ci_cache.digest([a]) != absent
            a.unlink()
            assert ci_cache.digest([], [a]) != ci_cache.digest([a])

    def test_the_reports_are_no_input(self):
        with tempfile.TemporaryDirectory() as tmp:
            data = Path(tmp) / "generated"
            (data / "reports").mkdir(parents=True)
            (data / "a").write_bytes(b"one")
            skip = (data / "reports",)
            base = ci_cache.digest([], [data], skip=skip)
            (data / "reports" / "widths-ruRU.json").write_text("{}")
            assert ci_cache.digest([], [data], skip=skip) == base, "a report written by a check"
            assert ci_cache.digest([], [data], skip=()) != base, "a folder left out only when asked"
            (data / "b").write_bytes(b"")
            assert ci_cache.digest([], [data], skip=skip) != base, "the data beside them"
        assert ROOT / "generated" / "reports" in ci_cache.NOT_INPUTS, "the checks' reports, by default"

    def test_a_dangling_link_is_an_input_too(self):
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp) / "dir"
            folder.mkdir()
            empty = ci_cache.digest([folder])
            (folder / "link").symlink_to("absent")
            assert ci_cache.digest([folder]) != empty, "its bytes cannot be read"
            assert ci_cache.digest([], [folder]) != ci_cache.digest([], [])


class PythonTestsTest(unittest.TestCase):
    def setUp(self):
        logging.disable(logging.CRITICAL)
        self.addCleanup(logging.disable, logging.NOTSET)
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.root = Path(tmp.name)
        self.logs = self.root / "logs"

    def _run(self, key: str = "k", use_cache: bool = True) -> dict[str, python_tests.Outcome]:
        outcomes = python_tests.run_modules(self.root, "pkg", self.logs, lambda: key, 2, use_cache=use_cache)
        return {o.module: o for o in outcomes}

    def test_counts_and_status(self):
        _write(self.root, test_a=PASSING, test_b=SKIPPING, test_c=FAILING, test_d=BROKEN, helper="raise SystemExit(3)\n")
        got = self._run()
        assert sorted(got) == ["test_a", "test_b", "test_c", "test_d"], "only the test modules run"
        assert (got["test_a"].status, got["test_a"].tests, got["test_a"].skipped) == (0, 2, 0)
        assert (got["test_b"].status, got["test_b"].tests, got["test_b"].skipped) == (0, 2, 1)
        assert got["test_c"].status != 0 and got["test_d"].status != 0
        assert "wrong" in (self.logs / "test_c.log").read_text()
        logging.disable(logging.NOTSET)
        with self.assertLogs("python_tests", logging.INFO) as logs:
            assert python_tests.report(list(got.values()), 0) == 1
        assert "python tests: 4 passed (1 skipped) in 4 modules, 2 failed" in logs.output[-1], "no failed test counted"

    def test_only_a_pass_on_the_same_key_is_remembered(self):
        _write(self.root, test_a=PASSING, test_c=FAILING)
        self._run()
        got = self._run()
        assert got["test_a"].remembered and (got["test_a"].status, got["test_a"].tests) == (0, 2), "a pass stays"
        assert not got["test_c"].remembered and got["test_c"].status != 0, "a failure runs again"
        assert python_tests.report(list(got.values()), 0) == 1, "and still fails the run"
        got = self._run(key="other")
        assert not got["test_a"].remembered, "other inputs: it runs"
        got = self._run(key="other", use_cache=False)
        assert not got["test_a"].remembered, "--no-cache: it runs"
        _write(self.root, test_c=PASSING)
        got = self._run(key="other")
        assert got["test_a"].remembered and got["test_c"].status == 0 and not got["test_c"].remembered
        got = self._run(key="other")
        assert all(o.remembered for o in got.values())
        assert python_tests.report(list(got.values()), 0) == 0

    def test_a_failure_forgets_the_earlier_pass(self):
        _write(self.root, test_a=PASSING)
        self._run()
        _write(self.root, test_a=FAILING)
        assert self._run(use_cache=False)["test_a"].status != 0
        assert self._run()["test_a"].status != 0, "the old pass must not hide the failure"

    def test_a_pass_on_inputs_changed_meanwhile_is_not_remembered(self):
        _write(self.root, test_a=PASSING)
        keys = iter(["before", "during"])
        (got,) = python_tests.run_modules(self.root, "pkg", self.logs, lambda: next(keys), 2)
        assert got.status == 0, "it passed, and the run reports so"
        assert not self._run(key="before")["test_a"].remembered, "the module may have read the later files"

    def test_no_modules_is_a_failure(self):
        _write(self.root)
        assert python_tests.report(list(self._run().values()), 0) == 1

    def test_modules_have_their_own_temporary_directory_removed_after(self):
        _write(self.root, test_a="import os, unittest\nclass T(unittest.TestCase):\n"
                                 "    def test_a(self):\n"
                                 "        open(os.path.join(os.environ['TMPDIR'], 'left'), 'w').close()\n")
        assert self._run()["test_a"].status == 0
        assert not (self.logs / "tmp").exists()

    def test_a_stopped_run_stops_its_modules(self):
        # the run as tools/ci.sh starts it, with a module that outlives the run unless the run takes it down
        marker = self.root / "pid"
        _write(self.root, test_a=f"import os, time, unittest\nclass T(unittest.TestCase):\n"
                                 f"    def test_a(self):\n"
                                 f"        open({str(marker)!r}, 'w').write(str(os.getpid()))\n"
                                 f"        time.sleep(600)\n")
        code = (f"import sys; sys.path.insert(0, {str(ROOT / 'tools')!r}); import python_tests as c; "
                f"from pathlib import Path; c.stop_on_signals(); "
                f"c.run_modules(Path({str(self.root)!r}), 'pkg', Path({str(self.logs)!r}), lambda: 'k', 1)")
        run = subprocess.Popen([sys.executable, "-c", code])
        self.addCleanup(run.wait)  # the last, after the kill below
        self.addCleanup(run.kill)
        deadline = time.monotonic() + 60
        while not (marker.exists() and marker.read_text()) and time.monotonic() < deadline:
            time.sleep(0.05)
        pid = int(marker.read_text())
        # twice, as tools/ci.sh's trap repeats a signal sent to the whole process group
        run.send_signal(signal.SIGTERM)
        run.send_signal(signal.SIGTERM)
        assert run.wait(timeout=60) == 128 + signal.SIGTERM
        while time.monotonic() < deadline:
            try:
                os.kill(pid, 0)
            except ProcessLookupError:
                break
            time.sleep(0.05)
        else:
            os.kill(pid, signal.SIGKILL)
            self.fail("the module outlived the stopped run")
        assert not (self.logs / "tmp").exists()

    def test_stopping_a_module_that_has_just_ended(self):
        process = subprocess.Popen([sys.executable, "-c", ""], start_new_session=True)
        deadline = time.monotonic() + 60
        while time.monotonic() < deadline:  # a zombie: ended and not polled, which macOS's killpg answers with EPERM
            state = subprocess.run(["ps", "-o", "stat=", "-p", str(process.pid)], capture_output=True, text=True).stdout
            if state.startswith("Z"):
                break
            time.sleep(0.05)
        else:
            process.kill()
            self.fail("the process did not end")
        python_tests._kill_group(process)
        assert process.returncode == 0


class RepositoryKeyTest(unittest.TestCase):
    READ = ("tools/research/scdata/kpf/kpd.py", "defs/ui/dialogs.json", "docs/research/notes.md", "tests/golden/a.json",
            "tools/compatibility.py", "README.md")
    NOT_READ = ("native/kp_core/src/kp_text.c", "tests/c/test_save.c", "docs/formats/kpd.md", ".github/workflows/ci.yml",
                "tools/ci.sh", "build/ignored")

    def test_key_follows_the_files_the_tests_read(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            subprocess.run(["git", "init", "-q"], cwd=root, check=True, capture_output=True)
            (root / ".gitignore").write_text("build/\n")
            for name in self.READ + self.NOT_READ:
                (root / name).parent.mkdir(parents=True, exist_ok=True)
                (root / name).write_text("one")
            base = python_tests.inputs_key(root)
            assert python_tests.inputs_key(root) == base
            for name in self.NOT_READ:
                (root / name).write_text("two")
                assert python_tests.inputs_key(root) == base, name
            for name in self.READ:
                (root / name).write_text("two")
                assert python_tests.inputs_key(root) != base, name
                (root / name).write_text("one")
            (root / "tools/research/scdata/kpf/new.py").write_text("")
            assert python_tests.inputs_key(root) != base, "a new file not committed yet"

    def test_a_package_without_metadata_is_no_failure(self):
        with tempfile.TemporaryDirectory() as tmp:
            (Path(tmp) / "half_removed-1.0.dist-info").mkdir()
            with mock.patch.object(sys, "path", [tmp, *sys.path]):
                assert str(Path(tmp) / "half_removed-1.0.dist-info") in python_tests._packages()
            assert str(Path(tmp) / "half_removed-1.0.dist-info") not in python_tests._packages()

    def test_the_programs_are_inputs(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp) / "repository"
            root.mkdir()
            subprocess.run(["git", "init", "-q"], cwd=root, check=True, capture_output=True)
            library = Path(tmp) / "libstorm-1.dylib"
            library.write_bytes(b"one")
            os.utime(library, ns=(1, 1))
            with mock.patch.dict(os.environ, {python_tests.stormlib.LIBRARY_ENV: str(library)}):
                base = python_tests.inputs_key(root)
                os.utime(library, ns=(2, 2))
                assert python_tests.inputs_key(root) != base, "the library's file"


if __name__ == "__main__":
    unittest.main()
