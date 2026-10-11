"""Full-game emulation oracle for StarCraft: Brood War 1.17.0.1.

The original StarCraft.exe and storm.dll run as x86 code in Unicorn; only the Windows
API boundary is emulated in Python (package :mod:`sc_emulator.hle`). See
docs/research/full-game-emulation.md.
"""
