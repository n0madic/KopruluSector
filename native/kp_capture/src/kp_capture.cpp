// kp_capture: the window's last drawn frame, for checks on desktop engines where the index
// target cannot be read from Lua and the OS screen capture may be unavailable. Registers
//   kp_capture.save(path[, bottom_up[, bgra]]) -> width, height: the frame as a PNG
//     (uncompressed: stored deflate blocks, so no compression library is needed);
//   kp_capture.read_index(x, y, w, h, scale[, bottom_up]) -> string: the index target's texels
//     x..x+w-1, y..y+h-1 (GL rows: y = 0 at the bottom; the result bottom-up as GL's
//     readPixels gives it) from a frame the palette pass drew with an identity grey palette
//     at integer scale `scale` from the window's bottom-left corner (viewer autotest).
#define EXTENSION_NAME KpCapture
#define LIB_NAME "KpCapture"

#include <dmsdk/sdk.h>
#include <dmsdk/graphics/graphics.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t g_CrcTable[256];

static void InitCrc()
{
    for (uint32_t n = 0; n < 256; n++)
    {
        uint32_t c = n;
        for (int k = 0; k < 8; k++) c = (c & 1) ? 0xedb88320u ^ (c >> 1) : c >> 1;
        g_CrcTable[n] = c;
    }
}

static uint32_t Crc(uint32_t crc, const uint8_t* p, size_t n)
{
    crc = ~crc;
    for (size_t i = 0; i < n; i++) crc = g_CrcTable[(crc ^ p[i]) & 0xff] ^ (crc >> 8);
    return ~crc;
}

static void PutU32(uint8_t* p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static void WriteChunk(FILE* f, const char* type, const uint8_t* data, uint32_t size)
{
    uint8_t head[8];
    PutU32(head, size);
    memcpy(head + 4, type, 4);
    fwrite(head, 1, 8, f);
    if (size) fwrite(data, 1, size, f);
    uint32_t crc = Crc(0, (const uint8_t*)type, 4);
    crc = Crc(crc, data, size);
    uint8_t tail[4];
    PutU32(tail, crc);
    fwrite(tail, 1, 4, f);
}

// RGBA rows (row 0 at the top) as an 8-bit RGBA PNG.
static bool WritePng(const char* path, const uint8_t* rgba, uint32_t w, uint32_t h)
{
    size_t raw_size = (size_t)(w * 4 + 1) * h;
    uint8_t* raw = (uint8_t*)malloc(raw_size);
    for (uint32_t y = 0; y < h; y++)
    {
        raw[y * (w * 4 + 1)] = 0; // filter: none
        memcpy(raw + y * (w * 4 + 1) + 1, rgba + (size_t)y * w * 4, w * 4);
    }
    // zlib stream of stored blocks of at most 65535 bytes
    size_t blocks = raw_size / 65535 + 1;
    size_t z_size = 2 + raw_size + blocks * 5 + 4;
    uint8_t* z = (uint8_t*)malloc(z_size);
    size_t at = 0;
    z[at++] = 0x78;
    z[at++] = 0x01;
    uint32_t a = 1, b = 0;
    for (size_t pos = 0; pos < raw_size || pos == 0;)
    {
        size_t n = raw_size - pos > 65535 ? 65535 : raw_size - pos;
        z[at++] = pos + n == raw_size ? 1 : 0;
        z[at++] = (uint8_t)n;
        z[at++] = (uint8_t)(n >> 8);
        z[at++] = (uint8_t)~n;
        z[at++] = (uint8_t)(~n >> 8);
        memcpy(z + at, raw + pos, n);
        for (size_t i = 0; i < n; i++)
        {
            a = (a + raw[pos + i]) % 65521;
            b = (b + a) % 65521;
        }
        at += n;
        pos += n;
        if (n == 0) break;
    }
    PutU32(z + at, (b << 16) | a);
    at += 4;
    FILE* f = fopen(path, "wb");
    bool ok = f != 0;
    if (ok)
    {
        static const uint8_t signature[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n' };
        fwrite(signature, 1, 8, f);
        uint8_t ihdr[13];
        PutU32(ihdr, w);
        PutU32(ihdr + 4, h);
        ihdr[8] = 8;  // bit depth
        ihdr[9] = 6;  // RGBA
        ihdr[10] = 0; // deflate
        ihdr[11] = 0; // adaptive filtering
        ihdr[12] = 0; // no interlace
        WriteChunk(f, "IHDR", ihdr, 13);
        WriteChunk(f, "IDAT", z, (uint32_t)at);
        WriteChunk(f, "IEND", 0, 0);
        ok = fclose(f) == 0;
    }
    free(z);
    free(raw);
    return ok;
}

// kp_capture.save(path[, bottom_up[, bgra]]): reads the window's frame buffer; the rows come
// bottom-up and the channels as BGRA when the flags say so (they depend on the graphics
// backend). Returns the width and height, or raises.
static int Save(lua_State* L)
{
    const char* path = luaL_checkstring(L, 1);
    bool bottom_up = lua_toboolean(L, 2);
    bool bgra = lua_toboolean(L, 3);
    dmGraphics::HContext context = dmGraphics::GetInstalledContext();
    uint32_t w = dmGraphics::GetWindowWidth(context), h = dmGraphics::GetWindowHeight(context);
    uint32_t size = w * h * 4;
    uint8_t* pixels = (uint8_t*)malloc(size);
    dmGraphics::ReadPixels(context, 0, 0, w, h, pixels, size);
    uint8_t* rows = (uint8_t*)malloc(size);
    for (uint32_t y = 0; y < h; y++)
    {
        const uint8_t* src = pixels + (size_t)(bottom_up ? h - 1 - y : y) * w * 4;
        uint8_t* dst = rows + (size_t)y * w * 4;
        for (uint32_t x = 0; x < w; x++)
        {
            dst[x * 4 + 0] = src[x * 4 + (bgra ? 2 : 0)];
            dst[x * 4 + 1] = src[x * 4 + 1];
            dst[x * 4 + 2] = src[x * 4 + (bgra ? 0 : 2)];
            dst[x * 4 + 3] = 255;
        }
    }
    bool ok = WritePng(path, rows, w, h);
    free(rows);
    free(pixels);
    if (!ok) return luaL_error(L, "kp_capture: cannot write %s", path);
    lua_pushinteger(L, w);
    lua_pushinteger(L, h);
    return 2;
}

static int ReadIndex(lua_State* L)
{
    int x0 = luaL_checkint(L, 1), y0 = luaL_checkint(L, 2);
    int w = luaL_checkint(L, 3), h = luaL_checkint(L, 4), scale = luaL_checkint(L, 5);
    bool bottom_up = lua_toboolean(L, 6);
    dmGraphics::HContext context = dmGraphics::GetInstalledContext();
    int ww = (int)dmGraphics::GetWindowWidth(context), wh = (int)dmGraphics::GetWindowHeight(context);
    if (scale < 1 || x0 < 0 || y0 < 0 || w < 0 || h < 0 || (x0 + w) * scale > ww || (y0 + h) * scale > wh)
    {
        lua_pushnil(L);
        lua_pushfstring(L, "kp_capture: texels %d,%d %dx%d at scale %d outside the %dx%d window", x0, y0, w, h, scale, ww, wh);
        return 2;
    }
    uint32_t size = (uint32_t)(ww * wh * 4);
    uint8_t* pixels = (uint8_t*)malloc(size);
    dmGraphics::ReadPixels(context, 0, 0, ww, wh, pixels, size);
    luaL_Buffer b;
    luaL_buffinit(L, &b);
    for (int ty = y0; ty < y0 + h; ty++)
    {
        int up = ty * scale + scale / 2; // window row counted from the bottom
        int row = bottom_up ? up : wh - 1 - up;
        for (int tx = x0; tx < x0 + w; tx++)
        {
            int col = tx * scale + scale / 2;
            luaL_addchar(&b, (char)pixels[((size_t)row * ww + col) * 4 + 1]); // grey: any of R, G, B
        }
    }
    free(pixels);
    luaL_pushresult(&b);
    return 1;
}

static dmExtension::Result Initialize(dmExtension::Params* params)
{
    InitCrc();
    static const luaL_reg functions[] = { { "save", Save }, { "read_index", ReadIndex }, { 0, 0 } };
    luaL_register(params->m_L, "kp_capture", functions);
    lua_pop(params->m_L, 1);
    return dmExtension::RESULT_OK;
}

DM_DECLARE_EXTENSION(EXTENSION_NAME, LIB_NAME, 0, 0, Initialize, 0, 0, 0)
