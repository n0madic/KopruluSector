#version 140

// The original indexed kernels (docs/research/native-pixel-blitters.md):
//   method 0: out = map[row][src]                (foreground through the current map)
//   method 1: out = table[row + src][dst]        (source/background blend, src*256+dst)
//   method 2: out = table[row][dst]              (shadow / fire background lookup)
// Transparent texels (GRP skip packets) leave the destination untouched.
in highp vec2 var_texcoord0;
flat in mediump vec4 var_params;

out vec4 out_fragColor;

uniform mediump sampler2D frames;
uniform mediump sampler2D remap;
uniform mediump sampler2D tables;
uniform mediump sampler2D dest;

int idx(vec4 v) { return int(v.r * 255.0 + 0.5); }

void main()
{
    vec4 texel = texelFetch(frames, ivec2(floor(var_texcoord0)), 0);
    if (texel.a < 0.5) discard;
    int src = idx(texel);
    int method = int(var_params.x + 0.5);
    int row = int(var_params.y + 0.5);
    int dst = idx(texelFetch(dest, ivec2(gl_FragCoord.xy), 0));
    vec4 result;
    if (method == 0) result = texelFetch(remap, ivec2(src, row), 0);
    else if (method == 1) result = texelFetch(tables, ivec2(dst, row + src), 0);
    else result = texelFetch(tables, ivec2(dst, row), 0);
    out_fragColor = vec4(result.r, 0.0, 0.0, 1.0);
}
