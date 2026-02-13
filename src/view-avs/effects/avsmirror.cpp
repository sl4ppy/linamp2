#include "avsmirror.h"
#include "avsframebuffer.h"

AvsMirror::AvsMirror(Mode mode)
    : m_mode(mode)
{
}

void AvsMirror::render(AvsFramebuffer &fb, const AvsAudioData &)
{
    uint32_t *px = fb.pixels();
    int w = fb.width();
    int h = fb.height();

    if (m_mode == Horizontal || m_mode == Both) {
        // Copy left half to right half (mirrored)
        int halfW = w / 2;
        for (int y = 0; y < h; y++) {
            uint32_t *row = px + y * w;
            for (int x = 0; x < halfW; x++) {
                row[w - 1 - x] = row[x];
            }
        }
    }

    if (m_mode == Vertical || m_mode == Both) {
        // Copy top half to bottom half (mirrored)
        int halfH = h / 2;
        for (int y = 0; y < halfH; y++) {
            uint32_t *srcRow = px + y * w;
            uint32_t *dstRow = px + (h - 1 - y) * w;
            for (int x = 0; x < w; x++) {
                dstRow[x] = srcRow[x];
            }
        }
    }
}
