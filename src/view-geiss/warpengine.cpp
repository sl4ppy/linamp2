#include "warpengine.h"

void WarpEngine::warp(const uint32_t* src, uint32_t* dst,
                      const WarpEntry* map, int numPixels, int stride)
{
    const uint8_t* srcBytes = reinterpret_cast<const uint8_t*>(src);
    uint8_t*       dstBytes = reinterpret_cast<uint8_t*>(dst);

    int srcOffset = 0;
    int dstIdx    = 0;

    uint16_t errR = 0;
    uint16_t errG = 0;
    uint16_t errB = 0;

    const int stride4 = stride * 4;

    for (int i = 0; i < numPixels; ++i) {
        srcOffset += map[i].offset;

        const uint8_t* p = srcBytes + srcOffset;

        uint8_t w0 = map[i].w[0];
        uint8_t w1 = map[i].w[1];
        uint8_t w2 = map[i].w[2];
        uint8_t w3 = map[i].w[3];

        // Channel 0 (Blue in Qt Format_RGB32 little-endian) with error diffusion
        uint16_t c0 = p[0] * w0 + p[4] * w1 + p[stride4] * w2 + p[stride4 + 4] * w3 + errR;
        errR = c0 & 0xFF;
        dstBytes[dstIdx + 0] = c0 >> 8;

        // Channel 1 (Green) with error diffusion
        uint16_t c1 = p[1] * w0 + p[5] * w1 + p[stride4 + 1] * w2 + p[stride4 + 5] * w3 + errG;
        errG = c1 & 0xFF;
        dstBytes[dstIdx + 1] = c1 >> 8;

        // Channel 2 (Red) with error diffusion
        uint16_t c2 = p[2] * w0 + p[6] * w1 + p[stride4 + 2] * w2 + p[stride4 + 6] * w3 + errB;
        errB = c2 & 0xFF;
        dstBytes[dstIdx + 2] = c2 >> 8;

        // Alpha
        dstBytes[dstIdx + 3] = 0xFF;

        dstIdx += 4;
    }
}

void WarpEngine::diminishCenter(uint32_t* fb, int width, int height,
                                int cx, int cy, float factor)
{
    // 5-pixel cross pattern: center, left, right, up, down
    const int offsets[][2] = {
        { 0,  0},
        {-1,  0},
        { 1,  0},
        { 0, -1},
        { 0,  1},
    };

    for (int i = 0; i < 5; ++i) {
        int x = cx + offsets[i][0];
        int y = cy + offsets[i][1];

        if (x < 0 || x >= width || y < 0 || y >= height)
            continue;

        uint8_t* p = reinterpret_cast<uint8_t*>(&fb[y * width + x]);
        p[0] = static_cast<uint8_t>(p[0] * factor);
        p[1] = static_cast<uint8_t>(p[1] * factor);
        p[2] = static_cast<uint8_t>(p[2] * factor);
    }
}
