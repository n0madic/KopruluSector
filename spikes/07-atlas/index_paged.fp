#version 140

// Spike 07 probe (index_paged): writes the decoded palette index as gray, discards transparent
// pixels, so a capture can be compared with the GRP data (check_capture.py).
in mediump vec2 var_texcoord0;
in mediump float var_page_index;
out vec4 out_fragColor;

uniform mediump sampler2DArray texture_sampler;

#define TEX(s) texture(s, vec3(var_texcoord0.xy, var_page_index))

void main()
{
    float index = TEX(texture_sampler).r;
    if (index < 0.5 / 255.0) discard;
    out_fragColor = vec4(index, index, index, 1.0);
}
