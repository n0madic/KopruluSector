#version 140

// The fog of war over the composed view, in index space, as DrawFogOverlay 0x46c3c0 darkens the original's view after
// its sprites (atlas P23/P24; native/kp_core/src/core/kp_fog_view.c kp_fog_view_shade is the CPU reference). It reads
// the copy of the index target (dest) and writes the darkened index back: every 8x8-pixel map block takes the four
// levels of the level map around its corner (fog_levels, one byte per block); four equal levels 0 make the block
// black, 31 leave it, another level darkens it through that dark.pcx row (fog_dark: 256 x 32, the darkened index of
// index i in row level); unequal ones interpolate the level per pixel in 8.8 fixed point.
out vec4 out_fragColor;

uniform mediump sampler2D dest;
uniform mediump sampler2D fog_levels;
uniform mediump sampler2D fog_dark;

uniform fs_uniforms
{
    highp vec4 fog; // xy = the map pixel of the view's top-left, z = the view's height in pixels
    highp vec4 fog_origin; // xy = the map block of the level map's first cell, zw = its size in blocks
};

const int SEEN = 31;

int byte_at(mediump sampler2D t, ivec2 at)
{
    return int(texelFetch(t, at, 0).r * 255.0 + 0.5);
}

int level_at(ivec2 block)
{
    ivec2 cell = block - ivec2(fog_origin.xy);
    if (cell.x < 0 || cell.y < 0 || cell.x >= int(fog_origin.z) || cell.y >= int(fog_origin.w)) return 0;
    return byte_at(fog_levels, cell);
}

int fogged(int index, ivec2 map)
{
    ivec2 block = map / 8, k = map - block * 8;
    int tl = level_at(block), tr = level_at(block + ivec2(1, 0));
    int bl = level_at(block + ivec2(0, 1)), br = level_at(block + ivec2(1, 1));
    int level;
    if (tl == tr && tl == bl && tl == br) {
        if (tl == 0) return 0;
        if (tl == SEEN) return index;
        level = tl;
    } else {
        int l = tl * 256 + k.y * (bl - tl) * 32, r = tr * 256 + k.y * (br - tr) * 32;
        int step = (l - r) / 8; // l - r is a multiple of 32
        level = (r + (7 - k.x) * step) / 256;
    }
    return byte_at(fog_dark, ivec2(index, level));
}

void main()
{
    ivec2 texel = ivec2(gl_FragCoord.xy);
    vec4 copy = texelFetch(dest, texel, 0);
    int index = int(copy.r * 255.0 + 0.5);
    // texel row 0 is the bottom of the view (the world's y grows upward)
    ivec2 map = ivec2(int(fog.x) + texel.x, int(fog.y) + int(fog.z) - 1 - texel.y);
    out_fragColor = vec4(float(fogged(index, map)) / 255.0, copy.gba);
}
