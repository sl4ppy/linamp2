#ifndef AVSFRAMEBUFFER_H
#define AVSFRAMEBUFFER_H

#include <QImage>
#include <cstdint>
#include <vector>

#define AVS_FB_WIDTH 320
#define AVS_FB_HEIGHT 100

class AvsFramebuffer
{
public:
    AvsFramebuffer(int width, int height);

    uint32_t *pixels();
    const uint32_t *pixels() const;
    int width() const;
    int height() const;
    int pixelCount() const;

    void clear(uint32_t color = 0xFF000000);
    void copyFrom(const AvsFramebuffer &other);
    QImage toImage() const;

private:
    int m_width;
    int m_height;
    std::vector<uint32_t> m_pixels;
};

#endif // AVSFRAMEBUFFER_H
