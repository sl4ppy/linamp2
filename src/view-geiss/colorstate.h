#ifndef COLORSTATE_H
#define COLORSTATE_H

#include <cstdint>
#include <cmath>
#include <cstdlib>
#include <algorithm>

struct ColorState {
    float gF[6] = {};

    void randomize() {
        for (int i = 0; i < 6; i++)
            gF[i] = 0.003f + (rand() % 1000) * 0.00001f;
    }

    void getColor(float frame, float base, uint8_t& r, uint8_t& g, uint8_t& b) const {
        float f = 7 * sinf(frame * 0.007f + 29) + 5 * cosf(frame * 0.0057f + 27);
        float cr = 0.58f + 0.21f * sinf(frame * gF[0] + 20 - f) + 0.21f * cosf(frame * gF[3] + 17 + f);
        float cg = 0.58f + 0.21f * sinf(frame * gF[1] + 42 + f) + 0.21f * cosf(frame * gF[4] + 26 - f);
        float cb = 0.58f + 0.21f * sinf(frame * gF[2] + 57 - f) + 0.21f * cosf(frame * gF[5] + 35 + f);
        r = std::min(255, (int)(base * cr));
        g = std::min(255, (int)(base * cg));
        b = std::min(255, (int)(base * cb));
    }
};

#endif // COLORSTATE_H
