#version 140

// Overwriting world images, written into R of the index target (index 0 is transparent,
// ADR 0003). By kind (presentation/draw_list.lua):
//   0 remap (methods 0/1, 13, 14, 16): the atlas index through a source remap row (owner or
//     player colour on 8..15, selection colours on 1..8, shift);
//   1 warp texture (method 12, 0x40e800): where the frame is opaque, the warp texture frame's
//     pixel at the same frame position. The original draws the texture through the sprite
//     owner's colour map; the stock texture frames use no index in 8..15, where that map
//     differs from the identity (tools/viewer_compose_check.py asserts it);
//   2 size outline (method 15, 0x40e9d0): the frame rectangle's edge pixels in colour `row`;
//   3 health bar (method 11): the bar the presentation drew into `bars` (R = index, A = drawn)
//     for slot `row`, BAR_ROWS texture rows per slot (presentation/bars.lua).
#include "/render/materials/world_common.glsl"

in highp vec2 var_texcoord0;
in highp vec2 var_local;
flat in highp vec4 var_image; // x = atlas, y = remap row (texture frame, colour), z = rank, w = kind
flat in highp vec4 var_frame; // x = width, y = height
flat in highp vec4 var_texture_uv;
flat in highp vec4 var_texture_size;

out vec4 out_fragColor;

uniform mediump sampler2D bars;

const int KIND_WARP = 1;
const int KIND_OUTLINE = 2;
const int KIND_BAR = 3;
const int BAR_ROWS = 16;

void main()
{
    int kind = int(var_image.w + 0.5);
    if (kind == KIND_OUTLINE) {
        vec2 p = floor(var_local);
        if (p.x > 0.5 && p.y > 0.5 && p.x < var_frame.x - 1.5 && p.y < var_frame.y - 1.5) discard;
        out_fragColor = vec4(var_image.y / 255.0, rank_channels(var_image.z), 1.0);
        return;
    }
    if (kind == KIND_BAR) {
        ivec2 p = ivec2(floor(var_local));
        vec4 t = texelFetch(bars, ivec2(p.x, int(var_image.y + 0.5) * BAR_ROWS + p.y), 0);
        if (t.a < 0.5) discard;
        out_fragColor = vec4(t.r, rank_channels(var_image.z), 1.0);
        return;
    }
    int index = atlas_index(int(var_image.x + 0.5), var_texcoord0);
    if (index == 0) discard;
    if (kind == KIND_WARP) {
        // Texture pixel at the frame position: (column, row from the top) in the texture frame.
        vec2 p = floor(var_local) + 0.5;
        vec2 size = var_texture_size.xy;
        if (p.x > size.x || p.y > size.y) discard; // past the texture: native scratch, not drawn
        float fx = p.x / size.x;
        float cy = 1.0 - p.y / size.y;
        bool rotated = var_texture_size.z > 0.5;
        vec4 uv = var_texture_uv;
        vec2 t = vec2(mix(uv.x, uv.z, rotated ? cy : fx), mix(uv.w, uv.y, rotated ? fx : cy));
        index = atlas_index(int(var_texture_size.w + 0.5), t);
        out_fragColor = vec4(float(index) / 255.0, rank_channels(var_image.z), 1.0);
        return;
    }
    out_fragColor = vec4(float(remap(index, int(var_image.y + 0.5))) / 255.0, rank_channels(var_image.z), 1.0);
}
