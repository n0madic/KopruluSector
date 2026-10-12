#version 140

// Palette pass: index from the world target, color from the 256x1 runtime palette.
in mediump vec2 var_uv;

out vec4 out_fragColor;

uniform mediump sampler2D index_tex;
uniform mediump sampler2D palette_tex;

void main()
{
    float index = floor(texture(index_tex, var_uv).r * 255.0 + 0.5);
    out_fragColor = vec4(texture(palette_tex, vec2((index + 0.5) / 256.0, 0.5)).rgb, 1.0);
}
