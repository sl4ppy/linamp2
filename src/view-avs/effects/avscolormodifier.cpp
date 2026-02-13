#include "avscolormodifier.h"
#include "avsframebuffer.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

AvsColorModifier::AvsColorModifier(Mode mode)
    : m_mode(mode)
{
}

// HSV helper: convert RGB to HSV
static void rgbToHsv(int r, int g, int b, float &h, float &s, float &v)
{
    float rf = r / 255.0f, gf = g / 255.0f, bf = b / 255.0f;
    float maxc = std::max({rf, gf, bf});
    float minc = std::min({rf, gf, bf});
    float delta = maxc - minc;
    v = maxc;
    s = (maxc > 0.0f) ? delta / maxc : 0.0f;
    if (delta < 0.0001f) {
        h = 0.0f;
    } else if (maxc == rf) {
        h = 60.0f * fmodf((gf - bf) / delta + 6.0f, 6.0f);
    } else if (maxc == gf) {
        h = 60.0f * ((bf - rf) / delta + 2.0f);
    } else {
        h = 60.0f * ((rf - gf) / delta + 4.0f);
    }
}

// HSV helper: convert HSV to RGB
static void hsvToRgb(float h, float s, float v, int &r, int &g, int &b)
{
    float c = v * s;
    float x = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f));
    float m = v - c;
    float rf, gf, bf;
    if (h < 60)      { rf = c; gf = x; bf = 0; }
    else if (h < 120) { rf = x; gf = c; bf = 0; }
    else if (h < 180) { rf = 0; gf = c; bf = x; }
    else if (h < 240) { rf = 0; gf = x; bf = c; }
    else if (h < 300) { rf = x; gf = 0; bf = c; }
    else              { rf = c; gf = 0; bf = x; }
    r = static_cast<int>((rf + m) * 255.0f);
    g = static_cast<int>((gf + m) * 255.0f);
    b = static_cast<int>((bf + m) * 255.0f);
}

void AvsColorModifier::render(AvsFramebuffer &fb, const AvsAudioData &)
{
    uint32_t *px = fb.pixels();
    int count = fb.pixelCount();

    switch (m_mode) {
    case Invert:
        for (int i = 0; i < count; i++) {
            uint32_t p = px[i];
            uint32_t a = p & 0xFF000000;
            px[i] = a | (~p & 0x00FFFFFF);
        }
        break;

    case Grayscale:
        for (int i = 0; i < count; i++) {
            uint32_t p = px[i];
            int r = (p >> 16) & 0xFF;
            int g = (p >> 8) & 0xFF;
            int b = p & 0xFF;
            int gray = (r * 77 + g * 150 + b * 29) >> 8; // luminance approximation
            px[i] = (p & 0xFF000000) | (gray << 16) | (gray << 8) | gray;
        }
        break;

    case HueShift:
        m_hueOffset = fmodf(m_hueOffset + 3.0f, 360.0f);
        for (int i = 0; i < count; i++) {
            uint32_t p = px[i];
            int r = (p >> 16) & 0xFF;
            int g = (p >> 8) & 0xFF;
            int b = p & 0xFF;
            if (r == 0 && g == 0 && b == 0)
                continue;
            float h, s, v;
            rgbToHsv(r, g, b, h, s, v);
            h = fmodf(h + m_hueOffset, 360.0f);
            hsvToRgb(h, s, v, r, g, b);
            px[i] = (p & 0xFF000000) | (r << 16) | (g << 8) | b;
        }
        break;

    case BrightnessBoost:
        for (int i = 0; i < count; i++) {
            uint32_t p = px[i];
            int r = std::min(255, static_cast<int>((p >> 16) & 0xFF) + 8);
            int g = std::min(255, static_cast<int>((p >> 8) & 0xFF) + 8);
            int b = std::min(255, static_cast<int>(p & 0xFF) + 8);
            px[i] = (p & 0xFF000000) | (r << 16) | (g << 8) | b;
        }
        break;
    }
}
