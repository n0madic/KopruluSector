#version 140

// Terrain into the indexed world target: the tile source is single-channel (palette index).
in mediump vec2 var_texcoord0;

out vec4 out_fragColor;

uniform mediump sampler2D texture_sampler;

void main()
{
    out_fragColor = vec4(texture(texture_sampler, var_texcoord0.xy).r, 0.0, 0.0, 1.0);
}
