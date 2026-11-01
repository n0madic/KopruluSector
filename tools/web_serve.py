"""A static server for the product's web build with HTTP Range requests (ADR 0013 R4b: the web build reads the HD2
art's pages, frames and clips as byte ranges, which `python3 -m http.server` does not serve: it answers 200 with the
whole file).

    python3 tools/web_serve.py [--port 8793] [--dir dist/product/web]
"""
from __future__ import annotations

import argparse
import functools
import logging
import os
import re
from http import HTTPStatus
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

log = logging.getLogger(__name__)

RANGE = re.compile(r"bytes=(\d*)-(\d*)$")
CHUNK = 1 << 20
WEB = Path(__file__).resolve().parents[1] / "dist" / "product" / "web"


class RangeHandler(SimpleHTTPRequestHandler):
    """SimpleHTTPRequestHandler with single byte ranges (206 Partial Content, 416 past the end)."""

    def send_head(self):
        match = RANGE.match(self.headers.get("Range", "").strip())
        path = self.translate_path(self.path)
        if not match or not os.path.isfile(path):
            return super().send_head()
        size = os.path.getsize(path)
        first, last = match.groups()
        if first == "":  # the last N bytes
            start, end = max(0, size - int(last or 0)), size - 1
        else:
            start, end = int(first), min(int(last), size - 1) if last else size - 1
        if start >= size or start > end:
            self.send_response(HTTPStatus.REQUESTED_RANGE_NOT_SATISFIABLE)
            self.send_header("Content-Range", f"bytes */{size}")
            self.end_headers()
            return None
        f = open(path, "rb")  # noqa: SIM115 - closed by copyfile
        f.seek(start)
        self.send_response(HTTPStatus.PARTIAL_CONTENT)
        self.send_header("Content-Type", self.guess_type(path))
        self.send_header("Content-Range", f"bytes {start}-{end}/{size}")
        self.send_header("Content-Length", str(end - start + 1))
        self.send_header("Accept-Ranges", "bytes")
        self.end_headers()
        self.range_left = end - start + 1
        return f

    def copyfile(self, source, outputfile):
        left = getattr(self, "range_left", None)
        if left is None:
            return super().copyfile(source, outputfile)
        try:
            while left > 0:
                chunk = source.read(min(CHUNK, left))
                if not chunk:
                    break
                outputfile.write(chunk)
                left -= len(chunk)
        finally:
            self.range_left = None

    def end_headers(self):
        self.send_header("Cache-Control", "no-cache")
        super().end_headers()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--port", type=int, default=8793)
    parser.add_argument("--dir", default=str(WEB), help="the web build (default: the repository's)")
    args = parser.parse_args()
    logging.basicConfig(level=logging.INFO, format="%(message)s")
    handler = functools.partial(RangeHandler, directory=args.dir)
    with ThreadingHTTPServer(("127.0.0.1", args.port), handler) as server:
        log.info("serving %s on http://127.0.0.1:%d/ (byte ranges)", args.dir, args.port)
        server.serve_forever()


if __name__ == "__main__":
    main()
