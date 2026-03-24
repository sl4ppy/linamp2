#ifndef WARPPARAMS_H
#define WARPPARAMS_H

#include <cstdint>
#include <cmath>
#include <cstdlib>

static constexpr int FB_W = 320;
static constexpr int FB_H = 100;
static constexpr int FB_PIXELS = FB_W * FB_H;
static constexpr uint8_t WEIGHT_SUM = 253;
static constexpr float ASPECT_COMPENSATION = (float)FB_W / FB_H / (4.0f / 3.0f);

struct WarpEntry {
    uint8_t w[4];    // Bilinear weights: TL, TR, BL, BR (sum ~ 253)
    int32_t offset;  // Relative byte offset from previous entry's source pixel
};

enum class WarpMode {
    InwardZoom = 0,
    OutwardZoom,
    Sphere,
    Ripples,
    Vortex,
    Perspective,
    Terra,
    Fuzzy,
    Flower,
    SpinBlur,
    HTunnel,
    VTunnel,
    BlackHole,
    PetalRings,
    CrystalBall,
    COUNT
};

static constexpr int NUM_WARP_MODES = static_cast<int>(WarpMode::COUNT);

struct WarpParams {
    WarpMode mode = WarpMode::InwardZoom;
    float centerX = FB_W / 2.0f;
    float centerY = FB_H / 2.0f;
    float turn = 0.015f;
    float cosT = 1.0f;
    float sinT = 0.0f;
    float scale = 0.96f;
    float damping = 0.9f;
    // Mode-specific parameters
    float f1 = 0.0f;
    float f2 = 0.0f;
    float f3 = 0.0f;
    float f4 = 0.0f;

    void computeTrig() {
        cosT = cosf(turn);
        sinT = sinf(turn);
    }

    static float randf() {
        return (rand() % 10000) / 10000.0f;
    }

    static WarpParams randomize() {
        WarpParams p;
        p.mode = static_cast<WarpMode>(rand() % NUM_WARP_MODES);
        p.centerX = FB_W / 2.0f + (rand() % 61) - 30;
        p.centerY = FB_H / 2.0f + (rand() % 11) - 5;
        p.damping = 0.6f + randf() * 0.35f;

        switch (p.mode) {
        case WarpMode::InwardZoom:
            p.scale = 0.88f + randf() * 0.10f;
            p.turn = 0.01f + randf() * 0.01f;
            if (rand() % 2) p.turn = -p.turn;
            break;
        case WarpMode::OutwardZoom:
            p.scale = 1.005f + randf() * 0.015f;
            p.turn = 0.02f + randf() * 0.05f;
            if (rand() % 2) p.turn = -p.turn;
            break;
        case WarpMode::Sphere:
            p.scale = 0.93f;
            p.turn = 0.007f + randf() * 0.02f;
            if (rand() % 2) p.turn = -p.turn;
            p.f1 = 0.00035f;
            break;
        case WarpMode::Ripples:
            p.scale = 0.90f;
            p.turn = randf() * 0.05f;
            if (rand() % 2) p.turn = -p.turn;
            p.f1 = 3.0f + randf() * 5.0f;
            break;
        case WarpMode::Vortex:
            p.scale = 1.02f;
            p.turn = 0.007f + randf() * 0.02f;
            if (rand() % 2) p.turn = -p.turn;
            p.f1 = 0.25f;
            break;
        case WarpMode::Perspective:
            p.scale = 0.97f;
            p.turn = 0.01f + randf() * 0.03f;
            if (rand() % 2) p.turn = -p.turn;
            p.f1 = 0.05f + randf() * 0.07f;
            p.f2 = 0.97f + randf() * 0.02f;
            break;
        case WarpMode::Terra:
            p.scale = 0.90f;
            p.turn = 0.01f + randf() * 0.005f;
            if (rand() % 2) p.turn = -p.turn;
            p.f1 = 0.0005f;
            break;
        case WarpMode::Fuzzy:
            p.scale = 0.92f;
            p.turn = 0.01f + randf() * 0.01f;
            if (rand() % 2) p.turn = -p.turn;
            p.f1 = 0.92f + randf() * 0.01f;
            p.f2 = 0.0006f + randf() * 0.0005f;
            break;
        case WarpMode::Flower:
            p.scale = 0.90f + randf() * 0.15f;
            p.turn = 0.01f + randf() * 0.02f;
            if (rand() % 2) p.turn = -p.turn;
            break;
        case WarpMode::SpinBlur:
            p.scale = 1.008f + randf() * 0.008f;
            p.turn = 0.12f + randf() * 0.06f;
            if (rand() % 2) p.turn = -p.turn;
            break;
        case WarpMode::HTunnel:
            p.scale = 0.97f;
            p.turn = 0.007f + randf() * 0.02f;
            if (rand() % 2) p.turn = -p.turn;
            p.f1 = 0.40f;
            break;
        case WarpMode::VTunnel:
            p.scale = 0.97f;
            p.turn = 0.007f + randf() * 0.02f;
            if (rand() % 2) p.turn = -p.turn;
            p.f1 = 0.40f;
            break;
        case WarpMode::BlackHole:
            p.scale = 1.04f;
            p.turn = 0.007f + randf() * 0.02f;
            if (rand() % 2) p.turn = -p.turn;
            p.f1 = 0.92f + randf() * 0.16f;
            break;
        case WarpMode::PetalRings:
            p.scale = 0.94f;
            p.turn = 0.04f + randf() * 0.04f;
            if (rand() % 2) p.turn = -p.turn;
            p.f1 = 2.0f + (rand() % 5);
            break;
        case WarpMode::CrystalBall:
            p.scale = 0.95f;
            p.turn = 0.007f + randf() * 0.02f;
            if (rand() % 2) p.turn = -p.turn;
            break;
        default:
            break;
        }

        p.computeTrig();
        return p;
    }
};

#endif // WARPPARAMS_H
