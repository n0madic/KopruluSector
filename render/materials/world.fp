#version 140

// Overwriting world images (draw methods 0/1, 13, 14, 16): the atlas index through a
// source remap row (owner or player colour on 8..15, selection colours on 1..8, shift),
// written into R of the index target. Index 0 is transparent (ADR 0003).
in mediump vec2 var_texcoord0;
flat in mediump vec4 var_image; // x = atlas, y = remap row

out vec4 out_fragColor;

uniform mediump sampler2D atlas0;
uniform mediump sampler2D atlas1;
uniform mediump sampler2D atlas2;
uniform mediump sampler2D atlas3;
uniform mediump sampler2D atlas4;
uniform mediump sampler2D atlas5;
uniform mediump sampler2D remap_table; // 256 x rows, R = mapped index

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
    int index = int(atlas_texel(int(var_image.x + 0.5), var_texcoord0) * 255.0 + 0.5);
    if (index == 0) discard;
    int row = int(var_image.y + 0.5);
    out_fragColor = vec4(texelFetch(remap_table, ivec2(index, row), 0).r, 0.0, 0.0, 1.0);
}
