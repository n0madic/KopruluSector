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
in highp vec2 var_texcoord0;
flat in highp vec4 var_image; // x = atlas, y = row code, w = kind
flat in highp vec4 var_frame; // z = 1 when drawn mirrored

out vec4 out_fragColor;

uniform mediump sampler2D atlas0;
uniform mediump sampler2D atlas1;
uniform mediump sampler2D atlas2;
uniform mediump sampler2D atlas3;
uniform mediump sampler2D atlas4;
uniform mediump sampler2D atlas5;
uniform mediump sampler2D dest;
uniform mediump sampler2D remap_table;

uniform fs_uniforms
{
    highp vec4 lookups; // y = brightness row of remap_table
    highp vec4 target;  // x = width, y = height of the index target
};

const int KIND_RAW = 1;
const int KIND_CLOAK = 2;
const int PARAM_SPAN = 256;

float atlas_texel(int atlas, vec2 uv)
{
    if (atlas == 0) return texture(atlas0, uv).r;
    if (atlas == 1) return texture(atlas1, uv).r;
    if (atlas == 2) return texture(atlas2, uv).r;
    if (atlas == 3) return texture(atlas3, uv).r;
    if (atlas == 4) return texture(atlas4, uv).r;
    return texture(atlas5, uv).r;
}

int remap(int index, int row)
{
    return int(texelFetch(remap_table, ivec2(index, row), 0).r * 255.0 + 0.5);
}

bool above_threshold(int brightness, int param)
{
    return (param >= 128 ? param - 256 : param) < brightness;
}

void main()
{
    int src = int(atlas_texel(int(var_image.x + 0.5), var_texcoord0) * 255.0 + 0.5);
    if (src == 0) discard;
    int kind = int(var_image.w + 0.5);
    int code = int(var_image.y + 0.5);
    int bright_row = int(lookups.y + 0.5);
    if (kind == KIND_CLOAK) {
        int mapped = remap(src, code / PARAM_SPAN);
        if (above_threshold(remap(mapped, bright_row), code % PARAM_SPAN)) {
            out_fragColor = vec4(float(mapped) / 255.0, 0.0, 0.0, 1.0);
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
    out_fragColor = vec4(texelFetch(dest, ivec2(at % w, h - 1 - at / w), 0).r, 0.0, 0.0, 1.0);
}
