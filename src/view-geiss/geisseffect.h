#ifndef GEISSEFFECT_H
#define GEISSEFFECT_H

#include <cstdint>
#include <algorithm>
#include "warpparams.h"

class AudioAnalyzer;

namespace GeissPixel {
    // Qt Format_RGB32 on little-endian: byte[0]=B, byte[1]=G, byte[2]=R, byte[3]=A
    static constexpr int CH_B = 0;
    static constexpr int CH_G = 1;
    static constexpr int CH_R = 2;

    // Additive write: max(existing, value) — never darkens
    inline void additive(uint32_t* fb, int offset, uint8_t r, uint8_t g, uint8_t b) {
        uint8_t* p = reinterpret_cast<uint8_t*>(&fb[offset]);
        if (r > p[CH_R]) p[CH_R] = r;
        if (g > p[CH_G]) p[CH_G] = g;
        if (b > p[CH_B]) p[CH_B] = b;
    }

    // Accumulative write: saturating add
    inline void accumulate(uint32_t* fb, int offset, uint8_t r, uint8_t g, uint8_t b) {
        uint8_t* p = reinterpret_cast<uint8_t*>(&fb[offset]);
        p[CH_R] = std::min(255, (int)p[CH_R] + r);
        p[CH_G] = std::min(255, (int)p[CH_G] + g);
        p[CH_B] = std::min(255, (int)p[CH_B] + b);
    }

    // Bounds-checked additive write
    inline void additiveSafe(uint32_t* fb, int x, int y, int w, int h,
                             uint8_t r, uint8_t g, uint8_t b) {
        if (x >= 0 && x < w && y >= 0 && y < h)
            additive(fb, y * w + x, r, g, b);
    }

    // Bounds-checked accumulate write
    inline void accumulateSafe(uint32_t* fb, int x, int y, int w, int h,
                               uint8_t r, uint8_t g, uint8_t b) {
        if (x >= 0 && x < w && y >= 0 && y < h)
            accumulate(fb, y * w + x, r, g, b);
    }
}

class GeissEffect {
public:
    virtual ~GeissEffect() = default;

    virtual void render(uint32_t* fb, int width, int height,
                       const AudioAnalyzer& audio, float frame) = 0;

    virtual void activate() {}
    virtual void deactivate() {}
};

#endif // GEISSEFFECT_H
