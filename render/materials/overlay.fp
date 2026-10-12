#version 140

// Debug overlays (walkability, regions) in RGB space after the palette pass.
in highp vec2 var_texcoord0;

out vec4 out_fragColor;

uniform mediump sampler2D texture_sampler;

void main()
{
    out_fragColor = texture(texture_sampler, var_texcoord0.xy);
}
