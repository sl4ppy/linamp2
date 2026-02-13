#ifndef AVSAUDIODATA_H
#define AVSAUDIODATA_H

#include <QByteArray>
#include <QAudioFormat>

#define AVS_WAVEFORM_SIZE 576
#define AVS_SPECTRUM_SIZE 256
#define AVS_BEAT_HISTORY_SIZE 10
#define AVS_BEAT_THRESHOLD 1.5f
#define AVS_BEAT_COOLDOWN_FRAMES 6

class AvsAudioData
{
public:
    AvsAudioData();

    void processFromPcm(const QByteArray &data, QAudioFormat format);

    float waveformLeft[AVS_WAVEFORM_SIZE];
    float waveformRight[AVS_WAVEFORM_SIZE];
    float waveformMono[AVS_WAVEFORM_SIZE];
    float spectrumMono[AVS_SPECTRUM_SIZE];
    bool isBeat;
    float beatDecay;

    // Subband beat detection
    bool isBeatBass;
    bool isBeatMid;
    bool isBeatHigh;
    float beatDecayBass;
    float beatDecayMid;
    float beatDecayHigh;

private:
    float m_bassHistory[AVS_BEAT_HISTORY_SIZE];
    int m_bassHistoryPos;
    int m_beatCooldown;
    float m_beatDecayValue;

    float m_midHistory[AVS_BEAT_HISTORY_SIZE];
    int m_midHistoryPos;
    int m_beatCooldownMid;
    float m_beatDecayMid;

    float m_highHistory[AVS_BEAT_HISTORY_SIZE];
    int m_highHistoryPos;
    int m_beatCooldownHigh;
    float m_beatDecayHigh;
};

#endif // AVSAUDIODATA_H
