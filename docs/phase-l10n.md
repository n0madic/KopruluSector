# Localization

Design: [ADR 0016](adr/0016-localization.md). Single-byte languages first, speech optional, CJK out of scope.

| Stage | What | State |
|---|---|---|
| F0 | Font specimens, the user's gate | done 2026-11-07: Eurostile + Eurostile Extended |
| L0 | ADR 0016, `defs/l10n/locales.json` | done 2026-11-07 |
| L1 | UTF-8 in the engine, enUS only: `kp_text`, the glyph log and vector text by code point, input, buffers, profile schema 7, file names, hotkeys by scancode, the base's 7 places transcoded | done 2026-11-07 |
| L2 | `glyphs.kpd` (`kp_fnt_make`), TBL u32, `converter locale`, overlays, `--locale`, the converter's checks; ruRU's text | |
| F1 | Real screens on macOS, Windows, Linux, web, Fold7, iOS Simulator; the user's check of the font on them | |
| L3 | Width report over all dialogs and texts, layout fixes | |
| L4 | Language dialog, translated own dialogs, first-launch language, flow restart, `KOPRULU_LOCALES` and packaging | |
| L5 | Speech: `converter speech`, overlays, its own choice in the profile | |
| L6 | The other single-byte languages (the user installs them in Battle.net first) | |

## L1 as built

- **`kp_text`** decodes on top of `kp_utf8`: a valid sequence is its code point, any other byte its windows-1252 code point. Bytes below `0x80` stay bytes in every scan, so the control codes, the highlight's prefix and its caret byte `0xA6` are found before anything decodes.
- **The engine:** `kp_ui_text.c` steps by character (`char_step`, the span's `next`); a character of a valid sequence with no glyph has width 0 and the text goes on, in the drawing as in the measuring (`text_skips_a_character_with_no_glyph`); a stray byte whose slot is empty still ends a drawn text as the original's does. `kp_ui_glyph` and `kp_ui_char_width` stay byte slots for the probes. The glyph log and `KpUiCanvasGlyph` hold an `int32_t` code point. `kp_vtext` keeps its 256 slots and finds them with `kp_text_byte`, so enUS's atlas is packed as before; `kp_ui_canvas.c` asks the same slot.
- **Input:** `KP_UI_CHAR`'s `key` is the code point (the Basic Multilingual Plane). `& 0xff` is gone from the character handlers, where U+041B would be Escape; `kp_ui_edit.c` steps, deletes and overwrites by character, its cursor and scroll stay bytes at characters' starts, `kp_ui_edit_set_text` moving the old scroll onto the new text's.
- **Hotkeys:** `kp_hud_text_key` turns a non-ASCII character outside an edit box into the physical key's Latin letter, once a text event (an IME's commit or a ligature is one hotkey), and drops it for a key that is no letter or for a soft keyboard. `KpApp.text_vk` comes from every press's scancode at the top of `kp_input_event`: 0 for a key with no virtual key (`;`, `,` and the other punctuation keys) and under Alt, Option, AltGr, Ctrl or Command, whose characters are the layout's symbols. The touch check's `key_letter`, `key_no_letter` and `key_option` steps send these through `kp_input_event`.
- **Limits and buffers:** a name's limit counts characters (24 for an ID, 27 for a save, 78 for the message line); `KP_UI_NAME` is 97 bytes, `KP_UI_SAVE_NAME` 109, `KP_UI_ROSTER_FORCE` 117, `KP_UI_LINE_SIZE` 436; tooltips, formats, status lines and `KP_UI_POPUP_TEXT` are doubled, the credits' texts 4096. The save and load commands' name is limited by the event's text, the dialog's limit being the one that counts. Copies cut on a character's start (`kp_text_copy`, `kp_text_floor`); the score roster keeps the original's cut in characters, 24 for a name and 29 for a force (`kp_text_copy_chars`), so enUS's 30-byte force name loses its last letter as it did.
- **The profile:** schema 7. A profile before it reads its names as windows-1252. `old_save_files` says `KP_SAVE_FILES_BEFORE_IDS` or `KP_SAVE_FILES_BYTES`, and `kp_profile_rename_old_saves` finds the old file names from `kp_text_to_legacy`.
- **File names:** a part past 84 characters of `_xx` (`FILE_PART_MAX`, which no name of the old limits reaches) is `_h` and a 64-bit FNV-1a of its ASCII-lowered bytes.
- **The converter:** `kpd.table_utf8` and `kpd.text_utf8`: on the 1.17 data `GHST` grows 10 bytes (5 × `0x92`), `POOL` 2 (2 × `0xA9`), `DLGS` keeps its size with its offsets moved; nothing else in `generated/data` changes. A table already in UTF-8 stays as it is, and an offset past a table's end stays past it.
- **Checked (2026-11-07):** `tools/ci.sh` with `KOPRULU_ORIGINAL`, `KOPRULU_SCR` and `KOPRULU_APP`: 420 C tests natively, under ASan and UBSan, under LLVM's sanitizers and in wasm, 285 converter tests, the touch check's 8 steps, no golden regenerated. Screenshots of the last commit's build and this one's over their own data are pixel-identical: the main menu and terran01's console, at the original font and the vector font (repeated runs, since the menu's movies differ by phase within either build).
- **Not looked at on screen:** the mission history, the one screen whose text changed bytes, is covered by the width and glyph tests only. No device run, nothing here is phone-specific; a Russian layout's hotkeys are tested through `kp_hud_text_key` and synthetic SDL events, not a real keyboard.

## Acceptance across the stages

- enUS does not change: `tools/ci.sh`, the text probes, the oracles and `tools/menus_check.py` give the same results.
- A mission's CRC with a language's text equals enUS's; only speech may differ.
- Every failure in ADR 0016 falls back to the base and logs once; none stops the game.
