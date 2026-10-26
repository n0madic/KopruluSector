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

/* The terrain into the index target: every texel's map pixel, its cell's megatile in the tile map (R16UI, bit 15 the
 * tile's high bit) and the megatile sheet's index there (every terrain texel is opaque, index 0 included); cells
 * outside the map stay cleared. */
const char KP_GFX_TERRAIN_FS[] =
	"uniform highp usampler2D terrain_map;\n"
	"uniform highp sampler2D terrain_sheet;\n"
	"uniform vec4 terrain;      // xy = the view's top-left map pixel, z = the target's height, w = the sheet's columns\n"
	"uniform vec4 terrain_size; // xy = the map's size in tiles\n"
	"out vec4 out_fragColor;\n"
	"void main()\n"
	"{\n"
	"    ivec2 t = ivec2(gl_FragCoord.xy);\n"
	"    ivec2 m = ivec2(int(terrain.x) + t.x, int(terrain.y) + int(terrain.z) - 1 - t.y);\n"
	"    ivec2 cell = m / 32;\n"
	"    if (m.x < 0 || m.y < 0 || cell.x >= int(terrain_size.x) || cell.y >= int(terrain_size.y)) discard;\n"
	"    int id = int(texelFetch(terrain_map, cell, 0).r & 0x7fffu);\n"
	"    int cols = int(terrain.w);\n"
	"    ivec2 s = ivec2((id % cols) * 32 + m.x % 32, (id / cols) * 32 + m.y % 32);\n"
	"    out_fragColor = vec4(texelFetch(terrain_sheet, s, 0).r, 0.0, 0.0, 1.0);\n"
	"}\n";

/* World images: six vertices per slot of the family's range; per-image data in one RGBA32F
 * texture (image_data[2 slot] = (x, y, x - previous x, y - previous y) in 1/256 world pixels, image_data[2 slot + 1] =
 * ((frame * KINDS + kind) * 2 + hflip, rank, row, layer)); the frame table (frame_table[2 f] = (x, y, w, h) in its
 * page, frame_table[2 f + 1] = (page, 0, 0, 0)). The rank becomes the depth, so the image last in the native draw
 * order wins whatever its family (depth test GREATER). window = (lowest rank, rank past the last, layer, 1 = only that
 * layer); in the overwriting passes (window.w = 0) a nonzero layer is a virtual frame's size, width * 256 + height. */
const char KP_GFX_WORLD_VS[] =
	"uniform highp sampler2D image_data;\n"
	"uniform highp sampler2D frame_table;\n"
	"uniform vec4 view;      // xy = the world point at the target's bottom-left, zw = the target's size\n"
	"uniform vec4 interp;    // x = alpha between the last two ticks\n"
	"uniform vec4 window;\n"
	"uniform vec4 slot_base; // x = the family's first slot\n"
	"out highp vec2 var_local;            // the frame pixel (source column, row from the top), mirrored as drawn\n"
	"flat out highp vec4 var_image;       // x = page, y = row, z = rank, w = kind\n"
	"flat out highp vec4 var_frame;       // xy = size, z = 1 when mirrored\n"
	"flat out highp vec4 var_origin;      // xy = the frame's top-left in its page\n"
	"flat out highp vec4 var_texture;     // the warp texture frame: x, y, w, h\n"
	"flat out highp float var_texture_page;\n"
	"const int KINDS = " S(KP_DRAW_KINDS) ";\n"
	"const int KIND_WARP = 1;\n"
	"const float DEPTH = 1.0 / " S(KP_RANK_RANGE) ".0;\n"
	"vec4 fetch(highp sampler2D t, int texel, int width)\n"
	"{\n"
	"    return texelFetch(t, ivec2(texel % width, texel / width), 0);\n"
	"}\n"
	"void main()\n"
	"{\n"
	"    int slot = int(slot_base.x + 0.5) + gl_VertexID / 6;\n"
	"    int corner = gl_VertexID % 6;\n"
	"    vec4 pos = fetch(image_data, slot * 2, " S(KP_IMAGE_DATA_WIDTH) ");\n"
	"    vec4 d = fetch(image_data, slot * 2 + 1, " S(KP_IMAGE_DATA_WIDTH) ");\n"
	"    int code = int(d.x + 0.5);\n"
	"    int frame = code / (2 * KINDS);\n"
	"    int kind = (code / 2) % KINDS;\n"
	"    bool flip = (code % 2) == 1;\n"
	"    vec4 rect = fetch(frame_table, frame * 2, " S(KP_FRAME_TABLE_WIDTH) ");\n"
	"    vec4 page = fetch(frame_table, frame * 2 + 1, " S(KP_FRAME_TABLE_WIDTH) ");\n"
	"    vec2 size = rect.zw;\n"
	"    float rank = d.y;\n"
	"    if (window.w < 0.5 && d.w > 0.5) size = vec2(floor(d.w / 256.0), mod(d.w, 256.0));\n"
	"    bool outside = rank < window.x || rank >= window.y || (window.w > 0.5 && abs(d.w - window.z) > 0.5);\n"
	"    if (outside) size = vec2(0.0);\n"
	"    // corners: 0 (0,0) 1 (1,0) 2 (1,1) 3 (0,0) 4 (1,1) 5 (0,1)\n"
	"    float cx = (corner == 1 || corner == 2 || corner == 4) ? 1.0 : 0.0;\n"
	"    float cy = (corner == 2 || corner == 4 || corner == 5) ? 1.0 : 0.0;\n"
	"    vec2 p = (pos.xy - (1.0 - interp.x) * pos.zw) / 256.0 + (vec2(cx, cy) - 0.5) * size;\n"
	"    // clip z = (rank + 1) / RANK_RANGE in (0, 1]; the target is cleared to depth 0\n"
	"    gl_Position = vec4((p - view.xy) / view.zw * 2.0 - 1.0, (rank + 1.0) * DEPTH, 1.0);\n"
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

/* The fog of war over the composed view in index space (DrawFogOverlay 0x46c3c0, the CPU reference kp_fog_view_shade):
 * every 8x8-pixel map block takes the four levels of the level map around its corner; four equal levels 0 make it
 * black, 31 leave it, another level darkens it through that dark.pcx row; unequal ones interpolate per pixel in 8.8
 * fixed point. */
const char KP_GFX_FOG_FS[] =
	"uniform highp sampler2D dest;\n"
	"uniform highp sampler2D fog_levels;\n"
	"uniform highp sampler2D fog_dark;\n"
	"uniform vec4 fog;        // xy = the map pixel of the view's top-left, z = the view's height\n"
	"uniform vec4 fog_origin; // xy = the map block of the level map's first cell, zw = its size in blocks\n"
	"out vec4 out_fragColor;\n"
	"const int SEEN = 31;\n"
	"int byte_at(highp sampler2D t, ivec2 at) { return int(texelFetch(t, at, 0).r * 255.0 + 0.5); }\n"
	"int level_at(ivec2 block)\n"
	"{\n"
	"    ivec2 cell = block - ivec2(fog_origin.xy);\n"
	"    if (cell.x < 0 || cell.y < 0 || cell.x >= int(fog_origin.z) || cell.y >= int(fog_origin.w)) return 0;\n"
	"    return byte_at(fog_levels, cell);\n"
	"}\n"
	"int fogged(int index, ivec2 map)\n"
	"{\n"
	"    ivec2 block = map / 8, k = map - block * 8;\n"
	"    int tl = level_at(block), tr = level_at(block + ivec2(1, 0));\n"
	"    int bl = level_at(block + ivec2(0, 1)), br = level_at(block + ivec2(1, 1));\n"
	"    int level;\n"
	"    if (tl == tr && tl == bl && tl == br) {\n"
	"        if (tl == 0) return 0;\n"
	"        if (tl == SEEN) return index;\n"
	"        level = tl;\n"
	"    } else {\n"
	"        int l = tl * 256 + k.y * (bl - tl) * 32, r = tr * 256 + k.y * (br - tr) * 32;\n"
	"        int step = (l - r) / 8;\n"
	"        level = (r + (7 - k.x) * step) / 256;\n"
	"    }\n"
	"    return byte_at(fog_dark, ivec2(index, level));\n"
	"}\n"
	"void main()\n"
	"{\n"
	"    ivec2 texel = ivec2(gl_FragCoord.xy);\n"
	"    vec4 copy = texelFetch(dest, texel, 0);\n"
	"    int index = int(copy.r * 255.0 + 0.5);\n"
	"    ivec2 map = ivec2(int(fog.x) + texel.x, int(fog.y) + int(fog.z) - 1 - texel.y);\n"
	"    out_fragColor = vec4(float(fogged(index, map)) / 255.0, 0.0, 0.0, 1.0);\n"
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
