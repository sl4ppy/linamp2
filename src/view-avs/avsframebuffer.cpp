#include "avsframebuffer.h"
#include <cstring>

AvsFramebuffer::AvsFramebuffer(int width, int height)
    : m_width(width), m_height(height), m_pixels(width * height, 0xFF000000)
{
}

uint32_t *AvsFramebuffer::pixels()
{
    return m_pixels.data();
}

const uint32_t *AvsFramebuffer::pixels() const
{
    return m_pixels.data();
}

int AvsFramebuffer::width() const
{
    return m_width;
}

int AvsFramebuffer::height() const
{
    return m_height;
}

int AvsFramebuffer::pixelCount() const
{
    return m_width * m_height;
}

void AvsFramebuffer::clear(uint32_t color)
{
    std::fill(m_pixels.begin(), m_pixels.end(), color);
}

void AvsFramebuffer::copyFrom(const AvsFramebuffer &other)
{
    if (m_width == other.m_width && m_height == other.m_height) {
        std::memcpy(m_pixels.data(), other.m_pixels.data(), m_pixels.size() * sizeof(uint32_t));
    }
}

QImage AvsFramebuffer::toImage() const
{
    return QImage(reinterpret_cast<const uchar *>(m_pixels.data()),
                  m_width, m_height, m_width * sizeof(uint32_t),
                  QImage::Format_ARGB32);
}
