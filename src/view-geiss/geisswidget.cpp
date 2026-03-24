#include "geisswidget.h"

#include <QPainter>
#include <QMouseEvent>
#include <QKeyEvent>
#include <cstring>

GeissWidget::GeissWidget(QWidget *parent)
    : QWidget(parent)
{
    qRegisterMetaType<std::vector<WarpEntry>>("std::vector<WarpEntry>");
    setFocusPolicy(Qt::StrongFocus);

    // Initialize framebuffers
    m_fb[0] = QImage(FB_W, FB_H, QImage::Format_RGB32);
    m_fb[1] = QImage(FB_W, FB_H, QImage::Format_RGB32);
    m_fb[0].fill(Qt::black);
    m_fb[1].fill(Qt::black);

    // Initialize color state
    m_colorState.randomize();

    // Create initial warp map (simple inward zoom + slight rotation)
    initWarpMap();

    // Select initial effects
    m_effects.selectEffects(false);

    // Start background warp map generator
    m_mapGen = new WarpMapGenerator(this);
    connect(m_mapGen, &WarpMapGenerator::mapReady, this, &GeissWidget::onMapReady);
    startGeneratingNextMap();

    // Frame timer at ~30 FPS
    m_frameTimer = new QTimer(this);
    m_frameTimer->setInterval(33);
    connect(m_frameTimer, &QTimer::timeout, this, &GeissWidget::onFrameTick);
    m_frameTimer->start();
}

GeissWidget::~GeissWidget()
{
    if (m_mapGen->isRunning()) {
        m_mapGen->quit();
        m_mapGen->wait(1000);
    }
}

void GeissWidget::feedAudio(const QByteArray& data, QAudioFormat format)
{
    m_audio.process(data, format);
}

void GeissWidget::initWarpMap()
{
    // Generate default inward-zoom + rotation map on the main thread
    m_currentParams = WarpParams();
    m_currentParams.mode = WarpMode::InwardZoom;
    m_currentParams.scale = 0.95f;
    m_currentParams.turn = 0.015f;
    m_currentParams.damping = 0.85f;
    m_currentParams.centerX = FB_W / 2.0f;
    m_currentParams.centerY = FB_H / 2.0f;
    m_currentParams.computeTrig();

    // Build the map synchronously for startup
    m_warpMap.resize(FB_PIXELS);
    int prevOffset = 0;

    for (int y = 0; y < FB_H; y++) {
        for (int x = 0; x < FB_W; x++) {
            float dx = x - m_currentParams.centerX;
            float dy = (y - m_currentParams.centerY) * ASPECT_COMPENSATION;

            float nx = dx * m_currentParams.cosT - dy * m_currentParams.sinT;
            float ny = dx * m_currentParams.sinT + dy * m_currentParams.cosT;

            float srcX = nx * m_currentParams.scale + m_currentParams.centerX;
            float srcY = ny * m_currentParams.scale / ASPECT_COMPENSATION + m_currentParams.centerY;

            srcX = x * (1.0f - m_currentParams.damping) + srcX * m_currentParams.damping;
            srcY = y * (1.0f - m_currentParams.damping) + srcY * m_currentParams.damping;

            srcX = std::clamp(srcX, 1.0f, (float)(FB_W - 2));
            srcY = std::clamp(srcY, 1.0f, (float)(FB_H - 2));

            int ix = (int)srcX;
            int iy = (int)srcY;
            float fx = srcX - ix;
            float fy = srcY - iy;

            int idx = y * FB_W + x;
            m_warpMap[idx].w[0] = (uint8_t)((1 - fx) * (1 - fy) * WEIGHT_SUM);
            m_warpMap[idx].w[1] = (uint8_t)(fx * (1 - fy) * WEIGHT_SUM);
            m_warpMap[idx].w[2] = (uint8_t)((1 - fx) * fy * WEIGHT_SUM);
            m_warpMap[idx].w[3] = (uint8_t)(fx * fy * WEIGHT_SUM);

            int absOffset = (iy * FB_W + ix) * 4;
            m_warpMap[idx].offset = absOffset - prevOffset;
            prevOffset = absOffset;
        }
    }
}

void GeissWidget::startGeneratingNextMap()
{
    WarpParams nextParams = WarpParams::randomize();
    m_mapGen->generate(nextParams);
}

void GeissWidget::selectNewEffects()
{
    m_effects.selectEffects(m_audio.hasSoundData());
    m_colorState.randomize();
}

void GeissWidget::onMapReady(std::vector<WarpEntry> newMap)
{
    m_warpMapNext = std::move(newMap);
    m_nextMapReady = true;
}

void GeissWidget::onFrameTick()
{
    m_frame += 1.0f;
    m_framesSinceSwap++;

    // --- Warp map swap logic ---
    bool autoSwapDue = m_framesSinceSwap >= FRAMES_TIL_AUTO_SWITCH;
    if (m_nextMapReady &&
        (m_forceSwap || autoSwapDue || !m_audio.isBeatMode() || m_audio.isBigBeat())) {
        std::swap(m_warpMap, m_warpMapNext);
        m_nextMapReady = false;
        m_forceSwap = false;
        m_framesSinceSwap = 0;
        selectNewEffects();
        startGeneratingNextMap();
    }

    // --- Get framebuffer pointers ---
    int srcIdx = m_activeFB;
    int dstIdx = 1 - m_activeFB;

    uint32_t* srcFB = reinterpret_cast<uint32_t*>(m_fb[srcIdx].bits());
    uint32_t* dstFB = reinterpret_cast<uint32_t*>(m_fb[dstIdx].bits());

    // --- Phase 1: Render overlay effects into source FB (before warp) ---
    m_effects.renderOverlays(srcFB, FB_W, FB_H, m_audio, m_frame);

    // --- Phase 2: Diminish center to prevent accumulation ---
    m_warp.diminishCenter(srcFB, FB_W, FB_H,
                          (int)m_currentParams.centerX,
                          (int)m_currentParams.centerY, 0.92f);

    // --- Phase 3: Apply warp map (source → destination) ---
    if (!m_warpMap.empty()) {
        m_warp.warp(srcFB, dstFB, m_warpMap.data(), FB_PIXELS, FB_W);
    } else {
        std::memcpy(dstFB, srcFB, FB_W * FB_H * 4);
    }

    // --- Phase 4: Render waveform into destination FB (after warp) ---
    if (m_audio.hasSoundData()) {
        m_effects.renderWaveform(dstFB, FB_W, FB_H, m_audio, m_frame);
    }

    // --- Phase 5: Render nuclide/beat-reactive effects (post-warp) ---
    if (m_audio.hasSoundData()) {
        m_effects.renderPostWarp(dstFB, FB_W, FB_H, m_audio, m_frame);
    }

    // --- Swap framebuffers ---
    m_activeFB = dstIdx;

    // --- Trigger repaint ---
    update();
}

void GeissWidget::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);

    // Scale the small framebuffer to fill the widget
    painter.drawImage(rect(), m_fb[m_activeFB]);
}

void GeissWidget::mousePressEvent(QMouseEvent *event)
{
    Q_UNUSED(event);
    emit userActivityDetected();
}

void GeissWidget::keyPressEvent(QKeyEvent *event)
{
    Q_UNUSED(event);
    emit userActivityDetected();
}
