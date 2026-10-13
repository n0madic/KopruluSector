#version 140

// Restores the index target from its copy after the target is bound again (render script
// copy_target: Defold's Vulkan backend loads neither colour nor depth of a render target on
// a later binding). The depth goes back to the cleared value; the ranks the composition
// tests against stay in G and B of the copy (world_common.glsl).
out vec4 out_fragColor;

uniform mediump sampler2D dest;

void main()
{
    out_fragColor = texelFetch(dest, ivec2(gl_FragCoord.xy), 0);
    gl_FragDepth = 0.0;
}
