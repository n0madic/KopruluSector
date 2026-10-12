#version 140

// Sharp bilinear upscale of the RGB world image: nearest inside each source texel,
// a one-output-pixel bilinear blend across texel edges (scale = output/source).
in mediump vec2 var_texcoord0;

out vec4 out_fragColor;

uniform mediump sampler2D rgb_target; // must use linear filtering

uniform fs_uniforms
{
    mediump vec4 source_size; // xy = source texels, zw = scale factors
};

void main()
{
    vec2 texel = var_texcoord0 * source_size.xy;
    vec2 base = floor(texel);
    vec2 frac_part = texel - base;
    vec2 region = 0.5 - 0.5 / source_size.zw;
    vec2 center_dist = frac_part - 0.5;
    vec2 f = (center_dist - clamp(center_dist, -region, region)) * source_size.zw + 0.5;
    out_fragColor = texture(rgb_target, (base + f) / source_size.xy);
}
