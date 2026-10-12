#version 140

// Spike 07 probe (index_mask): writes the decoded palette index as gray, discards transparent
// pixels, so a capture can be compared with the GRP data (check_capture.py).
in mediump vec2 var_texcoord0;

out vec4 out_fragColor;

uniform mediump sampler2D texture_sampler;
uniform mediump sampler2D mask_sampler;

#define TEX(s) texture(s, var_texcoord0.xy)

void main()
{
    float index = TEX(texture_sampler).r;
    if (TEX(mask_sampler).r < 0.5) discard;
    out_fragColor = vec4(index, index, index, 1.0);
}
