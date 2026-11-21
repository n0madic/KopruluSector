"""tools/doc_links.py: GitHub's heading anchors and the broken links it reports."""
from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import doc_links  # noqa: E402


class SlugTest(unittest.TestCase):
    def test_punctuation_and_code(self) -> None:
        self.assertEqual(doc_links.slug("N2: `kp_snet` and the UDP provider (done 2026-11-17)"),
                         "n2-kp_snet-and-the-udp-provider-done-2026-11-17")
        self.assertEqual(doc_links.slug("Cinematics: no container since ADR 0019's stage C4"),
                         "cinematics-no-container-since-adr-0019s-stage-c4")

    def test_repeated_headings_are_numbered(self) -> None:
        found = doc_links.anchors("# Consequences\n\n## Consequences\n\n```\n# not a heading\n```\n")
        self.assertEqual(found, {"consequences", "consequences-1"})


class BrokenLinksTest(unittest.TestCase):
    def test_reports_missing_files_and_headings_only(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "b.md").write_text("# The part\n\n<a id=\"kept\"></a>\n", encoding="utf-8")
            (root / "a.md").write_text(
                "[ok](b.md#the-part) [id](b.md#kept) [web](https://example.invalid/x#y) `[code](gone.md)`\n"
                "[file](gone.md)\n[heading](b.md#no-such-part)\n[self](#top)\n\n# Top\n", encoding="utf-8")
            broken = doc_links.broken_links(root, ["a.md", "b.md"])
        self.assertEqual(broken, ["a.md:2: no file gone.md", "a.md:3: no heading b.md#no-such-part"])


if __name__ == "__main__":
    unittest.main()
