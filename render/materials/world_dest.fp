#version 140

// Destination-table world images; dst comes from the copy of the index target made before
// this destination layer. By kind (presentation/draw_list.lua):
//   0 table (methods 6, 9, 10, 17): every opaque texel writes tables[row + (blend ? src : 0)][dst],
//     row = 2 * table row + blend (methods 6 and 9 blend by the source). Every blend bank
//     spans 256 rows (presentation/world_lookups.lua), so row + src stays inside it;
//   1 cloak blend (methods 5/7, 0x40e740): the trans50 blend, then the threshold pass
//     0x406235: where the brightness of the owner-mapped index exceeds the parameter byte
//     (signed compare), the mapped index. row = owner row * 256 + parameter.
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
const int PARAM_SPAN = 256;

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
    int result;
    if (int(var_image.w + 0.5) == KIND_CLOAK_BLEND) {
        int mapped = remap(src, code / PARAM_SPAN);
        bool shown = above_threshold(remap(mapped, int(lookups.y + 0.5)), code % PARAM_SPAN);
        result = shown ? mapped : table(int(lookups.x + 0.5), src, dst);
    } else {
        result = table(code, src, dst);
    }
    out_fragColor = vec4(float(result) / 255.0, 0.0, 0.0, 1.0);
}
