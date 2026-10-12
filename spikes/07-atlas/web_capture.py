#!/usr/bin/env python3
"""Run a bundled web spike in headless Chrome and save its canvas and SPIKE lines.

The page is served with an injected probe script that
  * optionally hides WebGL 2 (?webgl=1) so the engine must fall back to WebGL 1,
  * requests preserveDrawingBuffer so the canvas can be read back,
  * collects console "SPIKE ..." lines, and on "SPIKE capture" posts the canvas as PNG.
Usage: web_capture.py BUNDLE_DIR OUT_PREFIX [--webgl 1|2] [--timeout 90]
Writes OUT_PREFIX.log and OUT_PREFIX.png.
"""
from __future__ import annotations

import argparse
import base64
import functools
import http.server
import logging
import os
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path

log = logging.getLogger("web_capture")
CHROME = os.environ.get("CHROME", "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome")

PROBE = """<script>
(function () {
  var params = new URLSearchParams(location.search);
  var hideWebGL2 = params.get('webgl') === '1';
  var orig = HTMLCanvasElement.prototype.getContext;
  HTMLCanvasElement.prototype.getContext = function (type, attrs) {
    if (hideWebGL2 && type === 'webgl2') return null;
    if (type === 'webgl' || type === 'webgl2') {
      attrs = Object.assign({}, attrs || {}, {preserveDrawingBuffer: true});
    }
    return orig.call(this, type, attrs);
  };
  var lines = [];
  var post = function (path, body) { return fetch(path, {method: 'POST', body: body}); };
  var log = console.log.bind(console);
  console.log = function () {
    var text = Array.prototype.join.call(arguments, ' ');
    if (/ERROR|Error/.test(text)) {
      lines.push('SPIKE console ' + text);
      post('/partial', lines.join('\\n'));
    }
    if (text.indexOf('SPIKE ') === 0) {
      lines.push(text);
      if (text === 'SPIKE capture') {
        requestAnimationFrame(function () {
          post('/capture', document.getElementById('canvas').toDataURL('image/png'));
        });
      }
      post(text === 'SPIKE done' ? '/log' : '/partial', lines.join('\\n'));
    }
    log.apply(null, arguments);
  };
  window.addEventListener('error', function (e) {
    lines.push('SPIKE js_error ' + e.message);
    post('/partial', lines.join('\\n'));
  });
  ['error', 'warn'].forEach(function (level) {
    var orig = console[level].bind(console);
    console[level] = function () {
      lines.push('SPIKE console_' + level + ' ' + Array.prototype.join.call(arguments, ' '));
      post('/partial', lines.join('\\n'));
      orig.apply(null, arguments);
    };
  });
})();
</script>
"""


class Handler(http.server.SimpleHTTPRequestHandler):
    results: dict[str, bytes]

    def do_POST(self) -> None:  # noqa: N802 (http.server API)
        body = self.rfile.read(int(self.headers["Content-Length"]))
        self.results[self.path] = body
        self.send_response(204)
        self.end_headers()

    def log_message(self, *args: object) -> None:
        pass


def main() -> int:
    logging.basicConfig(level=logging.INFO, format="%(message)s")
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("bundle", type=Path)
    ap.add_argument("out")
    ap.add_argument("--webgl", choices=("1", "2"), default="2")
    ap.add_argument("--timeout", type=int, default=90)
    ap.add_argument("--port", type=int, default=8791)
    a = ap.parse_args()
    page = a.bundle / "index.html"
    html = page.read_text()
    (a.bundle / "probe.html").write_text(html.replace("<head>", "<head>" + PROBE, 1))
    results: dict[str, bytes] = {}
    handler = functools.partial(type("H", (Handler,), {"results": results}), directory=str(a.bundle))
    server = http.server.ThreadingHTTPServer(("127.0.0.1", a.port), handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    with tempfile.TemporaryDirectory() as profile:
        chrome = subprocess.Popen(
            [CHROME, "--headless=new", "--use-angle=metal", "--no-first-run", f"--user-data-dir={profile}",
             "--window-size=1280,720", f"http://127.0.0.1:{a.port}/probe.html?webgl={a.webgl}"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            deadline = time.time() + a.timeout
            while time.time() < deadline and "/log" not in results:
                time.sleep(0.5)
        finally:
            chrome.terminate()
            chrome.wait()
            server.shutdown()
    text = results.get("/log", results.get("/partial", b"") + b"\nSPIKE timeout").decode()
    Path(a.out + ".log").write_text(text + "\n")
    print(text)
    if "/capture" in results:
        data = results["/capture"].split(b",", 1)[1]
        Path(a.out + ".png").write_bytes(base64.b64decode(data))
        log.info("capture: %s.png", a.out)
    return 0 if "/log" in results else 1


if __name__ == "__main__":
    sys.exit(main())
