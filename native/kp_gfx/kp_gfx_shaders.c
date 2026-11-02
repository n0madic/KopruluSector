/*
 * The shaders of the index-space pipeline (plan phase S, stage S4): one GLSL ES 3.00 source each, the version line and
 * the precision statements prepended at compile time (kp_gfx_gl.c). The world images read their atlas pages by integer
 * texel (a texture array of every loaded pack's pages; the frames are never rotated), and their per-image data and
 * frame table by texelFetch (ADR 0002, ADR 0006).
 */
#include "kp_gfx_internal.h"

/* The C constants the shaders share, spelled into their sources; the kinds are enums, checked against the literals. */
#define S_(x) #x
#define S(x) S_(x)
typedef char kp_shader_wire_check[KP_UI_WIRE_PARTS <= 9 && KP_UI_WIRE_HIT_POINTS < KP_UI_WIRE_PARTS ? 1 : -1];
typedef char kp_shader_kinds_check[KP_KIND_WARP == 1 && KP_KIND_OUTLINE == 2 && KP_KIND_BAR == 3
	&& KP_KIND_CLOAK_BLEND == 1 && KP_KIND_TRANS50 == 2 && KP_KIND_RAW == 1 && KP_KIND_CLOAK == 2 ? 1 : -1];

/* The full-screen triangle pair of six vertices (no vertex buffer): texcoord (0, 0) bottom-left to (1, 1). */
const char KP_GFX_FULLSCREEN_VS[] =
	"out vec2 var_texcoord0;\n"
	"void main()\n"
	"{\n"
	"    int c = gl_VertexID;\n"
	"    vec2 p = vec2((c == 1 || c == 2 || c == 4) ? 1.0 : 0.0, (c == 2 || c == 4 || c == 5) ? 1.0 : 0.0);\n"
	"    var_texcoord0 = p;\n"
	"    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
	"}\n";

/* The terrain into the index target: every texel's map pixel, its cell's megatile in the tile map (RG16UI: r the
 * megatile word, bit 15 the tile's high bit; g 1 + the creep edge frame drawn over it, 0 none) and the megatile sheet's
 * index there (every terrain texel is opaque, index 0 included), under the edge frame's index where it is not 0 (the
 * frames' cells follow the megatiles in the sheet: 0x4c03b0's blit over the tile); cells outside the map stay
 * cleared. */
const char KP_GFX_TERRAIN_FS[] =
	"uniform highp usampler2D terrain_map;\n"
	"uniform highp sampler2D terrain_sheet;\n"
	"uniform vec4 terrain;      // xy = the view's top-left map pixel, z = the target's height, w = the sheet's columns\n"
	"uniform vec4 terrain_size; // xy = the map's size in tiles, z = the sheet's cell of the first creep edge\n"
	"out vec4 out_fragColor;\n"
	"const int TILE = " S(KP_WORLD_TILE) ";\n"
	"void main()\n"
	"{\n"
	"    ivec2 t = ivec2(gl_FragCoord.xy);\n"
	"    ivec2 m = ivec2(int(terrain.x) + t.x, int(terrain.y) + int(terrain.z) - 1 - t.y);\n"
	"    ivec2 cell = m / TILE;\n"
	"    if (m.x < 0 || m.y < 0 || cell.x >= int(terrain_size.x) || cell.y >= int(terrain_size.y)) discard;\n"
	"    uvec2 c = texelFetch(terrain_map, cell, 0).rg;\n"
	"    int id = int(c.r & 0x7fffu), cols = int(terrain.w);\n"
	"    ivec2 p = m % TILE;\n"
	"    float index = texelFetch(terrain_sheet, ivec2((id % cols) * TILE + p.x, (id / cols) * TILE + p.y), 0).r;\n"
	"    if (c.g != 0u) {\n"
	"        int e = int(terrain_size.z) + int(c.g) - 1;\n"
	"        float edge = texelFetch(terrain_sheet, ivec2((e % cols) * TILE + p.x, (e / cols) * TILE + p.y), 0).r;\n"
	"        if (edge > 0.0) index = edge;\n"
	"    }\n"
	"    out_fragColor = vec4(index, 0.0, 0.0, 1.0);\n"
	"}\n";

/* Shared by the world image vertex stages (SD and HD): the image data, the view and the composition window. */
#define WORLD_VS_COMMON \
	"uniform highp sampler2D image_data;\n" \
	"uniform highp sampler2D frame_table; // the SD frames\n" \
	"uniform vec4 view;      // xy = the world point at the target's bottom-left, zw = the logical screen's size\n" \
	"uniform vec4 interp;    // x = alpha between the last two ticks\n" \
	"uniform vec4 window;\n" \
	"uniform vec4 slot_base; // x = the family's first slot\n" \
	"const int KINDS = " S(KP_DRAW_KINDS) ";\n" \
	"const float DEPTH = 1.0 / " S(KP_RANK_RANGE) ".0;\n" \
	"vec4 fetch(highp sampler2D t, int texel, int width)\n" \
	"{\n" \
	"    return texelFetch(t, ivec2(texel % width, texel / width), 0);\n" \
	"}\n" \
	"// the image's centre at the frame's interpolation between the last two ticks (world pixels)\n" \
	"vec2 centre(vec4 pos) { return (pos.xy - (1.0 - interp.x) * pos.zw) / 256.0; }\n" \
	"// the clip position of world point p at a rank: z = (rank + 1) / RANK_RANGE in (0, 1], the target cleared to 0\n" \
	"vec4 clip(vec2 p, float rank) { return vec4((p - view.xy) / view.zw * 2.0 - 1.0, (rank + 1.0) * DEPTH, 1.0); }\n"
/* The image's data of the vertex's slot: frame, kind, mirror, and its corner of the six. */
#define WORLD_VS_IMAGE \
	"    int corner = gl_VertexID % 6;\n" \
	"    vec4 pos = fetch(image_data, slot * 2, " S(KP_IMAGE_DATA_WIDTH) ");\n" \
	"    vec4 d = fetch(image_data, slot * 2 + 1, " S(KP_IMAGE_DATA_WIDTH) ");\n" \
	"    int code = int(d.x + 0.5);\n" \
	"    int frame = code / (2 * KINDS);\n" \
	"    int kind = (code / 2) % KINDS;\n" \
	"    bool flip = (code % 2) == 1;\n" \
	"    // corners: 0 (0,0) 1 (1,0) 2 (1,1) 3 (0,0) 4 (1,1) 5 (0,1)\n" \
	"    float cx = (corner == 1 || corner == 2 || corner == 4) ? 1.0 : 0.0;\n" \
	"    float cy = (corner == 2 || corner == 4 || corner == 5) ? 1.0 : 0.0;\n"
/* The vertex's slot of the family: the family's slots in slot order. */
#define WORLD_VS_SLOT \
	"    int slot = int(slot_base.x + 0.5) + gl_VertexID / 6;\n" \
	WORLD_VS_IMAGE
/* The vertex's slot of the family in rank order: the n-th of the family's order values (kp_present.h), four a texel
 * after the slots. */
#define WORLD_VS_RANKED_SLOT \
	"    int base = int(slot_base.x + 0.5), n = base + gl_VertexID / 6;\n" \
	"    vec4 order = fetch(image_data, " S(KP_IMAGE_ORDER_TEXEL) " + n / 4, " S(KP_IMAGE_DATA_WIDTH) ");\n" \
	"    int slot = base + int(order[n % 4] + 0.5);\n" \
	WORLD_VS_IMAGE

/* World images: six vertices per slot of the family's range; per-image data in one RGBA32F
 * texture (image_data[2 slot] = (x, y, x - previous x, y - previous y) in 1/256 world pixels, image_data[2 slot + 1] =
 * ((frame * KINDS + kind) * 2 + hflip, rank, row, layer)); the frame table (frame_table[2 f] = (x, y, w, h) in its
 * page, frame_table[2 f + 1] = (page, 0, 0, 0)). The rank becomes the depth, so the image last in the native draw
 * order wins whatever its family (depth test GREATER). window = (lowest rank, rank past the last, layer, 1 = only that
 * layer); in the overwriting passes (window.w = 0) a nonzero layer is a virtual frame's size, width * 256 + height. */
const char KP_GFX_WORLD_VS[] =
	WORLD_VS_COMMON
	"out highp vec2 var_local;            // the frame pixel (source column, row from the top), mirrored as drawn\n"
	"flat out highp vec4 var_image;       // x = page, y = row, z = rank, w = kind\n"
	"flat out highp vec4 var_frame;       // xy = size, z = 1 when mirrored\n"
	"flat out highp vec4 var_origin;      // xy = the frame's top-left in its page\n"
	"flat out highp vec4 var_texture;     // the warp texture frame: x, y, w, h\n"
	"flat out highp float var_texture_page;\n"
	"const int KIND_WARP = 1;\n"
	"void main()\n"
	"{\n"
	WORLD_VS_SLOT
	"    vec4 rect = fetch(frame_table, frame * 2, " S(KP_FRAME_TABLE_WIDTH) ");\n"
	"    vec4 page = fetch(frame_table, frame * 2 + 1, " S(KP_FRAME_TABLE_WIDTH) ");\n"
	"    vec2 size = rect.zw;\n"
	"    float rank = d.y;\n"
	"    if (window.w < 0.5 && d.w > 0.5) size = vec2(floor(d.w / 256.0), mod(d.w, 256.0));\n"
	"    bool outside = rank < window.x || rank >= window.y || (window.w > 0.5 && abs(d.w - window.z) > 0.5);\n"
	"    if (outside) size = vec2(0.0);\n"
	"    gl_Position = clip(centre(pos) + (vec2(cx, cy) - 0.5) * size, rank);\n"
	"    float fx = flip ? 1.0 - cx : cx;\n"
	"    var_local = vec2(fx, 1.0 - cy) * size;\n"
	"    var_image = vec4(page.x, d.z, rank, float(kind));\n"
	"    var_frame = vec4(size, flip ? 1.0 : 0.0, 0.0);\n"
	"    var_origin = vec4(rect.xy, 0.0, 0.0);\n"
	"    if (kind == KIND_WARP) {\n"
	"        int texture_frame = int(d.z + 0.5);\n"
	"        var_texture = fetch(frame_table, texture_frame * 2, " S(KP_FRAME_TABLE_WIDTH) ");\n"
	"        var_texture_page = fetch(frame_table, texture_frame * 2 + 1, " S(KP_FRAME_TABLE_WIDTH) ").x;\n"
	"    } else {\n"
	"        var_texture = vec4(0.0);\n"
	"        var_texture_page = 0.0;\n"
	"    }\n"
	"}\n";

/* Shared by the world image fragment stages. */
#define WORLD_COMMON \
	"uniform highp sampler2DArray atlas;\n" \
	"uniform highp sampler2D remap_table; // 256 x rows, R = mapped index\n" \
	"in highp vec2 var_local;\n" \
	"flat in highp vec4 var_image;\n" \
	"flat in highp vec4 var_frame;\n" \
	"flat in highp vec4 var_origin;\n" \
	"flat in highp vec4 var_texture;\n" \
	"flat in highp float var_texture_page;\n" \
	"out vec4 out_fragColor;\n" \
	"int byte_of(float v) { return int(v * 255.0 + 0.5); }\n" \
	"// the frame's pixel under the fragment (its source column and row from the top)\n" \
	"ivec2 frame_pixel() { return ivec2(min(floor(var_local), var_frame.xy - 1.0)); }\n" \
	"int atlas_index(ivec2 origin, ivec2 p, float page)\n" \
	"{\n" \
	"    return byte_of(texelFetch(atlas, ivec3(origin + p, int(page + 0.5)), 0).r);\n" \
	"}\n" \
	"int frame_index() { return atlas_index(ivec2(var_origin.xy), frame_pixel(), var_image.x); }\n" \
	"int remap(int index, int row) { return byte_of(texelFetch(remap_table, ivec2(index, row), 0).r); }\n" \
	"// the threshold kernels' signed BYTE compare (0x406235): parameter < brightness\n" \
	"bool above_threshold(int brightness, int param) { return (param >= 128 ? param - 256 : param) < brightness; }\n"

/* Overwriting world images, index in R (index 0 is transparent, ADR 0003). By kind:
 * 0 remap (the atlas index through a source remap row); 1 warp texture (method 12: where the frame is opaque, the
 * texture frame's pixel at the same position); 2 size outline (method 15: the frame's edge pixels in colour `row`);
 * 3 health bar (method 11: the bar drawn into `bars` (R index, G drawn) for slot `row`, BAR_ROWS rows per slot). */
const char KP_GFX_WORLD_FS[] =
	WORLD_COMMON
	"uniform highp sampler2D bars;\n"
	"const int KIND_WARP = 1;\n"
	"const int KIND_OUTLINE = 2;\n"
	"const int KIND_BAR = 3;\n"
	"const int BAR_ROWS = " S(KP_BAR_ROWS) ";\n"
	"void main()\n"
	"{\n"
	"    int kind = int(var_image.w + 0.5);\n"
	"    if (kind == KIND_OUTLINE) {\n"
	"        vec2 p = floor(var_local);\n"
	"        if (p.x > 0.5 && p.y > 0.5 && p.x < var_frame.x - 1.5 && p.y < var_frame.y - 1.5) discard;\n"
	"        out_fragColor = vec4(var_image.y / 255.0, 0.0, 0.0, 1.0);\n"
	"        return;\n"
	"    }\n"
	"    if (kind == KIND_BAR) {\n"
	"        ivec2 p = frame_pixel();\n"
	"        vec4 t = texelFetch(bars, ivec2(p.x, int(var_image.y + 0.5) * BAR_ROWS + p.y), 0);\n"
	"        if (t.g < 0.5) discard;\n"
	"        out_fragColor = vec4(t.r, 0.0, 0.0, 1.0);\n"
	"        return;\n"
	"    }\n"
	"    int index = frame_index();\n"
	"    if (index == 0) discard;\n"
	"    if (kind == KIND_WARP) {\n"
	"        // the texture pixel at the frame position; past the texture: native scratch, not drawn\n"
	"        ivec2 p = ivec2(floor(var_local));\n"
	"        if (float(p.x) >= var_texture.z || float(p.y) >= var_texture.w) discard;\n"
	"        index = atlas_index(ivec2(var_texture.xy), p, var_texture_page);\n"
	"        out_fragColor = vec4(float(index) / 255.0, 0.0, 0.0, 1.0);\n"
	"        return;\n"
	"    }\n"
	"    out_fragColor = vec4(float(remap(index, int(var_image.y + 0.5))) / 255.0, 0.0, 0.0, 1.0);\n"
	"}\n";

/* Destination-table world images; dst from the copy made before this layer. By kind:
 * 0 table (methods 9, 10, 17: tables[row + (blend ? src : 0)][dst], row = 2 * table row + blend); 1 cloak blend
 * (methods 5/7: the trans50 blend, then the threshold pass 0x406235; row = colour row * 65536 + owner row * 256 +
 * parameter); 2 trans50 blend (method 6, method 9 with remapping 5; row = colour row). */
const char KP_GFX_DEST_FS[] =
	WORLD_COMMON
	"uniform highp sampler2D dest;\n"
	"uniform highp sampler2D tables;\n"
	"uniform vec4 lookups; // x = trans50 row code, y = brightness row of remap_table\n"
	"const int KIND_CLOAK_BLEND = 1;\n"
	"const int KIND_TRANS50 = 2;\n"
	"const int PARAM_SPAN = " S(KP_DRAW_PARAM_SPAN) ";\n"
	"const int COLOR_SPAN = " S(KP_DRAW_COLOR_SPAN) ";\n"
	"int table(int code, int src, int dst)\n"
	"{\n"
	"    int row = code / 2 + (code % 2 == 1 ? src : 0);\n"
	"    return byte_of(texelFetch(tables, ivec2(dst, row), 0).r);\n"
	"}\n"
	"void main()\n"
	"{\n"
	"    int src = frame_index();\n"
	"    if (src == 0) discard;\n"
	"    // the depth test (GREATER, no write) keeps it where no image ranked after it was drawn\n"
	"    vec4 under = texelFetch(dest, ivec2(gl_FragCoord.xy), 0);\n"
	"    int dst = byte_of(under.r);\n"
	"    int code = int(var_image.y + 0.5);\n"
	"    int kind = int(var_image.w + 0.5);\n"
	"    int trans50 = int(lookups.x + 0.5);\n"
	"    int result;\n"
	"    if (kind == KIND_CLOAK_BLEND) {\n"
	"        int mapped = remap(src, (code / PARAM_SPAN) % PARAM_SPAN);\n"
	"        bool shown = above_threshold(remap(mapped, int(lookups.y + 0.5)), code % PARAM_SPAN);\n"
	"        result = shown ? mapped : table(trans50, remap(src, code / COLOR_SPAN), dst);\n"
	"    } else if (kind == KIND_TRANS50) {\n"
	"        result = table(trans50, remap(src, code), dst);\n"
	"    } else {\n"
	"        result = table(code, src, dst);\n"
	"    }\n"
	"    out_fragColor = vec4(float(result) / 255.0, 0.0, 0.0, 1.0); // the depth (the rank) stays\n"
	"}\n";

/* Distortion world images, one window's copy read: every opaque texel copies the target pixel d bytes away in the
 * target's row-major memory order (rows from the top), ahead for normal frames, behind for mirrored ones, wrapping at
 * the end of the target. By kind: 0 (method 3) d = the source's brightness; 1 (method 8) d = the raw index; 2 (methods
 * 2/4) kind 0 after the threshold pass. */
const char KP_GFX_DISTORT_FS[] =
	WORLD_COMMON
	"uniform highp sampler2D dest;\n"
	"uniform vec4 lookups; // y = brightness row of remap_table\n"
	"const int KIND_RAW = 1;\n"
	"const int KIND_CLOAK = 2;\n"
	"const int PARAM_SPAN = " S(KP_DRAW_PARAM_SPAN) ";\n"
	"void main()\n"
	"{\n"
	"    int src = frame_index();\n"
	"    if (src == 0) discard;\n"
	"    int kind = int(var_image.w + 0.5);\n"
	"    int code = int(var_image.y + 0.5);\n"
	"    int bright_row = int(lookups.y + 0.5);\n"
	"    if (kind == KIND_CLOAK) {\n"
	"        int mapped = remap(src, code / PARAM_SPAN);\n"
	"        if (above_threshold(remap(mapped, bright_row), code % PARAM_SPAN)) {\n"
	"            out_fragColor = vec4(float(mapped) / 255.0, 0.0, 0.0, 1.0);\n"
	"            return;\n"
	"        }\n"
	"    }\n"
	"    int d = kind == KIND_RAW ? src : remap(src, bright_row);\n"
	"    int w = textureSize(dest, 0).x; // the index target's size\n"
	"    int h = textureSize(dest, 0).y;\n"
	"    ivec2 p = ivec2(gl_FragCoord.xy);\n"
	"    int size = w * h;\n"
	"    // GL rows are bottom-up; memory rows are top-down\n"
	"    int off = d % size;\n"
	"    int at = ((h - 1 - p.y) * w + p.x + (var_frame.z > 0.5 ? size - off : off)) % size;\n"
	"    out_fragColor = vec4(texelFetch(dest, ivec2(at % w, h - 1 - at / w), 0).r, 0.0, 0.0, 1.0);\n"
	"}\n";

/* The fog of war's level at a map pixel (DrawFogOverlay 0x46c3c0, the CPU reference kp_fog_view_shade): every 8x8-pixel
 * map block takes the four levels of the level map around its corner; four equal levels 0 make it black, 31 leave it,
 * another level darkens it through that dark.pcx row; unequal ones interpolate per pixel in 8.8 fixed point. Shared
 * by the fog in index space, the smooth fog and the original's fog drawn per fragment over the original's art in
 * RGBA (stage R5). */
#define FOG_LEVELS \
	"uniform highp sampler2D fog_levels;\n" \
	"uniform highp sampler2D fog_dark;\n" \
	"uniform vec4 fog;        // xy = the map pixel of the view's top-left, z = the view's height\n" \
	"uniform vec4 fog_origin; // xy = the map block of the level map's first cell, zw = its size in blocks\n" \
	"const int SEEN = " S(KP_FOG_SEEN) ";\n" \
	"int fog_byte(highp sampler2D t, ivec2 at) { return int(texelFetch(t, at, 0).r * 255.0 + 0.5); }\n" \
	"int level_at(ivec2 block)\n" \
	"{\n" \
	"    ivec2 cell = block - ivec2(fog_origin.xy);\n" \
	"    if (cell.x < 0 || cell.y < 0 || cell.x >= int(fog_origin.z) || cell.y >= int(fog_origin.w)) return 0;\n" \
	"    return fog_byte(fog_levels, cell);\n" \
	"}\n" \
	"// the level at map pixel map; even: the four blocks' levels are equal\n" \
	"int fog_level(ivec2 map, out bool even)\n" \
	"{\n" \
	"    ivec2 block = map / 8, k = map - block * 8;\n" \
	"    int tl = level_at(block), tr = level_at(block + ivec2(1, 0));\n" \
	"    int bl = level_at(block + ivec2(0, 1)), br = level_at(block + ivec2(1, 1));\n" \
	"    even = tl == tr && tl == bl && tl == br;\n" \
	"    if (even) return tl;\n" \
	"    int l = tl * 256 + k.y * (bl - tl) * 32, r = tr * 256 + k.y * (br - tr) * 32;\n" \
	"    int step = (l - r) / 8;\n" \
	"    return (r + (7 - k.x) * step) / 256;\n" \
	"}\n" \
	"// a palette index at map pixel map through the dark row of its level\n" \
	"int fogged(int index, ivec2 map)\n" \
	"{\n" \
	"    bool even;\n" \
	"    int level = fog_level(map, even);\n" \
	"    if (even && level == 0) return 0;\n" \
	"    if (even && level == SEEN) return index;\n" \
	"    return fog_byte(fog_dark, ivec2(index, level));\n" \
	"}\n" \
	"// the light the fog leaves at map pixel map: 0 black .. 1 clear\n" \
	"float fog_light(ivec2 map)\n" \
	"{\n" \
	"    bool even;\n" \
	"    return float(min(fog_level(map, even), SEEN)) / float(SEEN);\n" \
	"}\n" \
	"// the map pixel of a fragment of a target of the logical screen times s\n" \
	"ivec2 fog_map(float s)\n" \
	"{\n" \
	"    ivec2 texel = ivec2(floor(gl_FragCoord.xy / s));\n" \
	"    return ivec2(int(fog.x) + texel.x, int(fog.y) + int(fog.z) - 1 - texel.y);\n" \
	"}\n"

/* The fog of war over the composed view in index space: each texel's index through its level's dark row. */
const char KP_GFX_FOG_FS[] =
	"uniform highp sampler2D dest;\n"
	FOG_LEVELS
	"out vec4 out_fragColor;\n"
	"void main()\n"
	"{\n"
	"    vec4 copy = texelFetch(dest, ivec2(gl_FragCoord.xy), 0);\n"
	"    int index = int(copy.r * 255.0 + 0.5);\n"
	"    out_fragColor = vec4(float(fogged(index, fog_map(1.0))) / 255.0, 0.0, 0.0, 1.0);\n"
	"}\n";

/* The interface's canvas over the composed world (ADR 0011): kp_ui composed the panels into one canvas (index in R,
 * opacity in G: 255 or 254 opaque, 128 translucent, 0 none; rows top first); its opaque texels go into the index
 * target, a translucent one writes the blend row's entry of the world's index under it, and the cursor's frame (index 0
 * transparent, rows top first) lies over the canvas at its rectangle. */
const char KP_GFX_UI_FS[] =
	"uniform highp sampler2D ui_canvas;\n"
	"uniform highp sampler2D dest;\n"
	"uniform highp sampler2D ui_blend;\n"
	"uniform highp sampler2D ui_cursor;\n"
	"uniform vec4 cursor; // xy = the frame's top-left on the logical screen, zw = its size (0: none)\n"
	"uniform vec4 blend;  // x = 1 when the frame has translucent texels\n"
	"out vec4 out_fragColor;\n"
	"void main()\n"
	"{\n"
	"    ivec2 size = textureSize(ui_canvas, 0);\n"
	"    ivec2 at = ivec2(gl_FragCoord.xy);\n"
	"    ivec2 in_cursor = ivec2(at.x, size.y - 1 - at.y) - ivec2(cursor.xy);\n"
	"    if (in_cursor.x >= 0 && in_cursor.y >= 0 && in_cursor.x < int(cursor.z) && in_cursor.y < int(cursor.w)) {\n"
	"        float c = texelFetch(ui_cursor, in_cursor, 0).r;\n"
	"        if (c > 0.5 / 255.0) {\n"
	"            out_fragColor = vec4(c, 0.0, 0.0, 1.0);\n"
	"            return;\n"
	"        }\n"
	"    }\n"
	"    vec4 texel = texelFetch(ui_canvas, ivec2(at.x, size.y - 1 - at.y), 0);\n"
	"    if (texel.g < 0.25) discard;\n"
	"    if (texel.g < 0.75) {\n"
	"        if (blend.x < 0.5) discard;\n"
	"        int under = int(texelFetch(dest, at, 0).r * 255.0 + 0.5);\n"
	"        out_fragColor = vec4(texelFetch(ui_blend, ivec2(under, 0), 0).r, 0.0, 0.0, 1.0);\n"
	"        return;\n"
	"    }\n"
	"    out_fragColor = vec4(texel.r, 0.0, 0.0, 1.0);\n"
	"}\n";

/* The palette pass: index target -> RGB through the 256 x 1 palette texture (not a uniform array: WebGL2 guarantees
 * only 224 fragment uniform vectors, stage S8). */
const char KP_GFX_PALETTE_FS[] =
	"in vec2 var_texcoord0;\n"
	"uniform highp sampler2D index_target;\n"
	"uniform highp sampler2D palette;   // 256 x 1 RGBA8: the colour of index i\n"
	"uniform vec4 source_size;           // xy = the index target's size\n"
	"out vec4 out_fragColor;\n"
	"void main()\n"
	"{\n"
	"    ivec2 texel = ivec2(floor(var_texcoord0 * source_size.xy));\n"
	"    texel = clamp(texel, ivec2(0), ivec2(source_size.xy) - 1);\n"
	"    int index = int(texelFetch(index_target, texel, 0).r * 255.0 + 0.5);\n"
	"    out_fragColor = vec4(texelFetch(palette, ivec2(index, 0), 0).rgb, 1.0);\n"
	"}\n";

/* The sharp bilinear upscale: nearest inside each source texel, a one-output-pixel bilinear blend across texel
 * edges. */
const char KP_GFX_SHARP_FS[] =
	"in vec2 var_texcoord0;\n"
	"uniform highp sampler2D rgb_target; // linear filtering\n"
	"uniform vec4 source_size;          // xy = source texels, zw = scale factors\n"
	"out vec4 out_fragColor;\n"
	"void main()\n"
	"{\n"
	"    vec2 texel = var_texcoord0 * source_size.xy;\n"
	"    vec2 base = floor(texel);\n"
	"    vec2 frac_part = texel - base;\n"
	"    vec2 region = 0.5 - 0.5 / source_size.zw;\n"
	"    vec2 center_dist = frac_part - 0.5;\n"
	"    vec2 f = (center_dist - clamp(center_dist, -region, region)) * source_size.zw + 0.5;\n"
	"    out_fragColor = texture(rgb_target, (base + f) / source_size.xy);\n"
	"}\n";

/* A cinematic's picture (ADR 0015) over the viewport: its three planes (row 0 on top) sampled between their texels,
 * the chroma's coordinates scaled to the luma's (video_chroma); video_gb.z: the planes are G, B, R (sRGB), else Y, Cb, Cr
 * turned into RGB by video_yuv (y_low, y_scale, cr_r, cb_b) and video_gb (cb_g, cr_g), kp_gfx_video's. */
const char KP_GFX_VIDEO_FS[] =
	"in vec2 var_texcoord0;\n"
	"uniform highp sampler2D video_y;\n"
	"uniform highp sampler2D video_cb;\n"
	"uniform highp sampler2D video_cr;\n"
	"uniform highp vec4 video_yuv;\n"
	"uniform highp vec4 video_gb;\n"
	"uniform highp vec4 video_chroma;\n"
	"out vec4 out_fragColor;\n"
	"void main()\n"
	"{\n"
	"    vec2 uv = vec2(var_texcoord0.x, 1.0 - var_texcoord0.y);\n"
	"    highp float y = texture(video_y, uv).r;\n"
	"    highp float cb = texture(video_cb, uv * video_chroma.xy).r;\n"
	"    highp float cr = texture(video_cr, uv * video_chroma.xy).r;\n"
	"    if (video_gb.z > 0.5) {\n"
	"        out_fragColor = vec4(cr, y, cb, 1.0);\n"
	"        return;\n"
	"    }\n"
	"    highp float yy = (y - video_yuv.x) * video_yuv.y;\n"
	"    cb -= 128.0 / 255.0;\n"
	"    cr -= 128.0 / 255.0;\n"
	"    vec3 rgb = vec3(yy + video_yuv.z * cr, yy - video_gb.x * cb - video_gb.y * cr, yy + video_yuv.w * cb);\n"
	"    out_fragColor = vec4(clamp(rgb, 0.0, 1.0), 1.0);\n"
	"}\n";

/* The vector text (ADR 0011, 2026-10-26) over the scaled frame: each quad's four texels in text_quads (its rectangle
 * and atlas coordinates, its clip, its palette index) on the logical screen; the viewport is the frame's. */
const char KP_GFX_TEXT_VS[] =
	"uniform highp sampler2D text_quads;\n"
	"uniform vec4 source_size;           // xy = the logical screen\n"
	"out vec2 var_texcoord0;\n"
	"out vec2 var_logical;\n"
	"flat out vec4 var_clip;\n"
	"flat out float var_colour;\n"
	"void main()\n"
	"{\n"
	"    int q = gl_VertexID / 6, c = gl_VertexID - 6 * (gl_VertexID / 6);\n"
	"    const int PER_ROW = " S(KP_TEXT_QUADS_PER_ROW) ";\n"
	"    ivec2 at = ivec2((q - PER_ROW * (q / PER_ROW)) * 4, q / PER_ROW);\n"
	"    vec4 rect = texelFetch(text_quads, at, 0);\n"
	"    vec4 uv = texelFetch(text_quads, at + ivec2(1, 0), 0);\n"
	"    var_clip = texelFetch(text_quads, at + ivec2(2, 0), 0);\n"
	"    var_colour = texelFetch(text_quads, at + ivec2(3, 0), 0).x;\n"
	"    vec2 p = vec2((c == 1 || c == 2 || c == 4) ? 1.0 : 0.0, (c == 2 || c == 4 || c == 5) ? 1.0 : 0.0);\n"
	"    var_logical = mix(rect.xy, rect.zw, p);\n"
	"    var_texcoord0 = mix(uv.xy, uv.zw, p);\n"
	"    gl_Position = vec4(var_logical.x / source_size.x * 2.0 - 1.0, 1.0 - var_logical.y / source_size.y * 2.0,\n"
	"        0.0, 1.0);\n"
	"}\n";

/* The glyph's coverage (bilinear over the one-byte atlas), coloured by the palette; the cursor stays on top. */
const char KP_GFX_TEXT_FS[] =
	"in vec2 var_texcoord0;\n"
	"in vec2 var_logical;\n"
	"flat in vec4 var_clip;\n"
	"flat in float var_colour;\n"
	"uniform highp sampler2D text_atlas;\n"
	"uniform highp sampler2D palette;\n"
	"uniform highp sampler2D ui_cursor;\n"
	"uniform vec4 cursor; // xy = the frame's top-left on the logical screen, zw = its size (0: none)\n"
	"out vec4 out_fragColor;\n"
	"void main()\n"
	"{\n"
	"    if (var_logical.x < var_clip.x || var_logical.y < var_clip.y || var_logical.x >= var_clip.z\n"
	"        || var_logical.y >= var_clip.w) discard;\n"
	"    ivec2 in_cursor = ivec2(floor(var_logical)) - ivec2(cursor.xy);\n"
	"    if (in_cursor.x >= 0 && in_cursor.y >= 0 && in_cursor.x < int(cursor.z) && in_cursor.y < int(cursor.w)\n"
	"        && texelFetch(ui_cursor, in_cursor, 0).r > 0.5 / 255.0) discard;\n"
	"    vec2 size = vec2(textureSize(text_atlas, 0));\n"
	"    vec2 t = var_texcoord0 * size - 0.5;\n"
	"    ivec2 b = ivec2(floor(t));\n"
	"    vec2 f = t - floor(t);\n"
	"    ivec2 hi = ivec2(size) - 1;\n"
	"    float a00 = texelFetch(text_atlas, clamp(b, ivec2(0), hi), 0).r;\n"
	"    float a10 = texelFetch(text_atlas, clamp(b + ivec2(1, 0), ivec2(0), hi), 0).r;\n"
	"    float a01 = texelFetch(text_atlas, clamp(b + ivec2(0, 1), ivec2(0), hi), 0).r;\n"
	"    float a11 = texelFetch(text_atlas, clamp(b + ivec2(1, 1), ivec2(0), hi), 0).r;\n"
	"    float a = mix(mix(a00, a10, f.x), mix(a01, a11, f.x), f.y);\n"
	"    if (a <= 0.0) discard;\n"
	"    out_fragColor = vec4(texelFetch(palette, ivec2(int(var_colour + 0.5), 0), 0).rgb, a);\n"
	"}\n";

/* --- the HD world (ADR 0013, stage R3): StarCraft: Remastered's art into an RGBA target of the logical screen times
 * the art's scale, premultiplied colour; the interface's canvas over it in the composite pass --- */

/* The terrain in RGBA: each logical pixel's megatile; the HD art's texel from the atlas of the map's megatiles (the map's
 * slot table; a 2D array, slot s at column s % columns, row s / columns % rows of layer s / (columns * rows)), or with
 * art.x (stage R5) the original's megatile sheet's index in the palette's colours, through its dark row with art.z
 * (the original's fog, per fragment); creep's edge over a tile (the terrain map's g: KP_GFX_TERRAIN_FS)
 * from the HD art's edges (a layer each, the tile's size; a placeholder of another size: none), or the original's
 * sheet's cell art.w on; the target is the logical screen times the scale. Under a
 * water mask (stage R4c; StarCraft: Remastered's .tmsk, 255 water, at the masks' tile size whatever the art) the
 * bottom is seen through a moving surface: two animated normal maps scrolled across the map (the large waves and the
 * detail, each between two of its frames) shift where the tile is read, a little, and brighten the crests; the shift
 * reads land only where its own mask is water. */
const char KP_GFX_HD_TERRAIN_FS[] =
	"uniform highp usampler2D hd_map;\n"
	"uniform highp sampler2DArray hd_sheet;\n"
	"uniform highp sampler2DArray hd_water;\n"
	"uniform highp sampler2DArray hd_normal_a;\n"
	"uniform highp sampler2DArray hd_normal_b;\n"
	"uniform highp usampler2D terrain_map;\n"
	"uniform highp sampler2D terrain_sheet;\n"
	"uniform highp sampler2D palette;\n"
	"uniform highp sampler2DArray hd_edges;\n"
	FOG_LEVELS
	"uniform vec4 terrain;      // xy = the view's top-left map pixel, z = the logical screen's height\n"
	"uniform vec4 hd_atlas;     // x = the atlas's slots a row, y = its slot rows a layer\n"
	"uniform vec4 terrain_size; // xy = the map's size in tiles, z = an HD tile's size (the masks'), w = the scale\n"
	"uniform vec4 water;        // x = seconds, y = 1 with water, zw = the frames of the two normal maps\n"
	"uniform vec4 art;          // x = 1: the original's sheet (y: its megatiles a row, w: its first creep edge's cell),\n"
	"                           // z = 1: its fog per fragment\n"
	"out vec4 out_fragColor;\n"
	"const float WAVE_TILE = 96.0;    // logical pixels a large wave's map repeats over\n"
	"const float DETAIL_TILE = 48.0;  // and the detail's\n"
	"const float WAVE_FPS = 6.0;\n"
	"const float DETAIL_FPS = 10.0;\n"
	"const float SHIFT = 2.5;         // logical pixels the surface moves the bottom at most\n"
	"const float GLINT = 0.10;\n"
	"const int TILE = " S(KP_WORLD_TILE) ";\n"
	"const float TILE_F = float(TILE);\n"
	"// the ground's colour at map point m (logical pixels from the map's top-left) and its water mask in .a; fogged as at\n"
	"// the map pixel f\n"
	"vec4 ground(vec2 m, ivec2 f)\n"
	"{\n"
	"    ivec2 cell = clamp(ivec2(floor(m / TILE_F)), ivec2(0), ivec2(terrain_size.xy) - 1);\n"
	"    int slot = int(texelFetch(hd_map, cell, 0).r);\n"
	"    int tile = int(terrain_size.z), cols = int(hd_atlas.x), per_layer = cols * int(hd_atlas.y);\n"
	"    ivec2 t = clamp(ivec2(floor((m - vec2(cell) * TILE_F) * terrain_size.z / TILE_F)), ivec2(0), ivec2(tile - 1));\n"
	"    ivec3 at = ivec3((slot % cols) * tile + t.x, (slot % per_layer / cols) * tile + t.y, slot / per_layer);\n"
	"    float wet = water.y > 0.5 ? texelFetch(hd_water, at, 0).r : 0.0;\n"
	"    uvec2 c = texelFetch(terrain_map, cell, 0).rg;\n"
	"    if (art.x > 0.5) { // the original's megatile sheet\n"
	"        ivec2 p = clamp(ivec2(floor(m)) - cell * TILE, ivec2(0), ivec2(TILE - 1));\n"
	"        int id = int(c.r & 0x7fffu), sheet_cols = int(art.y);\n"
	"        int index = fog_byte(terrain_sheet, ivec2((id % sheet_cols) * TILE + p.x, (id / sheet_cols) * TILE + p.y));\n"
	"        if (c.g != 0u) {\n"
	"            int e = int(art.w) + int(c.g) - 1;\n"
	"            int edge = fog_byte(terrain_sheet, ivec2((e % sheet_cols) * TILE + p.x, (e / sheet_cols) * TILE + p.y));\n"
	"            if (edge != 0) {\n"
	"                index = edge;\n"
	"                wet = 0.0;\n"
	"            }\n"
	"        }\n"
	"        if (art.z > 0.5) index = fogged(index, f);\n"
	"        return vec4(texelFetch(palette, ivec2(index, 0), 0).rgb, wet);\n"
	"    }\n"
	"    vec3 rgb = texelFetch(hd_sheet, at, 0).rgb;\n"
	"    ivec3 edges = textureSize(hd_edges, 0);\n"
	"    if (c.g != 0u && int(c.g) <= edges.z && edges.x == tile) {\n"
	"        vec4 edge = texelFetch(hd_edges, ivec3(t, int(c.g) - 1), 0);\n"
	"        rgb = mix(rgb, edge.rgb, edge.a);\n"
	"        wet *= 1.0 - edge.a;\n"
	"    }\n"
	"    return vec4(rgb, wet);\n"
	"}\n"
	"// an animated normal map's normal at uv, between two of its frames\n"
	"vec3 wave(highp sampler2DArray set, float frames, float fps, vec2 uv)\n"
	"{\n"
	"    float f = mod(water.x * fps, frames), f0 = floor(f);\n"
	"    vec3 a = texture(set, vec3(uv, f0)).rgb, b = texture(set, vec3(uv, mod(f0 + 1.0, frames))).rgb;\n"
	"    return mix(a, b, f - f0) * 2.0 - 1.0;\n"
	"}\n"
	"void main()\n"
	"{\n"
	"    float s = terrain_size.w;\n"
	"    vec2 logical = vec2(gl_FragCoord.x / s, terrain.z - gl_FragCoord.y / s); // from the top\n"
	"    vec2 m = terrain.xy + logical;\n"
	"    if (m.x < 0.0 || m.y < 0.0 || m.x >= terrain_size.x * TILE_F || m.y >= terrain_size.y * TILE_F) {\n"
	"        out_fragColor = vec4(0.0, 0.0, 0.0, 1.0);\n"
	"        return;\n"
	"    }\n"
	"    ivec2 f = fog_map(s); // as the fog's pass reads it\n"
	"    vec4 g = ground(m, f);\n"
	"    if (g.a <= 0.0) {\n"
	"        out_fragColor = vec4(g.rgb, 1.0);\n"
	"        return;\n"
	"    }\n"
	"    float t = water.x;\n"
	"    vec2 n = wave(hd_normal_a, water.z, WAVE_FPS, m / WAVE_TILE + vec2(0.010, 0.016) * t).xy\n"
	"        + 0.6 * wave(hd_normal_b, water.w, DETAIL_FPS, m / DETAIL_TILE - vec2(0.028, 0.022) * t).xy;\n"
	"    vec4 moved = ground(m + n * SHIFT, f);\n"
	"    vec3 c = mix(g.rgb, moved.rgb, min(g.a, moved.a));\n"
	"    c += g.a * GLINT * clamp(n.x + n.y, 0.0, 1.0) * (art.z > 0.5 ? fog_light(f) : 1.0);\n"
	"    out_fragColor = vec4(c, 1.0);\n"
	"}\n";

/* The world images in HD: the same slots, image data and ranks as KP_GFX_WORLD_VS, their frames from the HD frame
 * table (hd_frames[2 f] = (x, y, w, h) in its page, hd_frames[2 f + 1] = (page, dx, dy, scale): the HD frame's centre
 * less the SD frame's, in half HD pixels, y down) and their page's slot (hd_slots); a frame without HD art or whose
 * page is not resident is the SD frame at the logical scale, and so is every frame with hd.w (the original's art in RGBA,
 * stage R5). A virtual frame (a health bar) keeps its logical size. window.w: 0 the overwriting family, 2 every
 * destination layer at once. The family's images are drawn in rank order (they blend: a translucent texel drawn before
 * an image under it would leave its rank in the depth and cut that image out). */
const char KP_GFX_HD_WORLD_VS[] =
	WORLD_VS_COMMON
	"uniform highp sampler2D hd_frames;\n"
	"uniform highp sampler2D hd_slots;    // per HD page its slot, -1 when not resident\n"
	"uniform vec4 hd;        // x = the target's pixels a logical pixel, w = 1: the original's art\n"
	"out highp vec2 var_local;       // the frame pixel as drawn: HD pixels (logical ones in a virtual frame)\n"
	"flat out highp vec4 var_image;  // x = slot (an SD frame: its atlas page), y = row, z = rank, w = kind\n"
	"flat out highp vec4 var_frame;  // xy = size, z = 1 for a virtual frame, w = 1 for an SD frame\n"
	"flat out highp vec4 var_origin; // xy = the frame's top-left in its page, z = -1 when flipped (else 1)\n"
	"const int SLOT_TABLE_WIDTH = " S(KP_HD_SLOT_TABLE_WIDTH) ";\n"
	"void main()\n"
	"{\n"
	WORLD_VS_RANKED_SLOT
	"    vec4 rect = vec4(0.0), extra = vec4(0.0);\n"
	"    if (hd.w < 0.5) {\n"
	"        rect = fetch(hd_frames, frame * 2, " S(KP_FRAME_TABLE_WIDTH) ");\n"
	"        extra = fetch(hd_frames, frame * 2 + 1, " S(KP_FRAME_TABLE_WIDTH) ");\n"
	"    }\n"
	"    float s = hd.x;\n"
	"    float layer = rect.z > 0.0 ? fetch(hd_slots, int(extra.x + 0.5), SLOT_TABLE_WIDTH).r : -1.0;\n"
	"    bool sd = layer < 0.0; // no HD art, or its page not resident: the SD frame, at the logical scale\n"
	"    vec2 size_hd = rect.zw;\n"
	"    vec2 size = size_hd / s;\n"
	"    vec2 shift = vec2(flip ? -extra.y : extra.y, -extra.z) / (2.0 * s);\n"
	"    if (sd) {\n"
	"        rect = fetch(frame_table, frame * 2, " S(KP_FRAME_TABLE_WIDTH) ");\n"
	"        layer = fetch(frame_table, frame * 2 + 1, " S(KP_FRAME_TABLE_WIDTH) ").x;\n"
	"        size_hd = size = rect.zw;\n"
	"        shift = vec2(0.0);\n"
	"    }\n"
	"    bool virtual_frame = window.w < 0.5 && d.w > 0.5;\n"
	"    if (virtual_frame) {\n"
	"        size = vec2(floor(d.w / 256.0), mod(d.w, 256.0));\n"
	"        size_hd = size;\n"
	"        shift = vec2(0.0);\n"
	"    }\n"
	"    float rank = d.y;\n"
	"    if (rank < window.x || rank >= window.y) size = vec2(0.0);\n"
	"    gl_Position = clip(centre(pos) + shift + (vec2(cx, cy) - 0.5) * size, rank);\n"
	"    float fx = flip ? 1.0 - cx : cx;\n"
	"    var_local = vec2(fx, 1.0 - cy) * size_hd;\n"
	"    var_image = vec4(layer, d.z, rank, float(kind));\n"
	"    var_frame = vec4(size_hd, virtual_frame ? 1.0 : 0.0, sd ? 1.0 : 0.0);\n"
	"    var_origin = vec4(rect.xy, flip ? -1.0 : 1.0, 0.0);\n"
	"}\n";

/* The world images' colour in HD, premultiplied (a colour with alpha 0 adds). hd.y: 0 the overwriting family (the
 * sprite with its owner's team colour, StarCraft: Remastered's mix(c, c * team, mask); the hallucination row in its
 * hallucination colours; a sprite of a lit slot, below light.w, in its light: lit(); a health bar from the bars in
 * the palette), 1 the destination family (a shadow, the shadow
 * row's, darkens by half where its alpha reaches a half, a blend bank's glow adds, the trans50 and cloak blends at half), 2 the distortion family (a cloaked unit's
 * faint brightness). hd.z = the shadow's table code; hd_colors: per remap row its team colour (alpha 0: the
 * hallucination row; KP_GFX_HD_SOLID_ALPHA: the colour alone at the art's alpha, a selection circle). An SD frame (stage R5: every frame with hd.w) in the palette's colours, alike: an overwriting
 * image's (its remap row's), a size outline's and a health bar's, through their dark rows with art.z (the original's
 * fog, per fragment; the blends' colours darkened by its light). */
const char KP_GFX_HD_WORLD_FS[] =
	"uniform highp sampler2DArray hd_diffuse;\n"
	"uniform highp sampler2DArray atlas;\n"
	"uniform highp sampler2D remap_table;\n"
	"uniform highp sampler2DArray hd_team;\n"
	"uniform highp sampler2D hd_colors;\n"
	"uniform highp sampler2D bars;\n"
	"uniform highp sampler2D palette;\n"
	"uniform highp sampler2DArray hd_light; // a lit slot's normal map and occlusion (BC3), two layers a slot\n"
	"uniform highp sampler2DArray hd_glow;  // and its specular and emissive colours (BC1)\n"
	FOG_LEVELS
	"uniform vec4 hd;\n"
	"uniform vec4 art; // z = 1: the original's fog per fragment\n"
	"uniform vec4 light; // xyz = the half vector of the light and the eye, w = the lit slots (0: none)\n"
	"in highp vec2 var_local;\n"
	"flat in highp vec4 var_image;\n"
	"flat in highp vec4 var_frame;\n"
	"flat in highp vec4 var_origin;\n"
	"out vec4 out_fragColor;\n"
	"const int KIND_OUTLINE = 2;\n"
	"const int KIND_BAR = 3;\n"
	"const int KIND_TABLE = 0;\n"
	"const int BAR_ROWS = " S(KP_BAR_ROWS) ";\n"
	"const vec3 LUMA = vec3(0.30196078, 0.59215686, 0.10980392);\n"
	"const float SOLID = " S(KP_GFX_HD_SOLID_ALPHA) ".0 / 255.0;\n"
	"const vec3 LIGHT_FROM = normalize(vec3(" S(KP_HD_LIGHT_X) ".0, " S(KP_HD_LIGHT_Y) ".0, " S(KP_HD_LIGHT_Z) ".0));\n"
	"const float SHINE = " S(KP_HD_SHINE) ".0;\n"
	"const float LIGHT_AMOUNT = " S(KP_HD_LIGHT_AMOUNT) ";\n"
	"vec3 page_uv()\n"
	"{\n"
	"    vec2 size = vec2(textureSize(hd_diffuse, 0).xy);\n"
	"    vec2 at = var_origin.xy + clamp(var_local, vec2(0.5), var_frame.xy - 0.5);\n"
	"    return vec3(at / size, var_image.x);\n"
	"}\n"
	"// a lit page's sprite colour in its light: the normal map's x (mirrored with the frame) and y, its occlusion, the\n"
	"// white light's diffuse and highlight, the emissive colour over them\n"
	"vec3 lit(vec3 colour, vec3 uv)\n"
	"{\n"
	"    vec3 at = vec3(uv.xy, 2.0 * uv.z);\n"
	"    vec4 n4 = texture(hd_light, at);\n"
	"    float occlusion = texture(hd_light, at + vec3(0.0, 0.0, 1.0)).g;\n"
	"    vec3 specular = texture(hd_glow, at).rgb, glow = texture(hd_glow, at + vec3(0.0, 0.0, 1.0)).rgb;\n"
	"    vec2 xy = vec2(n4.r, n4.a) * 2.0 - 1.0;\n"
	"    xy.x *= var_origin.z;\n"
	"    vec3 n = normalize(vec3(xy, sqrt(max(1.0 - dot(xy, xy), 0.1))));\n"
	"    float lambert = clamp(dot(LIGHT_FROM, n), 0.0, 1.0);\n"
	"    float shine = lambert > 0.0 ? pow(clamp(dot(light.xyz, n), 0.0, 1.0), SHINE) : 0.0;\n"
	"    return (lambert * colour + shine * specular) * occlusion + glow;\n"
	"}\n"
	"void main()\n"
	"{\n"
	"    int family = int(hd.y + 0.5);\n"
	"    int kind = int(var_image.w + 0.5);\n"
	"    int row = int(var_image.y + 0.5);\n"
	"    bool dark = art.z > 0.5;\n"
	"    ivec2 map = fog_map(hd.x);\n"
	"    if (family == 0 && kind == KIND_BAR) {\n"
	"        ivec2 p = ivec2(min(floor(var_local), var_frame.xy - 1.0));\n"
	"        vec4 t = texelFetch(bars, ivec2(p.x, row * BAR_ROWS + p.y), 0);\n"
	"        if (t.g < 0.5) discard;\n"
	"        int index = int(t.r * 255.0 + 0.5);\n"
	"        out_fragColor = vec4(texelFetch(palette, ivec2(dark ? fogged(index, map) : index, 0), 0).rgb, 1.0);\n"
	"        return;\n"
	"    }\n"
	"    if (family == 0 && kind == KIND_OUTLINE && hd.w > 0.5) { // the original's size outline: its frame's edge\n"
	"        vec2 p = floor(var_local);\n"
	"        if (p.x > 0.5 && p.y > 0.5 && p.x < var_frame.x - 1.5 && p.y < var_frame.y - 1.5) discard;\n"
	"        out_fragColor = vec4(texelFetch(palette, ivec2(dark ? fogged(row, map) : row, 0), 0).rgb, 1.0);\n"
	"        return;\n"
	"    }\n"
	"    // the kinds number per family: only the overwriting family's 2 is an outline (the distortion family's 2 is the cloak)\n"
	"    if ((family == 0 && kind == KIND_OUTLINE) || var_frame.z > 0.5) discard;\n"
	"    if (var_frame.w > 0.5) {\n"
	"        // the SD frame (the original's art, or its HD page not resident yet) in its palette colours: an overwriting\n"
	"        // image, a shadow darkening by half, a glow adding, a blend at half, a distortion's faint brightness\n"
	"        ivec2 p = ivec2(min(floor(var_local), var_frame.xy - 1.0));\n"
	"        int index = int(texelFetch(atlas, ivec3(ivec2(var_origin.xy) + p, int(var_image.x + 0.5)), 0).r * 255.0 + 0.5);\n"
	"        if (index == 0) discard;\n"
	"        if (family == 0) {\n"
	"            if (kind == 0) index = int(texelFetch(remap_table, ivec2(index, row), 0).r * 255.0 + 0.5);\n"
	"            if (dark) index = fogged(index, map);\n"
	"            out_fragColor = vec4(texelFetch(palette, ivec2(index, 0), 0).rgb, 1.0);\n"
	"            return;\n"
	"        }\n"
	"        if (family == 1 && kind == KIND_TABLE && abs(float(row) - hd.z) < 0.5) {\n"
	"            out_fragColor = vec4(0.0, 0.0, 0.0, 0.5);\n"
	"            return;\n"
	"        }\n"
	"        vec3 c = texelFetch(palette, ivec2(index, 0), 0).rgb * (dark ? fog_light(map) : 1.0);\n"
	"        if (family == 1 && kind == KIND_TABLE) out_fragColor = vec4(c, 0.0);\n"
	"        else if (family == 1) out_fragColor = vec4(c * 0.5, 0.5);\n"
	"        else out_fragColor = vec4(vec3(dot(c, LUMA)) * 0.25, 0.25);\n"
	"        return;\n"
	"    }\n"
	"    vec3 uv = page_uv();\n"
	"    vec4 c = texture(hd_diffuse, uv);\n"
	"    if (c.a < 0.004) discard;\n"
	"    if (family == 0) {\n"
	"        vec4 team = kind == 0 ? texelFetch(hd_colors, ivec2(row, 0), 0) : vec4(1.0);\n"
	"        vec3 rgb = mix(c.rgb, c.rgb * team.rgb, texture(hd_team, uv).r);\n"
	"        if (abs(team.a - SOLID) < 0.25) rgb = team.rgb;\n"
	"        else if (team.a < 0.5) rgb = vec3(0.75, 1.125, 2.65) * dot(rgb, LUMA);\n"
	"        bool shaded = var_image.x < light.w - 0.5 && abs(team.a - SOLID) >= 0.25; // a lit slot's, not a circle\n"
	"        if (shaded) rgb = mix(rgb, lit(rgb, uv), LIGHT_AMOUNT);\n"
	"        out_fragColor = vec4(rgb * c.a, c.a);\n"
	"    } else if (family == 1) {\n"
	"        // the shadow row darkens; a blend bank's glow (a weld's spark, a building's lights) adds, its dark texels\n"
	"        // nothing (taken for a shadow they darkened the ground in a box around it)\n"
	"        bool shadow = abs(float(row) - hd.z) < 0.5;\n"
	"        if (kind == KIND_TABLE && shadow) out_fragColor = vec4(0.0, 0.0, 0.0, 0.5 * min(1.0, 2.0 * c.a));\n"
	"        else if (kind == KIND_TABLE) out_fragColor = vec4(c.rgb * c.a, 0.0);\n"
	"        else out_fragColor = vec4(c.rgb * c.a * 0.5, c.a * 0.5);\n"
	"    } else {\n"
	"        float a = 0.25 * c.a;\n"
	"        out_fragColor = vec4(vec3(dot(c.rgb, LUMA)) * a, a);\n"
	"    }\n"
	"}\n";

/* The fog of war over the RGBA world (the HD art's, or the original's with the smooth fog): the level KP_GFX_FOG_FS
 * interpolates for a logical pixel darkens it, 0 black, 31 clear (StarCraft: Remastered's smooth fog instead of
 * dark.pcx's rows). */
const char KP_GFX_HD_FOG_FS[] =
	FOG_LEVELS
	"uniform vec4 hd;         // x = the target's pixels a logical pixel\n"
	"out vec4 out_fragColor;\n"
	"void main()\n"
	"{\n"
	"    bool even;\n"
	"    int level = fog_level(fog_map(hd.x), even);\n"
	"    if (level >= SEEN) discard;\n"
	"    out_fragColor = vec4(0.0, 0.0, 0.0, 1.0 - float(level) / float(SEEN));\n"
	"}\n";

/* The window's picture in HD: the HD world in the palette's tone (its fades and the brightness option, grey when the
 * palette is), the interface's canvas (as KP_GFX_UI_FS reads it: opaque texels in their palette colour, translucent
 * ones over the world at half) and the cursor over it, at the window's resolution. */
const char KP_GFX_HD_COMPOSITE_FS[] =
	"in vec2 var_texcoord0;\n"
	"uniform highp sampler2D hd_target;\n"
	"uniform highp sampler2D ui_canvas;\n"
	"uniform highp sampler2D ui_cursor;\n"
	"uniform highp sampler2D palette;\n"
	"uniform highp sampler2D hd_tone; // per level each channel's tone (kp_palette_tone); .a of level 0: grey\n"
	"uniform highp usampler2D ui_ids;      // per canvas texel its HD item (0 none)\n"
	"uniform highp usampler2D ui_below;    // and what lay under its SD pixel (KpUiCanvas.below)\n"
	"uniform highp sampler2D ui_items;     // per item: its image's rectangle, its atlas texels, layer and tint\n"
	"uniform highp sampler2DArray ui_atlas;\n"
	"uniform vec4 cursor;      // xy = the frame's top-left on the logical screen, zw = its size (0: none)\n"
	"uniform vec4 source_size; // xy = the logical screen, z = 1 with a canvas, w = 1 with HD items\n"
	"uniform vec4 hd_cursor_rect;  // the cursor's HD frame: its rectangle in the window's pixels (from the bottom-left)\n"
	"uniform vec4 hd_cursor_src;   // its atlas texels\n"
	"uniform vec4 hd_cursor_layer; // x = its layer, y = 1: the pass of the cursor alone\n"
	"uniform vec4 ui_gamma;                      // x = the gamma option's exponent (the palette's colours have it)\n"
	"out vec4 out_fragColor;\n"
	"const vec3 LUMA = vec3(0.30196078, 0.59215686, 0.10980392);\n"
	"const float OVERLAY_A = " S(KP_UI_CANVAS_OVERLAY_A) ".0 / 255.0; // the game view's layer (kp_ui.h)\n"
	"vec3 gamma(vec3 c) { return pow(c, vec3(ui_gamma.x)); }\n"
	"vec3 colour(float index) { return texelFetch(palette, ivec2(int(index * 255.0 + 0.5), 0), 0).rgb; }\n"
	"float tone(float v, int c) { return texelFetch(hd_tone, ivec2(int(v * 255.0 + 0.5), 0), 0)[c]; }\n"
	"const int ITEMS_PER_ROW = " S(KP_UI_ITEMS_PER_ROW) ", ITEM_TEXELS = " S(KP_GFX_UI_ITEM_FLOATS) " / 4;\n"
	"// an item's texel n (0 .. ITEM_TEXELS - 1)\n"
	"vec4 item_texel(int id, int n)\n"
	"{\n"
	"    return texelFetch(ui_items, ivec2((id % ITEMS_PER_ROW) * ITEM_TEXELS + n, id / ITEMS_PER_ROW), 0);\n"
	"}\n"
	"// an HD item's colour and alpha at the logical point p (from the top); .a < 0: outside its image\n"
	"vec4 item(int id, vec2 p)\n"
	"{\n"
	"    vec4 r = item_texel(id, 0), s = item_texel(id, 1), m = item_texel(id, 2), q = item_texel(id, 3);\n"
	"    if (m.y > -1.5 && q.y > 0.0) { // tiles: p in the first one's SD frame\n"
	"        vec2 origin = item_texel(id, 4).xy;\n"
	"        p = origin + mod(p - origin, q.yz);\n"
	"    }\n"
	"    vec2 t = (p - r.xy) / r.zw;\n"
	"    if (t.x < 0.0 || t.y < 0.0 || t.x > 1.0 || t.y > 1.0) return vec4(0.0, 0.0, 0.0, -1.0);\n"
	"    vec2 at = clamp(mix(s.xy, s.zw, t), s.xy + 0.5, s.zw - 0.5);\n"
	"    vec2 size = vec2(textureSize(ui_atlas, 0).xy);\n"
	"    float layer = m.x < 0.0 ? -1.0 - m.x : m.x;\n"
	"    if (m.y < -1.5) { // a wireframe: its shields, its outline, then its hit point parts over them, each in its colour\n"
	"        vec4 w = vec4(0.0);\n"
	"        const int PARTS = " S(KP_UI_WIRE_PARTS) ", HIT_POINTS = " S(KP_UI_WIRE_HIT_POINTS) ";\n"
	"        for (int n = 0; n < PARTS; n++) {\n"
	"            int k = n < PARTS - HIT_POINTS ? n + HIT_POINTS : n - (PARTS - HIT_POINTS);\n"
	"            float word = k < 3 ? q.y : k < 6 ? q.z : q.w;\n"
	"            int index = int(mod(floor(word / exp2(8.0 * float(k % 3))), 256.0));\n"
	"            vec4 l = texture(ui_atlas, vec3((at + vec2(float(k) * (s.z - s.x), 0.0)) / size, layer));\n"
	"            vec3 part = texelFetch(palette, ivec2(index, 0), 0).rgb;\n"
	"            w = vec4(mix(w.rgb, l.rgb * part, l.a), w.a + (1.0 - w.a) * l.a);\n"
	"        }\n"
	"        return w;\n"
	"    }\n"
	"    vec4 hd = texture(ui_atlas, vec3(at / size, layer));\n"
	"    return vec4(m.y >= 0.0 ? min(hd.rgb * m.yzw, vec3(1.0)) : gamma(hd.rgb), hd.a);\n"
	"}\n"
	"void main()\n"
	"{\n"
	"    if (hd_cursor_layer.y > 0.5) { // the cursor's HD frame alone, blended over the picture and its text\n"
	"        vec2 t = (gl_FragCoord.xy - hd_cursor_rect.xy) / hd_cursor_rect.zw;\n"
	"        t.y = 1.0 - t.y; // from the top\n"
	"        if (t.x < 0.0 || t.y < 0.0 || t.x > 1.0 || t.y > 1.0) discard;\n"
	"        vec2 at = clamp(mix(hd_cursor_src.xy, hd_cursor_src.zw, t), hd_cursor_src.xy + 0.5, hd_cursor_src.zw - 0.5);\n"
	"        vec4 hd = texture(ui_atlas, vec3(at / vec2(textureSize(ui_atlas, 0).xy), hd_cursor_layer.x));\n"
	"        out_fragColor = vec4(gamma(hd.rgb), hd.a);\n"
	"        return;\n"
	"    }\n"
	"    vec3 world = texture(hd_target, var_texcoord0).rgb;\n"
	"    if (texelFetch(hd_tone, ivec2(0), 0).a > 0.5) world = vec3(dot(world, LUMA));\n"
	"    world = vec3(tone(world.r, 0), tone(world.g, 1), tone(world.b, 2));\n"
	"    ivec2 at = clamp(ivec2(floor(var_texcoord0 * source_size.xy)), ivec2(0), ivec2(source_size.xy) - 1);\n"
	"    ivec2 from_top = ivec2(at.x, int(source_size.y) - 1 - at.y);\n"
	"    ivec2 in_cursor = from_top - ivec2(cursor.xy);\n"
	"    if (in_cursor.x >= 0 && in_cursor.y >= 0 && in_cursor.x < int(cursor.z) && in_cursor.y < int(cursor.w)) {\n"
	"        float c = texelFetch(ui_cursor, in_cursor, 0).r;\n"
	"        if (c > 0.5 / 255.0) {\n"
	"            out_fragColor = vec4(colour(c), 1.0);\n"
	"            return;\n"
	"        }\n"
	"    }\n"
	"    if (source_size.z > 0.5) {\n"
	"        vec4 texel = texelFetch(ui_canvas, from_top, 0);\n"
	"        vec3 c = colour(texel.r);\n"
	"        bool replaced = false;\n"
	"        if (source_size.w > 0.5 && texel.g >= 0.25) {\n"
	"            uint id = texelFetch(ui_ids, from_top, 0).r;\n"
	"            if (id > 0u) {\n"
	"                vec2 p = vec2(var_texcoord0.x, 1.0 - var_texcoord0.y) * source_size.xy;\n"
	"                vec4 hd = item(int(id), p);\n"
	"                int under = int(item_texel(int(id), 3).x + 0.5);\n"
	"                replaced = item_texel(int(id), 2).x < 0.0;\n"
	"                vec4 u = under > 0 ? item(under, p) : vec4(0.0, 0.0, 0.0, -1.0);\n"
	"                // the HD art drawn over what lay under the SD picture (the item's, or the item under it's), as\n"
	"                // Remastered draws it: its translucent texels over the background, not over the picture's own pixel\n"
	"                uint below = texelFetch(ui_below, from_top, 0).r;\n"
	"                bool art = (below & " S(KP_UI_BELOW_UNDER) "u) != 0u ? u.a >= 0.0 : hd.a >= 0.0;\n"
	"                if (!replaced && art && (below & " S(KP_UI_BELOW_KNOWN) "u) != 0u) c = colour(float(below & 255u) / 255.0);\n"
	"                if (under > 0) { // the item it lies over (an icon's button) first\n"
	"                    int deeper = int(item_texel(under, 3).x + 0.5);\n"
	"                    bool piece = !replaced && item_texel(under, 2).x < 0.0;\n"
	"                    // a console piece under it, or under the item it lies over (an icon's box over the console):\n"
	"                    // the piece's silhouette over the world in place of the SD pixel, as the console shows it\n"
	"                    if (!replaced && !piece && deeper > 0 && item_texel(deeper, 2).x < 0.0) {\n"
	"                        vec4 d = item(deeper, p);\n"
	"                        if (d.a >= 0.0) c = mix(world, d.rgb, d.a);\n"
	"                    }\n"
	"                    if (u.a >= 0.0) c = mix(piece ? world : c, u.rgb, u.a);\n"
	"                }\n"
	"                // the HD art's own silhouette, over the game view's layer the console left (the canvas's overlay)\n"
	"                if (replaced && abs(texel.g - OVERLAY_A) < 0.5 / 255.0) world = c;\n"
	"                if (replaced) world = mix(world, hd.rgb, max(hd.a, 0.0));\n"
	"                else if (hd.a >= 0.0) c = mix(c, hd.rgb, hd.a);\n"
	"            }\n"
	"        }\n"
	"        if (!replaced && texel.g >= 0.75) world = c;\n"
	"        else if (!replaced && texel.g >= 0.25) world = mix(world, c, 0.5);\n"
	"    }\n"
	"    out_fragColor = vec4(world, 1.0);\n"
	"}\n";

const char KP_GFX_ES_HEADER[] = "#version 300 es\nprecision highp float;\nprecision highp int;\n"
	"precision highp sampler2D;\nprecision highp sampler2DArray;\nprecision highp usampler2D;\n";

const KpGfxProgramSource KP_GFX_PROGRAMS[KP_PROGRAMS] = {
	{ KP_PROG_TERRAIN, KP_GFX_FULLSCREEN_VS, KP_GFX_TERRAIN_FS },
	{ KP_PROG_IMAGES, KP_GFX_WORLD_VS, KP_GFX_WORLD_FS },
	{ KP_PROG_DEST, KP_GFX_WORLD_VS, KP_GFX_DEST_FS },
	{ KP_PROG_DISTORT, KP_GFX_WORLD_VS, KP_GFX_DISTORT_FS },
	{ KP_PROG_FOG, KP_GFX_FULLSCREEN_VS, KP_GFX_FOG_FS },
	{ KP_PROG_UI, KP_GFX_FULLSCREEN_VS, KP_GFX_UI_FS },
	{ KP_PROG_PALETTE, KP_GFX_FULLSCREEN_VS, KP_GFX_PALETTE_FS },
	{ KP_PROG_SHARP, KP_GFX_FULLSCREEN_VS, KP_GFX_SHARP_FS },
	{ KP_PROG_TEXT, KP_GFX_TEXT_VS, KP_GFX_TEXT_FS },
	{ KP_PROG_HD_TERRAIN, KP_GFX_FULLSCREEN_VS, KP_GFX_HD_TERRAIN_FS },
	{ KP_PROG_HD_IMAGES, KP_GFX_HD_WORLD_VS, KP_GFX_HD_WORLD_FS },
	{ KP_PROG_HD_FOG, KP_GFX_FULLSCREEN_VS, KP_GFX_HD_FOG_FS },
	{ KP_PROG_HD_COMPOSITE, KP_GFX_FULLSCREEN_VS, KP_GFX_HD_COMPOSITE_FS },
	{ KP_PROG_VIDEO, KP_GFX_FULLSCREEN_VS, KP_GFX_VIDEO_FS },
};
