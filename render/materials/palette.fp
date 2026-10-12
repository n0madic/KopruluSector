#version 140

// Palette pass: index render target -> RGB through the palette constant array. A constant,
// not a texture: every texture upload has a fixed cost of milliseconds on mobile Vulkan
// (docs/spikes/15-upload.md), and palette cycling changes the palette often.
// mode.x = 0: integer/nearest sampling of the index target at screen resolution.
// mode.x = 1: output at world resolution (an upscale pass follows).
in mediump vec2 var_texcoord0;

out vec4 out_fragColor;

uniform mediump sampler2D index_target;

uniform fs_uniforms
{
    mediump vec4 source_size; // xy = index target size in texels
    mediump vec4 palette_colors[256]; // rgb = colour of index i, in 0..1 (byte / 255)
};

void main()
{
    ivec2 texel = ivec2(floor(var_texcoord0 * source_size.xy));
    texel = clamp(texel, ivec2(0), ivec2(source_size.xy) - 1);
    int index = int(texelFetch(index_target, texel, 0).r * 255.0 + 0.5);
    out_fragColor = vec4(palette_colors[index].rgb, 1.0);
}
