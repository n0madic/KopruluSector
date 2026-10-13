#version 140

// Distortion world images (cloak, EMP), drawn one at a time after a copy of the index
// target (`dest`) that holds everything ranked before them (ADR 0006 windows). Every opaque
// texel copies the target pixel `d` bytes away in the target's row-major memory order
// (rows from the top, as the original 640x480 bitmap): ahead for normal frames, behind for
// mirrored ones, wrapping at the end of the target like the native 0x4b000-byte wrap. The
// original reads the buffer it writes, but always at addresses its own traversal has not
// written yet, so the copy gives the same pixels. By kind (presentation/draw_list.lua):
//   0 (method 3, 0x4063b5 / 0x406c09): d = brightness of the raw source index;
//   1 (method 8, 0x406533 / 0x406d8e): d = the raw source index;
//   2 (methods 2/4, 0x40e6c0 / 0x40e700): kind 0, then the threshold pass 0x406235: where the
//     brightness of the owner-mapped index exceeds the parameter byte (signed compare), the
//     mapped index. row = owner row * 256 + parameter.
#include "/render/materials/world_common.glsl"

in highp vec2 var_texcoord0;
flat in highp vec4 var_image; // x = atlas, y = row code, z = rank, w = kind
flat in highp vec4 var_frame; // z = 1 when drawn mirrored

out vec4 out_fragColor;

uniform mediump sampler2D dest;

uniform fs_uniforms
{
    highp vec4 lookups; // y = brightness row of remap_table
    highp vec4 target;  // x = width, y = height of the index target
};

const int KIND_RAW = 1;
const int KIND_CLOAK = 2;
const int PARAM_SPAN = 256;

void main()
{
    int src = atlas_index(int(var_image.x + 0.5), var_texcoord0);
    if (src == 0) discard;
    // The depth test against the copy (the depth buffer orders the distortion images).
    if (var_image.z <= stored_rank(texelFetch(dest, ivec2(gl_FragCoord.xy), 0))) discard;
    int kind = int(var_image.w + 0.5);
    int code = int(var_image.y + 0.5);
    int bright_row = int(lookups.y + 0.5);
    if (kind == KIND_CLOAK) {
        int mapped = remap(src, code / PARAM_SPAN);
        if (above_threshold(remap(mapped, bright_row), code % PARAM_SPAN)) {
            out_fragColor = vec4(float(mapped) / 255.0, rank_channels(var_image.z), 1.0);
            return;
        }
    }
    int d = kind == KIND_RAW ? src : remap(src, bright_row);
    int w = int(target.x + 0.5);
    int h = int(target.y + 0.5);
    ivec2 p = ivec2(gl_FragCoord.xy);
    int size = w * h;
    // GL rows are bottom-up; memory rows are top-down.
    int off = d % size;
    int at = ((h - 1 - p.y) * w + p.x + (var_frame.z > 0.5 ? size - off : off)) % size;
    out_fragColor = vec4(texelFetch(dest, ivec2(at % w, h - 1 - at / w), 0).r, rank_channels(var_image.z), 1.0);
}
