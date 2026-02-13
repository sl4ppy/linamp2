#include "avsonbeatclear.h"
#include "avsframebuffer.h"
#include "avsaudiodata.h"

AvsOnBeatClear::AvsOnBeatClear(Mode mode, uint32_t flashColor)
    : m_mode(mode), m_flashColor(flashColor)
{
}

void AvsOnBeatClear::render(AvsFramebuffer &fb, const AvsAudioData &audio)
{
    if (!audio.isBeat)
        return;

    switch (m_mode) {
    case ClearBlack:
        fb.clear(0xFF000000);
        break;
    case FlashWhite:
        fb.clear(0xFFFFFFFF);
        break;
    case FlashColor:
        fb.clear(m_flashColor);
        break;
    case RandomColor: {
        // Simple xorshift for random bright color
        m_rngState ^= m_rngState << 13;
        m_rngState ^= m_rngState >> 17;
        m_rngState ^= m_rngState << 5;
        uint32_t color = 0xFF000000 | (m_rngState & 0x00FFFFFF);
        fb.clear(color);
        break;
    }
    }
}
