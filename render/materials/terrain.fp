#version 140

// Terrain into the indexed world target. The tile source is single-channel: R = palette
// index. Every terrain texel is opaque, index 0 included.
in highp vec2 var_texcoord0;

out vec4 out_fragColor;

uniform mediump sampler2D texture_sampler;

void main()
{
    out_fragColor = vec4(texture(texture_sampler, var_texcoord0.xy).r, 0.0, 0.0, 1.0);
}
