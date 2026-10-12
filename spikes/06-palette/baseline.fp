#version 140

// Probe fixtures in original top-down order inside each tile (GL rows count upwards):
//   set 0 (blitters, 320x320): (row * 320 + column) & 255
//   set 1 (effects, 640x480):  (i * 17 + row * 31) & 255, i = row * 640 + column
out vec4 out_fragColor;

uniform fs_uniforms
{
    highp vec4 tile; // x = width, y = height, z = set
};

void main()
{
    ivec2 p = ivec2(gl_FragCoord.xy);
    int w = int(tile.x + 0.5), h = int(tile.y + 0.5);
    int column = p.x % w;
    int row = h - 1 - (p.y % h);
    int i = row * w + column;
    int value = int(tile.z + 0.5) == 0 ? (i & 255) : ((i * 17 + row * 31) & 255);
    out_fragColor = vec4(float(value) / 255.0, 0.0, 0.0, 1.0);
}
