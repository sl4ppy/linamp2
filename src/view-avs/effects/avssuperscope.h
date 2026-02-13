#ifndef AVSSUPERSCOPE_H
#define AVSSUPERSCOPE_H

#include "avseffect.h"
#include <cstdint>

class AvsSuperScope : public AvsEffect
{
public:
    enum DrawMode { Points, Lines };
    enum ShapePreset { Oscilloscope, Circle, Spiral, SpectrumBars };
    enum SourceType { Waveform, Spectrum };

    AvsSuperScope();

    void render(AvsFramebuffer &fb, const AvsAudioData &audio) override;
    QString name() const override { return "SuperScope"; }

    DrawMode drawMode = Lines;
    ShapePreset shapePreset = Oscilloscope;
    SourceType sourceType = Waveform;
    uint32_t color = 0xFF00FFFF; // cyan
    float scaleY = 0.8f;

private:
    void drawLine(AvsFramebuffer &fb, int x0, int y0, int x1, int y1, uint32_t col);
    void setPixel(AvsFramebuffer &fb, int x, int y, uint32_t col);
};

#endif // AVSSUPERSCOPE_H
