# Localization

Design: [ADR 0016](adr/0016-localization.md). Single-byte languages first, speech optional, CJK out of scope.

| Stage | What | State |
|---|---|---|
| F0 | Font specimens, the user's gate | done 2026-11-07: Eurostile + Eurostile Extended |
| L0 | ADR 0016, `defs/l10n/locales.json` | done 2026-11-07 |
| L1 | UTF-8 in the engine, enUS only: `kp_text`, the glyph log and vector text by code point, input, buffers, profile schema 7, file names, hotkeys by scancode, the base's 7 places transcoded | next |
| L2 | `glyphs.kpd` (`kp_fnt_make`), TBL u32, `converter locale`, overlays, `--locale`, the converter's checks; ruRU's text | |
| F1 | Real screens on macOS, Windows, Linux, web, Fold7, iOS Simulator; the user's check of the font on them | |
| L3 | Width report over all dialogs and texts, layout fixes | |
| L4 | Language dialog, translated own dialogs, first-launch language, flow restart, `KOPRULU_LOCALES` and packaging | |
| L5 | Speech: `converter speech`, overlays, its own choice in the profile | |
| L6 | The other single-byte languages (the user installs them in Battle.net first) | |

## Acceptance across the stages

- enUS does not change: `tools/ci.sh`, the text probes, the oracles and `tools/menus_check.py` give the same results.
- A mission's CRC with a language's text equals enUS's; only speech may differ.
- Every failure in ADR 0016 falls back to the base and logs once; none stops the game.
