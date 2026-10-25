#version 140

// The interface's canvas over the composed world (ADR 0011): kp_ui composes every panel and the console art into
// one texture of the logical screen's size on the CPU (palette index in R, opacity in A; rows top first), and
// this pass writes its opaque texels (opacity 255, or 254 for the layers over the game view: the text lines, the
// tooltip, the drag box and the command marker, which the host's input tells from the console art) into the index target before the palette pass. A translucent texel (opacity
// 128, the in-game dialogs' border colour, kp_ui_canvas_dialog) writes the blend row's entry of the world's index
// under it (dest: a copy of the index target; ui_blend: 256 x 1, the index in R; bound to the canvas itself when the
// frame has no translucent texels). The interface's cursor (ui_cursor: its frame, rows top first, opacity 255 or 0) lies
// over the canvas at its rectangle.
out vec4 out_fragColor;

uniform mediump sampler2D ui_canvas;
uniform mediump sampler2D dest;
uniform mediump sampler2D ui_blend;
uniform mediump sampler2D ui_cursor;

uniform fs_uniforms
{
    highp vec4 cursor; // xy = the frame's top-left on the logical screen (rows top first), zw = its size (0: none)
};

void main()
{
    ivec2 size = textureSize(ui_canvas, 0);
    ivec2 at = ivec2(gl_FragCoord.xy);
    ivec2 in_cursor = ivec2(at.x, size.y - 1 - at.y) - ivec2(cursor.xy);
    if (in_cursor.x >= 0 && in_cursor.y >= 0 && in_cursor.x < int(cursor.z) && in_cursor.y < int(cursor.w)) {
        mediump vec4 c = texelFetch(ui_cursor, in_cursor, 0);
        if (c.a >= 0.5) {
            out_fragColor = vec4(c.r, 0.0, 0.0, 1.0);
            return;
        }
    }
    mediump vec4 texel = texelFetch(ui_canvas, ivec2(at.x, size.y - 1 - at.y), 0);
    if (texel.a < 0.25) discard;
    if (texel.a < 0.75) {
        int under = int(texelFetch(dest, at, 0).r * 255.0 + 0.5);
        out_fragColor = vec4(texelFetch(ui_blend, ivec2(under, 0), 0).r, 0.0, 0.0, 1.0);
        return;
    }
    out_fragColor = vec4(texel.r, 0.0, 0.0, 1.0);
}
