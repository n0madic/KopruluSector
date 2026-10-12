#version 140

// Destination-reading images (shadow, fire/blood, trans50 and other table kernels):
// opaque texels write tables[row + (blend ? src : 0)][dst]; dst comes from a copy of
// the index target made before this pass. remap.y = 1 selects the blend form.
in mediump vec2 var_texcoord0;
flat in mediump vec4 var_remap;

out vec4 out_fragColor;

uniform mediump sampler2D texture_sampler;
uniform mediump sampler2D dest;
uniform mediump sampler2D tables;

void main()
{
    // ADR 0003: single-channel index atlases, index 0 is transparent.
    int src = int(texture(texture_sampler, var_texcoord0).r * 255.0 + 0.5);
    if (src == 0) discard;
    int dst = int(texelFetch(dest, ivec2(gl_FragCoord.xy), 0).r * 255.0 + 0.5);
    int row = int(var_remap.x + 0.5) + (var_remap.y > 0.5 ? src : 0);
    out_fragColor = vec4(texelFetch(tables, ivec2(dst, row), 0).r, 0.0, 0.0, 1.0);
}
