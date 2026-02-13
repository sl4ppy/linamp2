#include "vbansender.h"
#include <QHostAddress>
#include <QDebug>

//#define DEBUG_VBAN

#define VBAN_SAMPLE_RATE 44100
#define VBAN_PW_CHANNELS 2

bool globalVbanSenderInstanceIsRunning = false;

static void vban_on_process(void *userdata)
{
    VbanPwData *data = (VbanPwData *)userdata;
    struct pw_buffer *b;
    struct spa_buffer *buf;
    int16_t *samples;
    uint32_t n_bytes;

    QMutexLocker l(data->bufferMutex);

    if (data->stream == nullptr || data->loop == nullptr) {
        return;
    }

    if ((b = pw_stream_dequeue_buffer(data->stream)) == NULL) {
        pw_log_warn("out of buffers: %m");
        return;
    }

    buf = b->buffer;
    if ((samples = (int16_t *)buf->datas[0].data) == NULL) {
        pw_stream_queue_buffer(data->stream, b);
        return;
    }

    n_bytes = buf->datas[0].chunk->size;

    if (data->buffer == nullptr) {
        pw_stream_queue_buffer(data->stream, b);
        return;
    }

    // Buffer overflow protection: discard oldest data if buffer exceeds max
    if (data->buffer->size() + (int)n_bytes > VBAN_BUFFER_MAX_SIZE) {
        int excess = data->buffer->size() + n_bytes - VBAN_BUFFER_MAX_SIZE;
        data->buffer->remove(0, excess);
    }

    data->buffer->append((const char *)samples, n_bytes);

    pw_stream_queue_buffer(data->stream, b);
}

static void vban_on_stream_param_changed(void *_data, uint32_t id, const struct spa_pod *param)
{
    VbanPwData *data = (VbanPwData *)_data;

    if (param == NULL || id != SPA_PARAM_Format)
        return;

    if (spa_format_parse(param, &data->format.media_type, &data->format.media_subtype) < 0)
        return;

    if (data->format.media_type != SPA_MEDIA_TYPE_audio ||
        data->format.media_subtype != SPA_MEDIA_SUBTYPE_raw)
        return;

    spa_format_audio_raw_parse(param, &data->format.info.raw);
}

static const struct pw_stream_events vban_stream_events = {
    PW_VERSION_STREAM_EVENTS,
    .param_changed = vban_on_stream_param_changed,
    .process = vban_on_process
};

static void vban_do_quit(void *userdata, int)
{
    VbanPwData *data = (VbanPwData *)userdata;
    if (data == nullptr || data->loop == nullptr) return;
    pw_main_loop_quit(data->loop);
}

VbanSender::VbanSender(QObject *parent)
    : QObject(parent)
{
    // Initialize PipeWire data
    m_pwData.format.media_type = SPA_MEDIA_TYPE_audio;
    m_pwData.format.media_subtype = SPA_MEDIA_SUBTYPE_raw;
    m_pwData.loop = nullptr;
    m_pwData.move = false;
    m_pwData.buffer = nullptr;
    m_pwData.bufferMutex = new QMutex();
    m_pwData.stream = nullptr;

    m_sendTimer = new QTimer(this);
    m_sendTimer->setInterval(VBAN_SEND_INTERVAL_MS);
    connect(m_sendTimer, &QTimer::timeout, this, &VbanSender::sendPackets);

    loadSettings();

    // Auto-start if previously enabled
    if (m_enabled) {
        start();
    }
}

VbanSender::~VbanSender()
{
    stop();
    delete m_pwData.bufferMutex;
}

void VbanSender::loadSettings()
{
    QSettings settings;
    m_enabled = settings.value("vban/enabled", false).toBool();
    m_destinationIp = settings.value("vban/destinationIp", "255.255.255.255").toString();
    m_port = settings.value("vban/port", 6980).toInt();
    m_streamName = settings.value("vban/streamName", "Linamp").toString();
}

void VbanSender::saveSettings()
{
    QSettings settings;
    settings.setValue("vban/enabled", m_enabled);
}

void VbanSender::start()
{
    #ifdef DEBUG_VBAN
    qDebug() << "VbanSender::start()";
    #endif

    if (m_pwLoopThread.isRunning()) {
        #ifdef DEBUG_VBAN
        qDebug() << "VbanSender: already running";
        #endif
        return;
    }

    // Create UDP socket
    if (m_socket == nullptr) {
        m_socket = new QUdpSocket(this);
    }

    // Create buffer
    {
        QMutexLocker l(m_pwData.bufferMutex);
        if (m_pwData.buffer != nullptr) {
            delete m_pwData.buffer;
        }
        m_pwData.buffer = new QByteArray();
    }

    m_frameCounter = 0;

    // Start PipeWire capture thread
    m_pwLoopThread = QtConcurrent::run(&VbanSender::pwLoop, this);

    // Start send timer
    m_sendTimer->start();
}

void VbanSender::stop()
{
    #ifdef DEBUG_VBAN
    qDebug() << "VbanSender::stop()";
    #endif

    m_sendTimer->stop();

    if (m_pwLoopThread.isRunning()) {
        vban_do_quit(&m_pwData, 1);
    }

    QMutexLocker l(m_pwData.bufferMutex);
    if (m_pwData.buffer != nullptr) {
        delete m_pwData.buffer;
        m_pwData.buffer = nullptr;
    }
}

void VbanSender::setEnabled(bool enabled)
{
    if (m_enabled == enabled) return;

    m_enabled = enabled;
    saveSettings();

    if (m_enabled) {
        start();
    } else {
        stop();
    }

    emit enabledChanged(m_enabled);
}

bool VbanSender::isEnabled() const
{
    return m_enabled;
}

void VbanSender::pwLoop()
{
    if (globalVbanSenderInstanceIsRunning) {
        #ifdef DEBUG_VBAN
        qDebug() << "VbanSender: another instance is already running";
        #endif
        return;
    }

    globalVbanSenderInstanceIsRunning = true;

    const struct spa_pod *params[1];
    uint8_t buffer[1024];
    struct pw_properties *props;
    struct spa_pod_builder b;
    b.data = buffer;
    b.size = sizeof(buffer);
    b.callbacks.data = nullptr;
    b.callbacks.funcs = nullptr;
    b.state.flags = 0;
    b.state.frame = nullptr;
    b.state.offset = 0;
    b._padding = 0;

    pw_init(nullptr, nullptr);

    m_pwData.loop = pw_main_loop_new(NULL);

    pw_loop_add_signal(pw_main_loop_get_loop(m_pwData.loop), SIGINT, vban_do_quit, &m_pwData);
    pw_loop_add_signal(pw_main_loop_get_loop(m_pwData.loop), SIGTERM, vban_do_quit, &m_pwData);

    props = pw_properties_new(PW_KEY_MEDIA_TYPE, "Audio",
                              PW_KEY_CONFIG_NAME, "client-rt.conf",
                              PW_KEY_MEDIA_CATEGORY, "Capture",
                              PW_KEY_MEDIA_ROLE, "Music",
                              NULL);
    pw_properties_set(props, PW_KEY_STREAM_CAPTURE_SINK, "true");

    m_pwData.stream = pw_stream_new_simple(
        pw_main_loop_get_loop(m_pwData.loop),
        "vban-capture",
        props,
        &vban_stream_events,
        &m_pwData);

    struct spa_audio_info_raw audio_info;
    audio_info.format = SPA_AUDIO_FORMAT_S16_LE; // Interleaved S16 for VBAN
    audio_info.channels = VBAN_PW_CHANNELS;
    audio_info.rate = VBAN_SAMPLE_RATE;
    params[0] = spa_format_audio_raw_build(&b, SPA_PARAM_EnumFormat, &audio_info);

    pw_stream_connect(m_pwData.stream,
                      PW_DIRECTION_INPUT,
                      PW_ID_ANY,
                      (pw_stream_flags)(PW_STREAM_FLAG_AUTOCONNECT |
                                        PW_STREAM_FLAG_MAP_BUFFERS |
                                        PW_STREAM_FLAG_RT_PROCESS),
                      params, 1);

    pw_main_loop_run(m_pwData.loop);

    pw_stream_destroy(m_pwData.stream);
    pw_main_loop_destroy(m_pwData.loop);
    pw_deinit();
    m_pwData.stream = nullptr;
    m_pwData.loop = nullptr;

    globalVbanSenderInstanceIsRunning = false;
}

void VbanSender::sendPackets()
{
    QMutexLocker l(m_pwData.bufferMutex);

    if (m_pwData.buffer == nullptr || m_socket == nullptr) {
        return;
    }

    QHostAddress destAddr(m_destinationIp);

    // Send as many complete packets as we have data for
    while (m_pwData.buffer->size() >= VBAN_PACKET_DATA_SIZE) {
        // Build VBAN header
        VbanHeader header;
        memcpy(header.vban, "VBAN", 4);
        header.format_SR = VBAN_SR_44100; // SR index 4, protocol 0 (audio)
        header.format_nbs = VBAN_SAMPLES_PER_PACKET - 1; // 255
        header.format_nbc = VBAN_CHANNELS - 1; // 1
        header.format_bit = 0x01; // Int16 = 1, codec PCM = 0
        memset(header.streamname, 0, sizeof(header.streamname));
        QByteArray nameBytes = m_streamName.toUtf8().left(15);
        memcpy(header.streamname, nameBytes.constData(), nameBytes.size());
        header.nuFrame = m_frameCounter++;

        // Build packet: header + audio data
        QByteArray packet;
        packet.reserve(VBAN_HEADER_SIZE + VBAN_PACKET_DATA_SIZE);
        packet.append((const char *)&header, VBAN_HEADER_SIZE);
        packet.append(m_pwData.buffer->constData(), VBAN_PACKET_DATA_SIZE);

        // Remove consumed data from buffer
        m_pwData.buffer->remove(0, VBAN_PACKET_DATA_SIZE);

        // Send packet
        m_socket->writeDatagram(packet, destAddr, m_port);
    }
}
