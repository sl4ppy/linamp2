#ifndef AUDIOANALYZER_H
#define AUDIOANALYZER_H

#include <QByteArray>
#include <QAudioFormat>

#include "fft.h"

constexpr int WAVEFORM_SIZE = 512;
constexpr int SPECTRUM_SIZE = 256;
constexpr int VOL_HISTORY_SIZE = 120;

class AudioAnalyzer
{
public:
    AudioAnalyzer();

    void process(const QByteArray& pcm, const QAudioFormat& format);

    const float* waveformL() const;
    const float* waveformR() const;
    const float* smoothWaveL() const;
    const float* smoothWaveR() const;
    int waveformSize() const;

    const float* spectrum() const;
    float bassEnergy() const;
    float midEnergy() const;
    float trebleEnergy() const;

    float currentVol() const;
    float avgVol() const;
    float avgVolWide() const;

    bool isBeatMode() const;
    bool isBigBeat() const;
    bool hasSoundData() const;

private:
    void detectBeat();

    float m_waveL[WAVEFORM_SIZE];
    float m_waveR[WAVEFORM_SIZE];
    float m_smoothWaveL[WAVEFORM_SIZE];
    float m_smoothWaveR[WAVEFORM_SIZE];

    float m_spectrum[SPECTRUM_SIZE];

    float m_bassEnergy;
    float m_midEnergy;
    float m_trebleEnergy;

    float m_currentVol;
    float m_avgVol;
    float m_avgVolWide;

    float m_volHistory[VOL_HISTORY_SIZE];
    int m_volHistoryPos;

    bool m_beatMode;
    bool m_bigBeat;
    float m_beatThreshold;

    int m_waveformSize;
    bool m_hasSoundData;
    int m_silenceFrames;
};

#endif // AUDIOANALYZER_H
