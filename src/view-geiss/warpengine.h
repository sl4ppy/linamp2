#ifndef WARPENGINE_H
#define WARPENGINE_H

#include <cstdint>
#include "warpparams.h"

class WarpEngine {
public:
    // Bilinear-interpolated warp with error diffusion.
    // src/dst are RGBA framebuffers, map has numPixels entries,
    // stride is the width of the source framebuffer in pixels.
    void warp(const uint32_t* src, uint32_t* dst,
              const WarpEntry* map, int numPixels, int stride);

    // Darken a small cross-shaped region around (cx,cy) to prevent
    // brightness accumulation at the warp center.
    void diminishCenter(uint32_t* fb, int width, int height,
                        int cx, int cy, float factor);
};

#endif // WARPENGINE_H
