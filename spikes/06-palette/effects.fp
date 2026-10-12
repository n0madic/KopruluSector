#version 140

// Original effect kernels on a 640x480 indexed buffer (docs/research/native-pixel-effects.md):
//   stage 0 cloak : dst[d] = cur[(d +- brightness[src]) mod 0x4b000]   (method 3; methods 2/4 first pass)
//   stage 1 raw   : dst[d] = cur[(d +- src) mod 0x4b000]              (method 8)
//   stage 2 threshold: m = map[src]; write m when (signed) brightness[m] > (signed) arg
//   stage 3 blend : dst[d] = trans50[src][dst]                          (methods 5/6/7)
//   stage 4 warp  : where the main image is opaque, the texture frame pixel at the same
//                   crop position (column reversed when mirrored), 0xA5 where it is
//                   transparent (probe-supplied scratch byte)            (method 12)
//   stage 5 outline: arg written on the crop's retained frame edges      (method 15)
// cur is the buffer *during* the in-place traversal: a sample that lands on a pixel of
// the same image already processed (earlier in row order; rows right-to-left when
// mirrored) takes that pixel's own result, found by following the chain.
flat in highp vec4 var_place;
flat in highp vec4 var_shape;
flat in highp vec4 var_source;
flat in highp vec4 var_extra;   // x = map row, yz = texture frame atlas position, w = frame height

out vec4 out_fragColor;

uniform mediump sampler2D frames;
uniform mediump sampler2D tables; // row 0 brightness, 1 identity, 2 reversed, 3.. trans50 bank
uniform mediump sampler2D dest;   // copy of the buffer before this image

const int W = 640;
const int H = 480;
const int WRAP = 0x4b000;
const int MAX_CHAIN = 256;

int idx(vec4 v) { return int(v.r * 255.0 + 0.5); }
int sgn8(int v) { return v >= 128 ? v - 256 : v; }

ivec2 g_tile;     // GL origin of this tile
ivec4 g_place;    // x, y, left, top
ivec4 g_shape;    // cw, ch, fw, mirror
ivec2 g_frame;    // atlas position of the frame

// Source texel for image-local (xx, yy); returns -1 when transparent.
int source_at(int xx, int yy)
{
    int sx = g_shape.w == 1 ? g_shape.z - 1 - g_place.z - xx : g_place.z + xx;
    vec4 t = texelFetch(frames, g_frame + ivec2(sx, g_place.w + yy), 0);
    return t.a < 0.5 ? -1 : idx(t);
}

int order_of(int xx, int yy) { return yy * g_shape.x + (g_shape.w == 1 ? g_shape.x - 1 - xx : xx); }

int buffer_at(int offset)
{
    int x = offset % W, y = offset / W;
    return idx(texelFetch(dest, g_tile + ivec2(x, H - 1 - y), 0));
}

int table(int row, int column) { return idx(texelFetch(tables, ivec2(column, row), 0)); }

void main()
{
    ivec2 g = ivec2(gl_FragCoord.xy);
    g_tile = ivec2((g.x / W) * W, (g.y / H) * H);
    g_place = ivec4(var_place + 0.5);
    g_shape = ivec4(var_shape + 0.5);
    g_frame = ivec2(var_source.xy + 0.5);
    int stage = int(var_source.z + 0.5);
    int arg = int(var_source.w + 0.5);
    int lx = g.x - g_tile.x;
    int ny = H - 1 - (g.y - g_tile.y);
    int xx = lx - g_place.x, yy = ny - g_place.y;
    int result;
    if (stage == 5) {
        int fh = int(var_extra.w + 0.5);
        bool edge = (g_place.w == 0 && yy == 0) || (g_place.z == 0 && xx == 0)
            || (g_place.z + g_shape.x == g_shape.z && xx == g_shape.x - 1)
            || (g_place.w + g_shape.y == fh && yy == g_shape.y - 1);
        if (!edge) discard;
        out_fragColor = vec4(float(arg) / 255.0, 0.0, 0.0, 1.0);
        return;
    }
    int src = source_at(xx, yy);
    if (src < 0) discard;
    if (stage == 4) {
        int tx = g_shape.w == 1 ? g_shape.x - 1 - xx : xx;
        vec4 t = texelFetch(frames, ivec2(var_extra.yz + 0.5) + ivec2(g_place.z + tx, g_place.w + yy), 0);
        result = t.a < 0.5 ? 0xA5 : idx(t);
    } else if (stage == 2) {
        int m = table(int(var_extra.x + 0.5), src);
        if (sgn8(table(0, m)) <= sgn8(arg)) discard;
        result = m;
    } else if (stage == 3) {
        result = table(3 + src, buffer_at(ny * W + lx));
    } else {
        int d = ny * W + lx;
        int order = order_of(xx, yy);
        int value = src;
        result = -1;
        for (int k = 0; k < MAX_CHAIN; k++) {
            int disp = stage == 0 ? table(0, value) : value;
            int s = (d + (g_shape.w == 1 ? -disp : disp)) % WRAP;
            if (s < 0) s += WRAP;
            int sxx = s % W - g_place.x, syy = s / W - g_place.y;
            bool inside = sxx >= 0 && syy >= 0 && sxx < g_shape.x && syy < g_shape.y;
            int sv = inside ? source_at(sxx, syy) : -1;
            if (sv >= 0 && order_of(sxx, syy) < order) {
                d = s; order = order_of(sxx, syy); value = sv; // processed earlier: follow its sample
            } else {
                result = buffer_at(s);
                break;
            }
        }
        if (result < 0) result = 0; // chain longer than MAX_CHAIN (reported by the byte check)
    }
    out_fragColor = vec4(float(result) / 255.0, 0.0, 0.0, 1.0);
}
