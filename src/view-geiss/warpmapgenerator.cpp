#include "warpmapgenerator.h"

#include <cmath>
#include <cstdlib>
#include <algorithm>

void WarpMapGenerator::generate(const WarpParams &params)
{
    m_params = params;
    start();
}

static float computeScale(float dx, float dy, float r, float rmult,
                           float dy_raw, int x, int y,
                           const WarpParams &p)
{
    switch (p.mode) {
    case WarpMode::InwardZoom:
        return p.scale;
    case WarpMode::OutwardZoom:
        return p.scale;
    case WarpMode::Sphere:
        return 0.9f + r * rmult * p.f1;
    case WarpMode::Ripples:
        return 0.85f + 0.1f * sinf(sqrtf(r * rmult) * p.f1);
    case WarpMode::Vortex:
        return 1.04f - p.f1 * sqrtf(dx * dx + dy * dy) / (FB_W * 0.5f);
    case WarpMode::Perspective: {
        float rr = sqrtf(sqrtf(dx * dx + dy * dy)) * 0.19f * rmult;
        return p.f2 - p.f1 * rr;
    }
    case WarpMode::Terra:
        return p.scale - dy_raw * p.f1;
    case WarpMode::Fuzzy: {
        float rr = sqrtf(dx * dx + dy * dy) * p.f2 * rmult;
        return p.f1 - rr + ((rand() % 100) - 50) * 0.001f;
    }
    case WarpMode::Flower:
        return p.scale;
    case WarpMode::SpinBlur:
        return p.scale;
    case WarpMode::HTunnel: {
        float ny = (y - p.centerY) * 2.0f / FB_H;
        return p.scale - ny * ny * p.f1;
    }
    case WarpMode::VTunnel: {
        float nx = (x - p.centerX) * 2.0f / FB_W;
        return p.scale - nx * nx * p.f1;
    }
    case WarpMode::BlackHole: {
        float rn = r * rmult;
        float s = 1.04f - rn * sqrtf(rn) * 0.000035f;
        return (s - 1.0f) * p.f1 + 1.0f;
    }
    case WarpMode::PetalRings: {
        float angle = atan2f(dy, dx);
        return p.scale + 0.08f * sinf(angle * p.f1);
    }
    case WarpMode::CrystalBall: {
        float rn = r * rmult;
        return p.scale + rn * rn * 0.000001f;
    }
    default:
        return p.scale;
    }
}

void WarpMapGenerator::run()
{
    std::vector<WarpEntry> map(FB_PIXELS);

    int prevOffset = 0;

    for (int y = 0; y < FB_H; ++y) {
        for (int x = 0; x < FB_W; ++x) {
            int idx = y * FB_W + x;

            float dx = x - m_params.centerX;
            float dy_raw = y - m_params.centerY;
            float dy = dy_raw * ASPECT_COMPENSATION;

            float r = sqrtf(dx * dx + dy * dy);
            float rmult = 320.0f / FB_W;

            float scale = computeScale(dx, dy, r, rmult, dy_raw, x, y, m_params);

            // Rotation
            float nx = dx * m_params.cosT - dy * m_params.sinT;
            float ny = dx * m_params.sinT + dy * m_params.cosT;

            // Scale + translate
            float srcX = nx * scale + m_params.centerX;
            float srcY = ny * scale / ASPECT_COMPENSATION + m_params.centerY;

            // Damping
            srcX = x * (1.0f - m_params.damping) + srcX * m_params.damping;
            srcY = y * (1.0f - m_params.damping) + srcY * m_params.damping;

            // Clamp
            srcX = std::clamp(srcX, 1.0f, (float)(FB_W - 2));
            srcY = std::clamp(srcY, 1.0f, (float)(FB_H - 2));

            // Bilinear weights
            int ix = (int)srcX;
            int iy = (int)srcY;
            float fx = srcX - ix;
            float fy = srcY - iy;

            map[idx].w[0] = (uint8_t)((1.0f - fx) * (1.0f - fy) * WEIGHT_SUM);
            map[idx].w[1] = (uint8_t)(fx * (1.0f - fy) * WEIGHT_SUM);
            map[idx].w[2] = (uint8_t)((1.0f - fx) * fy * WEIGHT_SUM);
            map[idx].w[3] = (uint8_t)(fx * fy * WEIGHT_SUM);

            // Relative offset
            int absOffset = (iy * FB_W + ix) * 4;
            map[idx].offset = absOffset - prevOffset;
            prevOffset = absOffset;
        }
    }

    emit mapReady(std::move(map));
}
