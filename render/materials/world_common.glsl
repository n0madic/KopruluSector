// Shared by the world image fragment programs (world.fp, world_dest.fp, world_distort.fp):
// the six atlas samplers, the remap table and the lookups every family uses.

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

// Palette index of an atlas texel.
int atlas_index(int atlas, vec2 uv)
{
    return int(atlas_texel(atlas, uv) * 255.0 + 0.5);
}

// Index `index` mapped through remap row `row`.
int remap(int index, int row)
{
    return int(texelFetch(remap_table, ivec2(index, row), 0).r * 255.0 + 0.5);
}

// Signed BYTE compare of the threshold kernels (0x406235): parameter < brightness.
bool above_threshold(int brightness, int param)
{
    return (param >= 128 ? param - 256 : param) < brightness;
}
