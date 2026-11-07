# ADR 0016: Localization: UTF-8 text, overlay packs and one font

Status: accepted (2026-11-07; the user approved the design and chose Eurostile at the specimens' gate F0, and kept it at the real screens' gate F1 on 2026-11-08: Eurostile and Eurostile Extended are the vector fonts and the supplement's source for every language). Stages and progress: [docs/phase-l10n.md](../phase-l10n.md).

## Context

The product speaks enUS. StarCraft: Remastered's storage holds 13 languages. The single-byte ones (deDE, esES, esMX, frFR, itIT, plPL, ptBR, ruRU) come first, the CJK ones (koKR, jaJP, zhCN, zhTW) possibly later, and speech is an optional extra.

The engine is the original's: the text is bytes, a font has 224 byte slots (FNT 32..255), the vector text of large scales (ADR 0011) is laid out on the cells of that FNT, and the oracles check all of this on enUS.

Measured on Remastered 1.23.10 (ruRU is the only other language installed here):

- enUS's converted text has non-ASCII in 5 rows of `gluHist` (byte `0x92`) and 2 bytes of `POOL` (`0xA9`), none in the maps' strings. Of the `.txt` files only `gluAll.txt`, which the product does not read, has any, and it is UTF-8 already.
- ruRU's map `STR` and `.txt` are UTF-8 already. Its XML is UTF-8 with decimal `\\NNN` escapes and `0x07` for an inner NUL.
- ruRU's `stat_txt` in UTF-8 is 68,480 bytes, past the TBL's u16 offsets; in cp1251 it would be 44,324. Map `STR` keeps its u16 offsets as the CHK has them (at most 25,275 bytes).
- ruRU's `TRIG` and `MBRF` differ from the base only in the actions' `TIME` (transmissions and sounds, -9.2..+17.3 s), except `EXPTerran/Terran03`, whose structure differs. `TIME` reaches the simulation through the portrait's end (`kp_trigger_release_wait`), so speech changes timing; text does not.
- ruRU's dialog hotkeys are the Latin letter of the physical key (`О` of «Отмена» is `j`): 179 of 192. Its `stat_txt` buttons keep the English letter: 234 of 234.
- Three rows differ from the base in `%`: `STR_PERCENT`, and `gluAll`'s `profile_season_mapStat` and `profile_season_dateformatting`.
- No localized image but `SCR_logo`; 20 localized `.srt`.

## Decision

1. **All text the runtime reads is UTF-8; there are no code pages.**
   - UTF-8 leaves bytes below `0x80` alone, so the control codes, spaces, `%`, line feeds and hotkey prefixes keep their bytes and a byte scan for them stays right.
   - `kp_text` decodes: a valid sequence gives its code point, and any other byte gives the code point of that byte in WHATWG's windows-1252, a bijection of the 256 bytes. The original engine's byte probes (33 of them hold high bytes, none valid UTF-8) therefore stay exact.
   - The converter refuses invalid UTF-8; only old profiles' names and the probes reach that branch.
   - The converter transcodes the 7 places of the enUS base.
2. **A glyph is found by code point:** ASCII and windows-1252's code points take their slot of the original FNT, anything else the glyph supplement. A code point in neither has width 0 and is logged once.
3. **A language is a set of overlay packs of the existing kinds,** read before the base section by section. There is no new container kind.
   - `generated/locale/<loc>/strings.kpd` (`STRS`; TBL with u32 offsets: section schema 2), `ui.kpd` (`UI  `, partial: `DLGS`, `POOL`, `GFIL`), `cinematics.kpd` (`CINE`, only `SUBS`), `missions/<c>/<m>.kpm` (`MISN`, partial: `STRS`).
   - `generated/speech/<loc>/missions/<c>/<m>.kpm` (`TRIG`, `MBRF`), `missions/<c>/<m>_sounds.kpd` (`MSND`), `units.kpd` (`MSND`, keyed by sfx id), `sounds.kpd` (as the base's).
   - A loader checks the overlay's kind, schema and row count; a mismatch rejects that overlay and the base is used.
   - Remastered's own files (XML, JSON, CHK) stay the converter's input. The engine parses no XML, and phones and the web have no install.
4. **One glyph set for every language: `generated/locale/glyphs.kpd`** (kind `FONT`).
   - `VFNT`: the vector fonts, which replace `ui.kpd`'s while this pack is there. Eurostile for the text, Eurostile Extended for font16x (the user's choice, 2026-11-07).
   - `U8  `, `U10 `, `U14 `, `U16 `, `U16X`: the FNT supplement, sorted code points to glyphs in FNT's packet form. It is required even when only vector text shows, because the vector text is laid out on FNT cells.
   - It is rasterized from the same fonts, for the union of all single-byte languages, so a language list or a save name in another script just works.
5. **The fonts come from the user's install, never the repository.** Only a conversion from Remastered writes `glyphs.kpd`.
   - A conversion from 1.17 alone (`--data`) has no TrueType font and no translation. It keeps Exo 2 and Michroma (`third_party/fonts`, SIL OFL) in `ui.kpd` and enUS.
   - `ui.kpd` does not change, so `compare_outputs` between the two sources stays clean and the language pack adds what it needs.
   - The original FNT stays the base at every source; enUS below scale 2 draws it as ever.
6. **What people type is UTF-8 on disk.** Player and save names in the profile (schema 7) do not change when the language does. A name's limit counts characters, its buffer bytes. Schema 6's bytes are read as windows-1252. A file name past the filesystem's limit takes a hash instead of `_xx` escapes.
7. **Hotkeys outside an edit box come from the physical key.** With a non-Latin layout the text event brings a letter no hotkey has; the scancode's Latin letter is what Remastered's ruRU rows expect. This also fixes enUS under a Russian layout. A key with no Latin letter and a press under Alt, Option, AltGr, Ctrl or Command, whose character is the layout's symbol, have no hotkey; one key is one hotkey however many characters it brings.
8. **The language is chosen on the main menu** and applied by restarting the flow in the process (`kp_flow_free`, `kp_flow_init`); a new profile takes `SDL_GetPreferredLocales` when that pack is installed. Development checks and oracles stay on enUS: only the product and `--locale` read a language.
9. **The converter checks a language before writing it:** valid UTF-8, the same `%` specifiers as the base (a listed exception otherwise), the same control-byte shape, ASCII hotkeys, the same row counts, every code point in the glyph set.

## Consequences

- Text alone never changes the simulation: a mission's CRC with ruRU text equals enUS's. Speech changes `TIME`, so it changes timing, and only a product with speech selected sees that.
- A save does not hold text. It loads under any language; its `TRIG` records keep the `TIME` they were saved with, so only the speech's timing can differ.
- Fixed buffers have to hold twice the bytes for Cyrillic (`TOOLTIP_TEXT 128`, `KP_UI_NAME 25` and others); copies cut on a character's boundary and a test requires no cut at all.
- The vector atlas holds more glyphs. Measured in L2 with Eurostile and ruRU's set of 148 code points: 2048 × 1291 at scale 4 (2.6 MB), under `ATLAS_MAX` 4096. A phone's memory under it is looked at in F1.

## Out of scope: CJK

UTF-8 and the supplement carry it. Three things remain, marked `TODO:` where they belong (`kp_ui_text.c`, `kp_vtext.c`): line breaks without spaces, an atlas filled on demand instead of whole, and cell metrics from TrueType instead of thousands of bitmaps. Eurostile has no CJK, so it would also need a second font, as Remastered's own stack does (`bl.ttf`).

## Alternatives rejected

- **Code pages (one table per language):** the converter would transcode what is already UTF-8, names would be garbled by a language change, and a list of languages could not be drawn with one font.
- **New container kinds LOCT and SPCH:** they add nothing over partial packs of the existing kinds.
- **`bl.ttf` as the only font:** the one file covering every language, but neutral beside the game, 7.3 MB, and without `№`. The user chose Eurostile from the specimens.
- **Reading Remastered's files at run time:** no install on phones and the web, an XML parser in the engine, no container checks.
