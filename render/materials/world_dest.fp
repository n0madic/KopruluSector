#version 140

// Destination-table world images; dst comes from the copy of the index target made before
// this destination layer. By kind (presentation/draw_list.lua):
//   0 table (methods 9, 10, 17): every opaque texel writes tables[row + (blend ? src : 0)][dst],
//     row = 2 * table row + blend (method 9 blends by the source). Every blend bank spans 256
//     rows (presentation/world_lookups.lua), so row + src stays inside it;
//   1 cloak blend (methods 5/7, 0x40e740): the trans50 blend, then the threshold pass
//     0x406235: where the brightness of the owner-mapped index exceeds the parameter byte
//     (signed compare), the mapped index. row = colour row * 65536 + owner row * 256 +
//     parameter;
//   2 trans50 blend (method 6, method 9 with remapping 5). row = colour row.
// trans50 rows 8..15 hold the colours of the last sprite whose main image draws with a
// method 5..7 (0x416bd0), so trans50 is read at the source mapped through that colour row
// (presentation/draw_list.lua).
in highp vec2 var_texcoord0;
flat in highp vec4 var_image; // x = atlas, y = row code, w = kind

out vec4 out_fragColor;

uniform mediump sampler2D atlas0;
uniform mediump sampler2D atlas1;
uniform mediump sampler2D atlas2;
uniform mediump sampler2D atlas3;
uniform mediump sampler2D atlas4;
uniform mediump sampler2D atlas5;
uniform mediump sampler2D dest;
uniform mediump sampler2D tables;
uniform mediump sampler2D remap_table;

uniform fs_uniforms
{
    highp vec4 lookups; // x = trans50 row code, y = brightness row of remap_table
};

const int KIND_CLOAK_BLEND = 1;
const int KIND_TRANS50 = 2;
const int PARAM_SPAN = 256;
const int COLOR_SPAN = 65536;

float atlas_texel(int atlas, vec2 uv)
{
    if (atlas == 0) return texture(atlas0, uv).r;
    if (atlas == 1) return texture(atlas1, uv).r;
    if (atlas == 2) return texture(atlas2, uv).r;
    if (atlas == 3) return texture(atlas3, uv).r;
    if (atlas == 4) return texture(atlas4, uv).r;
    return texture(atlas5, uv).r;
}

int remap(int index, int row)
{
    return int(texelFetch(remap_table, ivec2(index, row), 0).r * 255.0 + 0.5);
}

int table(int code, int src, int dst)
{
    int row = code / 2 + (code % 2 == 1 ? src : 0);
    return int(texelFetch(tables, ivec2(dst, row), 0).r * 255.0 + 0.5);
}

// Signed BYTE compare of the threshold kernels: parameter < brightness.
bool above_threshold(int brightness, int param)
{
    return (param >= 128 ? param - 256 : param) < brightness;
}

void main()
{
    int src = int(atlas_texel(int(var_image.x + 0.5), var_texcoord0) * 255.0 + 0.5);
    if (src == 0) discard;
    int dst = int(texelFetch(dest, ivec2(gl_FragCoord.xy), 0).r * 255.0 + 0.5);
    int code = int(var_image.y + 0.5);
    int kind = int(var_image.w + 0.5);
    int trans50 = int(lookups.x + 0.5);
    int result;
    if (kind == KIND_CLOAK_BLEND) {
        int mapped = remap(src, (code / PARAM_SPAN) % PARAM_SPAN);
        bool shown = above_threshold(remap(mapped, int(lookups.y + 0.5)), code % PARAM_SPAN);
        result = shown ? mapped : table(trans50, remap(src, code / COLOR_SPAN), dst);
    } else if (kind == KIND_TRANS50) {
        result = table(trans50, remap(src, code), dst);
    } else {
        result = table(code, src, dst);
    }
    out_fragColor = vec4(float(result) / 255.0, 0.0, 0.0, 1.0);
}
