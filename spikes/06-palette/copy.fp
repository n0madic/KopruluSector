#version 140

// Copies the index target so destination-reading kernels see the state before them.
out vec4 out_fragColor;

uniform mediump sampler2D source_target;

void main()
{
    out_fragColor = texelFetch(source_target, ivec2(gl_FragCoord.xy), 0);
}
