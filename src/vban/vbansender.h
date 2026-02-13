#ifndef VBANSENDER_H
#define VBANSENDER_H

#include <QObject>
#include <QTimer>
#include <QUdpSocket>
#include <QMutex>
#include <QSettings>
#include <QtConcurrent>
#include <spa/param/audio/format-utils.h>
#include <pipewire/pipewire.h>

// VBAN protocol constants
#define VBAN_HEADER_SIZE 28
#define VBAN_SAMPLES_PER_PACKET 256
#define VBAN_CHANNELS 2
#define VBAN_BYTES_PER_SAMPLE 2
#define VBAN_PACKET_DATA_SIZE (VBAN_SAMPLES_PER_PACKET * VBAN_CHANNELS * VBAN_BYTES_PER_SAMPLE)
#define VBAN_SEND_INTERVAL_MS 5
#define VBAN_BUFFER_MAX_SIZE (1024 * 1024) // 1MB max buffer

// VBAN sample rate index for 44100Hz
#define VBAN_SR_44100 0x04

#pragma pack(push, 1)
struct VbanHeader {
    char vban[4];          // "VBAN"
    uint8_t format_SR;     // Sample rate index | (protocol << 5)
    uint8_t format_nbs;    // Samples per packet - 1
    uint8_t format_nbc;    // Channels - 1
    uint8_t format_bit;    // Bit format | (codec << 4)
    char streamname[16];   // Null-padded stream name
    uint32_t nuFrame;      // Incrementing frame counter
};
#pragma pack(pop)

struct VbanPwData {
    struct pw_main_loop *loop;
    struct pw_stream *stream;
    struct spa_audio_info format;
    unsigned move:1;

    QMutex *bufferMutex;
    QByteArray *buffer;
};

class VbanSender : public QObject
{
    Q_OBJECT
public:
    explicit VbanSender(QObject *parent = nullptr);
    ~VbanSender();

    void start();
    void stop();
    void setEnabled(bool enabled);
    bool isEnabled() const;

signals:
    void enabledChanged(bool enabled);

private:
    void pwLoop();
    void sendPackets();

    bool m_enabled = false;
    QString m_destinationIp;
    int m_port;
    QString m_streamName;

    QUdpSocket *m_socket = nullptr;
    QTimer *m_sendTimer = nullptr;
    uint32_t m_frameCounter = 0;

    VbanPwData m_pwData;
    QFuture<void> m_pwLoopThread;

    void loadSettings();
    void saveSettings();
};

#endif // VBANSENDER_H
