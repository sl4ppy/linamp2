#include "avsclock.h"
#include "avsframebuffer.h"
#include "avsaudiodata.h"
#include <QTime>

// 5x7 pixel font for digits 0-9 and colon
// Each row is a uint8_t; bit 4 = leftmost pixel, bit 0 = rightmost
const uint8_t AvsClock::FONT_5x7[11][7] = {
    {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}, // 0
    {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}, // 1
    {0x0E, 0x11, 0x01, 0x06, 0x08, 0x10, 0x1F}, // 2
    {0x0E, 0x11, 0x01, 0x06, 0x01, 0x11, 0x0E}, // 3
    {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}, // 4
    {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E}, // 5
    {0x0E, 0x10, 0x10, 0x1E, 0x11, 0x11, 0x0E}, // 6
    {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}, // 7
    {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}, // 8
    {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}, // 9
    {0x00, 0x00, 0x04, 0x00, 0x04, 0x00, 0x00}, // : (colon)
};

AvsClock::AvsClock(uint32_t color, int scale, bool blinkColon)
    : color(color), scale(scale), blinkColon(blinkColon)
{
}

void AvsClock::drawGlyph(AvsFramebuffer &fb, int glyphIndex, int x, int y, uint32_t drawColor, int drawScale)
{
    if (glyphIndex < 0 || glyphIndex > 10) return;

    const uint8_t *glyph = FONT_5x7[glyphIndex];
    uint32_t *pixels = fb.pixels();
    int w = fb.width();
    int h = fb.height();

    for (int row = 0; row < 7; ++row) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < 5; ++col) {
            if (bits & (0x10 >> col)) {
                for (int sy = 0; sy < drawScale; ++sy) {
                    for (int sx = 0; sx < drawScale; ++sx) {
                        int px = x + col * drawScale + sx;
                        int py = y + row * drawScale + sy;
                        if (px >= 0 && px < w && py >= 0 && py < h)
                            pixels[py * w + px] = drawColor;
                    }
                }
            }
        }
    }
}

void AvsClock::render(AvsFramebuffer &fb, const AvsAudioData &audio)
{
    QTime now = QTime::currentTime();

    int hour = now.hour() % 12;
    if (hour == 0) hour = 12;
    int minute = now.minute();

    // Glyph indices for "H:MM" or "HH:MM" (12-hour, no leading zero)
    // -1 means skip (no leading zero for hours < 10)
    int glyphs[5] = {
        hour >= 10 ? hour / 10 : -1,
        hour % 10,
        10, // colon
        minute / 10, minute % 10
    };

    // Blinking colon: visible for first 500ms of each second
    bool showColon = !blinkColon || now.msec() < 500;

    // Beat pulse: bump scale +1 on beat, flash color toward white
    float beat = audio.beatDecay;
    int effectiveScale = scale + (beat > 0.4f ? 1 : 0);

    uint32_t r = (color >> 16) & 0xFF;
    uint32_t g = (color >> 8) & 0xFF;
    uint32_t b = color & 0xFF;
    r += static_cast<uint32_t>(beat * (255 - r));
    g += static_cast<uint32_t>(beat * (255 - g));
    b += static_cast<uint32_t>(beat * (255 - b));
    uint32_t pulseColor = 0xFF000000 | (r << 16) | (g << 8) | b;

    // Layout using effective scale
    int charW = 5 * effectiveScale;
    int charH = 7 * effectiveScale;
    int gap = effectiveScale;
    int numChars = (glyphs[0] >= 0) ? 5 : 4;
    int totalW = numChars * charW + (numChars - 1) * gap;

    int startX = (fb.width() - totalW) / 2;
    int startY = (fb.height() - charH) / 2;

    int x = startX;
    for (int i = 0; i < 5; ++i) {
        if (glyphs[i] < 0)
            continue; // skip leading zero slot

        if (i == 2 && !showColon) {
            x += charW + gap;
            continue;
        }

        // Drop shadow
        drawGlyph(fb, glyphs[i], x + 1, startY + 1, 0xFF000000, effectiveScale);
        // Main text
        drawGlyph(fb, glyphs[i], x, startY, pulseColor, effectiveScale);

        x += charW + gap;
    }
}
