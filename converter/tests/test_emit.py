"""Shared emitter helpers (no original data needed)."""
from __future__ import annotations

import unittest

from converter.emit import atlas
from converter.emit.files import ROOT, project_path


class EmitTest(unittest.TestCase):
    def test_animation_id(self):
        assert atlas.animation_id("unit\\Terran\\marine.grp") == "terran_marine"
        assert atlas.animation_id("unit\\thingy\\fire.grp") == "thingy_fire"

    def test_project_path(self):
        assert project_path(ROOT / "generated" / "x.ogg") == "/generated/x.ogg"
