# Third-party files

| Path | What | Source | License |
|---|---|---|---|
| `stb/stb_truetype.h` | stb_truetype v1.26: TrueType parsing and glyph rasterization for the vector text of large scales (`native/kp_present/kp_vtext.c`) | github.com/nothings/stb | public domain or MIT (the file's end) |
| `fonts/rajdhani/Rajdhani-SemiBold.ttf` | Rajdhani SemiBold 1.201 by the Indian Type Foundry: the vector font, packed into `ui.kpd` `VFNT` by the converter | github.com/google/fonts (`ofl/rajdhani`) | SIL Open Font License 1.1 (`fonts/rajdhani/OFL.txt`) |

`stb/kp_stb_truetype.c` compiles the implementation once (`kp_stb` in CMakeLists.txt), without the project's warnings.
