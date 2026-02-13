#include "avsclearscreen.h"
#include "avsframebuffer.h"

AvsClearScreen::AvsClearScreen(uint32_t color)
    : m_color(color)
{
}

void AvsClearScreen::render(AvsFramebuffer &fb, const AvsAudioData &)
{
    fb.clear(m_color);
}
