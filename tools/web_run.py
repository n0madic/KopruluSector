#!/usr/bin/env python3
"""The product's web build run in a headless Chrome: its page served with byte ranges (tools/web_serve.py, on a port
of its own), the app's arguments given by ?args=, the page's console lines written to a log as they come, until one
matches --until (exit 0) or --seconds pass (exit 1). tools/ci.sh checks the web this way over Remastered's repack in
groups (ADR 0019): a language's group fetched whole, a movie and the HD art's pages by their ranges.

Usage: python3 tools/web_run.py BUNDLE LOG --until REGEX [--seconds N] [-- ARG...]
  BUNDLE  the web bundle (dist/product/web: index.html beside generated/)
  LOG     the console's lines, one a line (overwritten)
  ARG     the app's arguments (`--locale ruRU`, `--script generated/scripts/NAME.kps --script-out /tmp/NAME`)

Chrome is KOPRULU_CHROME, else macOS's Google Chrome, else google-chrome, chromium or chromium-browser on the PATH. Its
profile is a temporary folder, removed after the run, as is the server.
"""
from __future__ import annotations

import argparse
import functools
import logging
import os
import re
import shutil
import subprocess
import sys
import tempfile
import threading
import time
import urllib.parse
from http.server import ThreadingHTTPServer
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from web_serve import RangeHandler  # noqa: E402

log = logging.getLogger("web_run")

MAC_CHROME = "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"
CHROMES = ("google-chrome", "chromium", "chromium-browser")
# Chrome's line for a console message on stderr (--enable-logging=stderr): `[...:INFO:CONSOLE:1] "text", source: URL (1)`
CONSOLE = re.compile(r':CONSOLE[^\]]*\] "(.*)", source: ')
CHROME_FLAGS = (
    "--headless=new",
    "--no-first-run",
    "--no-default-browser-check",
    "--enable-logging=stderr",
    "--v=0",
    "--enable-unsafe-swiftshader",  # WebGL 2 where no GPU is there
    "--autoplay-policy=no-user-gesture-required",
    "--mute-audio",
    "--window-size=1280,720",  # tools/ci.sh's APP_WINDOW
    "--force-device-scale-factor=2",  # a Retina display's: the HD art wants 2 pixels or more a logical one
)
STOP_SECONDS = 5  # Chrome's time to end after SIGTERM before it is killed


class QuietHandler(RangeHandler):
    """The range server without a line a request."""

    def log_message(self, format, *args):  # noqa: A002 - the base class's name
        pass


class QuietServer(ThreadingHTTPServer):
    """The range server, a response the page gave up (Chrome ended during a fetch) said by no traceback."""

    daemon_threads = True

    def handle_error(self, request, client_address):
        if not isinstance(sys.exc_info()[1], ConnectionError):
            super().handle_error(request, client_address)


def find_chrome() -> str:
    """The headless Chrome to run, or a SystemExit naming how to give one."""
    named = os.environ.get("KOPRULU_CHROME")
    if named:
        return named
    if os.path.exists(MAC_CHROME):
        return MAC_CHROME
    for name in CHROMES:
        path = shutil.which(name)
        if path:
            return path
    raise SystemExit("web_run: no Chrome (KOPRULU_CHROME names one)")


def run(bundle: Path, out: Path, until: re.Pattern[str], seconds: float, args: list[str]) -> int:
    """The page until a console line matches until: 0, else 1 after seconds (the lines in out either way)."""
    handler = functools.partial(QuietHandler, directory=str(bundle))
    with QuietServer(("127.0.0.1", 0), handler) as server, tempfile.TemporaryDirectory() as profile:
        threading.Thread(target=server.serve_forever, daemon=True).start()
        query = urllib.parse.urlencode({"args": " ".join(args)}, quote_via=urllib.parse.quote, safe="/")
        url = f"http://127.0.0.1:{server.server_address[1]}/index.html?{query}"
        command = [find_chrome(), f"--user-data-dir={profile}", *CHROME_FLAGS, url]
        log.info("web_run: %s", url)
        matched = threading.Event()

        def read(chrome: subprocess.Popen[str]) -> None:
            assert chrome.stderr is not None
            with out.open("w", encoding="utf-8") as lines:
                for line in chrome.stderr:
                    found = CONSOLE.search(line)
                    if not found:
                        continue
                    text = found.group(1)
                    lines.write(text + "\n")
                    lines.flush()
                    if until.search(text):
                        matched.set()

        chrome = subprocess.Popen(command, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                                  stderr=subprocess.PIPE, text=True, errors="replace")
        reader = threading.Thread(target=read, args=(chrome,), daemon=True)
        reader.start()
        try:
            end = time.monotonic() + seconds
            while not matched.is_set() and time.monotonic() < end and chrome.poll() is None:
                matched.wait(0.1)
        finally:
            chrome.terminate()
            try:
                chrome.wait(STOP_SECONDS)
            except subprocess.TimeoutExpired:
                chrome.kill()
                chrome.wait()
            reader.join(STOP_SECONDS)
            server.shutdown()
    if matched.is_set():
        return 0
    log.error("web_run: no console line matching %r in %g s (%s)", until.pattern, seconds, out)
    return 1


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("bundle", type=Path, help="the web bundle (index.html beside generated/)")
    parser.add_argument("log", type=Path, help="the console's lines (overwritten)")
    parser.add_argument("--until", required=True, help="the console line that ends the run (a regular expression)")
    parser.add_argument("--seconds", type=float, default=120, help="the run's limit (default 120)")
    argv = sys.argv[1:]
    cut = argv.index("--") if "--" in argv else len(argv)  # the app's arguments after "--"
    ns = parser.parse_args(argv[:cut])
    logging.basicConfig(level=logging.INFO, format="%(message)s")
    if not (ns.bundle / "index.html").is_file():
        raise SystemExit(f"web_run: no {ns.bundle / 'index.html'}")
    return run(ns.bundle, ns.log, re.compile(ns.until), ns.seconds, argv[cut + 1:])


if __name__ == "__main__":
    sys.exit(main())
