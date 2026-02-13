#include "avsmosaic.h"
#include "avsframebuffer.h"
#include "avsaudiodata.h"
#include <algorithm>

AvsMosaic::AvsMosaic(int blockSize, bool beatReactive)
    : blockSize(blockSize)
    , beatReactive(beatReactive)
{
}

void AvsMosaic::render(AvsFramebuffer &fb, const AvsAudioData &audio)
{
    int w = fb.width();
    int h = fb.height();
    uint32_t *px = fb.pixels();

    int bs = blockSize;
    if (beatReactive && audio.isBeat)
        bs *= 2;
    bs = std::max(bs, 2);

    for (int by = 0; by < h; by += bs) {
        for (int bx = 0; bx < w; bx += bs) {
            int bw = std::min(bs, w - bx);
            int bh = std::min(bs, h - by);
            int count = bw * bh;

            // Compute average color of the block
            uint32_t sumR = 0, sumG = 0, sumB = 0;
            for (int y = by; y < by + bh; ++y) {
                for (int x = bx; x < bx + bw; ++x) {
                    uint32_t c = px[y * w + x];
                    sumR += (c >> 16) & 0xFF;
                    sumG += (c >> 8) & 0xFF;
                    sumB += c & 0xFF;
                }
            }

            uint32_t avg = 0xFF000000
                | ((sumR / count) << 16)
                | ((sumG / count) << 8)
                | (sumB / count);

            // Fill block with average
            for (int y = by; y < by + bh; ++y) {
                for (int x = bx; x < bx + bw; ++x) {
                    px[y * w + x] = avg;
                }
            }
        }
    }
}
