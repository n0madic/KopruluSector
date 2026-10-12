#version 140

// Destination-table world images (draw methods 9, 10, 17): every opaque texel writes
// tables[row + (blend ? src : 0)][dst], dst from the copy of the index target made before
// this destination layer. row = 2 * table row + blend (method 9 blends by the source).
in mediump vec2 var_texcoord0;
flat in mediump vec4 var_image; // x = atlas, y = 2 * table row + blend

out vec4 out_fragColor;

uniform mediump sampler2D atlas0;
uniform mediump sampler2D atlas1;
uniform mediump sampler2D atlas2;
uniform mediump sampler2D atlas3;
uniform mediump sampler2D atlas4;
uniform mediump sampler2D atlas5;
uniform mediump sampler2D dest;
uniform mediump sampler2D tables;

float atlas_texel(int atlas, vec2 uv)
{
    if (atlas == 0) return texture(atlas0, uv).r;
    if (atlas == 1) return texture(atlas1, uv).r;
    if (atlas == 2) return texture(atlas2, uv).r;
    if (atlas == 3) return texture(atlas3, uv).r;
    if (atlas == 4) return texture(atlas4, uv).r;
    return texture(atlas5, uv).r;
}

void main()
{
    int src = int(atlas_texel(int(var_image.x + 0.5), var_texcoord0) * 255.0 + 0.5);
    if (src == 0) discard;
    int dst = int(texelFetch(dest, ivec2(gl_FragCoord.xy), 0).r * 255.0 + 0.5);
    int code = int(var_image.y + 0.5);
    int row = code / 2 + (code % 2 == 1 ? src : 0);
    out_fragColor = vec4(texelFetch(tables, ivec2(dst, row), 0).r, 0.0, 0.0, 1.0);
}
