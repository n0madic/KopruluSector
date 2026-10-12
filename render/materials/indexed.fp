#version 140

// Writes the palette index of an indexed atlas texel into R (A = 1).
// Atlas texels: R = colour index; index 0 is transparent (ADR 0003; packs with opaque index 0
// would add a mask sampler, verified in spike 06).
// Source remap: index -> remap_table[row][index] (player colours 8..15, selection 1..8, shift).
in mediump vec2 var_texcoord0;
flat in mediump vec4 var_remap;

out vec4 out_fragColor;

uniform mediump sampler2D texture_sampler;
uniform mediump sampler2D remap_table; // 256 x rows, R = mapped index

void main()
{
    // ADR 0003: single-channel index atlases, index 0 is transparent.
    int index = int(texture(texture_sampler, var_texcoord0).r * 255.0 + 0.5);
    if (index == 0) discard;
    int row = int(var_remap.x + 0.5);
    float mapped = texelFetch(remap_table, ivec2(index, row), 0).r;
    out_fragColor = vec4(mapped, 0.0, 0.0, 1.0);
}
