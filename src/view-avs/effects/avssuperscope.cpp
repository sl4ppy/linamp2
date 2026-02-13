#include "avssuperscope.h"
#include "avsframebuffer.h"
#include "avsaudiodata.h"
#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

AvsSuperScope::AvsSuperScope()
{
}

void AvsSuperScope::setPixel(AvsFramebuffer &fb, int x, int y, uint32_t col)
{
    if (x >= 0 && x < fb.width() && y >= 0 && y < fb.height()) {
        fb.pixels()[y * fb.width() + x] = col;
    }
}

void AvsSuperScope::drawLine(AvsFramebuffer &fb, int x0, int y0, int x1, int y1, uint32_t col)
{
    // Bresenham's line algorithm
    int dx = std::abs(x1 - x0);
    int dy = -std::abs(y1 - y0);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx + dy;

    for (;;) {
        setPixel(fb, x0, y0, col);
        if (x0 == x1 && y0 == y1)
            break;
        int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void AvsSuperScope::render(AvsFramebuffer &fb, const AvsAudioData &audio)
{
    int w = fb.width();
    int h = fb.height();
    int numPoints = AVS_WAVEFORM_SIZE;

    const float *source;
    int sourceSize;
    if (sourceType == Spectrum) {
        source = audio.spectrumMono;
        sourceSize = AVS_SPECTRUM_SIZE;
    } else {
        source = audio.waveformMono;
        sourceSize = AVS_WAVEFORM_SIZE;
    }

    int prevPx = -1, prevPy = -1;
    float n = static_cast<float>(numPoints);

    for (int i = 0; i < numPoints; i++) {
        float t = static_cast<float>(i) / n;
        int srcIdx = std::min(i, sourceSize - 1);
        float val = source[srcIdx];
        float x, y;

        switch (shapePreset) {
        case Oscilloscope:
            x = t * 2.0f - 1.0f;
            y = val * scaleY;
            break;

        case Circle: {
            float angle = t * 2.0f * M_PI;
            float r = 0.5f + val * scaleY * 0.3f;
            x = cosf(angle) * r;
            y = sinf(angle) * r;
            break;
        }

        case Spiral: {
            float r = t + val * scaleY * 0.2f;
            float angle = r * 4.0f * M_PI;
            x = cosf(angle) * r;
            y = sinf(angle) * r;
            break;
        }

        case SpectrumBars:
            x = t * 2.0f - 1.0f;
            y = -val * scaleY;
            break;
        }

        // Map normalized coords [-1,1] to pixel coords
        int px = static_cast<int>((x + 1.0f) * 0.5f * w);
        int py = static_cast<int>((y + 1.0f) * 0.5f * h);

        px = std::clamp(px, 0, w - 1);
        py = std::clamp(py, 0, h - 1);

        if (drawMode == Lines && i > 0 && prevPx >= 0) {
            drawLine(fb, prevPx, prevPy, px, py, color);
        } else {
            setPixel(fb, px, py, color);
        }

        prevPx = px;
        prevPy = py;
    }
}
