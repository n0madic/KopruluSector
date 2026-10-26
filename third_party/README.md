# Third-party files

| Path | What | Source | License |
|---|---|---|---|
| `stb/stb_truetype.h` | stb_truetype v1.26: TrueType parsing and glyph rasterization for the vector text of large scales (`native/kp_present/kp_vtext.c`) | github.com/nothings/stb | public domain or MIT (the file's end) |
| `fonts/exo2/Exo2.ttf` | Exo 2 (variable; stb_truetype draws its default instance) by the Exo 2 Project Authors: the vector text's font, packed into `ui.kpd` `VFNT` by the converter | github.com/google/fonts (`ofl/exo2`) | SIL Open Font License 1.1 (`fonts/exo2/OFL.txt`) |
| `fonts/michroma/Michroma-Regular.ttf` | Michroma by the Michroma Project Authors: the vector font of font16x, the original's wide menu font, in `ui.kpd` `VFNT` | github.com/google/fonts (`ofl/michroma`) | SIL Open Font License 1.1 (`fonts/michroma/OFL.txt`) |

`stb/kp_stb_truetype.c` compiles the implementation once (`kp_stb` in CMakeLists.txt), without the project's warnings.
