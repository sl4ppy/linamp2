#include "avsview.h"
#include <QPainter>
#include <QMouseEvent>
#include <QKeyEvent>

AvsView::AvsView(QWidget *parent)
    : QWidget(parent)
{
    // Black background
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, Qt::black);
    setPalette(pal);

    // Make sure we can receive key events
    setFocusPolicy(Qt::StrongFocus);

    // Render timer at ~30 FPS
    m_renderTimer = new QTimer(this);
    m_renderTimer->setInterval(33);
    connect(m_renderTimer, &QTimer::timeout, this, QOverload<>::of(&QWidget::update));

    // Auto-cycle timer
    m_autoCycleTimer = new QTimer(this);
    m_autoCycleTimer->setInterval(AUTO_CYCLE_INTERVAL_MS);
    connect(m_autoCycleTimer, &QTimer::timeout, this, [this]() {
        m_engine.nextPreset();
        onPresetChanged();
    });
}

AvsView::~AvsView()
{
}

void AvsView::setAudioData(const QByteArray &data, QAudioFormat format)
{
    m_audioData.processFromPcm(data, format);
}

void AvsView::setMetadata(QMediaMetaData metadata)
{
    QString artist = metadata.value(QMediaMetaData::AlbumArtist).toString();
    if (artist.isEmpty())
        artist = metadata.value(QMediaMetaData::ContributingArtist).toString();
    QString title = metadata.value(QMediaMetaData::Title).toString();

    // Only show OSD if metadata actually changed and has content
    if ((artist != m_trackArtist || title != m_trackTitle) && !title.isEmpty()) {
        m_trackArtist = artist;
        m_trackTitle = title;
        m_showTrackInfo = true;
        m_trackInfoTimer.restart();
    }
}

void AvsView::start()
{
    m_running = true;
    m_renderTimer->start();
    if (m_autoCycleEnabled)
        m_autoCycleTimer->start();
    setFocus();
}

void AvsView::stop()
{
    m_running = false;
    m_renderTimer->stop();
    m_autoCycleTimer->stop();
}

void AvsView::onPresetChanged()
{
    m_showPresetName = true;
    m_presetNameTimer.restart();

    // Restart auto-cycle countdown on manual change
    if (m_autoCycleEnabled && m_running)
        m_autoCycleTimer->start();
}

void AvsView::paintEvent(QPaintEvent *)
{
    QPainter painter(this);

    if (m_running) {
        const QImage &frame = m_engine.renderFrame(m_audioData);

        // Disable smooth scaling for nearest-neighbor retro look
        painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
        painter.drawImage(rect(), frame);

        // Draw preset name OSD (top-left)
        if (m_showPresetName && m_presetNameTimer.isValid()) {
            int elapsed = m_presetNameTimer.elapsed();
            if (elapsed < PRESET_NAME_DURATION_MS) {
                // Fade out over the last 500ms
                float alpha = 1.0f;
                int fadeStart = PRESET_NAME_DURATION_MS - 500;
                if (elapsed > fadeStart)
                    alpha = 1.0f - static_cast<float>(elapsed - fadeStart) / 500.0f;

                QColor textColor(255, 255, 255, static_cast<int>(alpha * 220));
                QColor shadowColor(0, 0, 0, static_cast<int>(alpha * 180));

                painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
                QFont font("DejaVu Sans", 10);
                font.setBold(true);
                painter.setFont(font);

                QString text = m_engine.presetName();
                QRect textRect = rect().adjusted(8, 8, -8, -8);

                // Shadow
                painter.setPen(shadowColor);
                painter.drawText(textRect.adjusted(1, 1, 1, 1), Qt::AlignTop | Qt::AlignLeft, text);
                // Text
                painter.setPen(textColor);
                painter.drawText(textRect, Qt::AlignTop | Qt::AlignLeft, text);
            } else {
                m_showPresetName = false;
            }
        }

        // Draw track info OSD (bottom-right)
        if (m_showTrackInfo && m_trackInfoTimer.isValid()) {
            int elapsed = m_trackInfoTimer.elapsed();
            if (elapsed < TRACK_INFO_DURATION_MS) {
                float alpha = 1.0f;
                int fadeStart = TRACK_INFO_DURATION_MS - 500;
                if (elapsed > fadeStart)
                    alpha = 1.0f - static_cast<float>(elapsed - fadeStart) / 500.0f;

                QColor textColor(255, 255, 255, static_cast<int>(alpha * 200));
                QColor shadowColor(0, 0, 0, static_cast<int>(alpha * 160));

                painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
                QFont font("DejaVu Sans", 9);
                painter.setFont(font);

                QString text;
                if (!m_trackArtist.isEmpty())
                    text = m_trackArtist + " - " + m_trackTitle;
                else
                    text = m_trackTitle;

                QRect textRect = rect().adjusted(8, 8, -8, -8);

                // Shadow
                painter.setPen(shadowColor);
                painter.drawText(textRect.adjusted(1, 1, 1, 1), Qt::AlignBottom | Qt::AlignRight, text);
                // Text
                painter.setPen(textColor);
                painter.drawText(textRect, Qt::AlignBottom | Qt::AlignRight, text);
            } else {
                m_showTrackInfo = false;
            }
        }
    } else {
        painter.fillRect(rect(), Qt::black);
    }
}

void AvsView::mousePressEvent(QMouseEvent *event)
{
    m_pressPos = event->pos();
    m_swiping = false;
}

void AvsView::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_swiping) {
        int dx = event->pos().x() - m_pressPos.x();
        if (qAbs(dx) > 30)
            m_swiping = true;
    }
}

void AvsView::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_swiping) {
        int dx = event->pos().x() - m_pressPos.x();
        if (dx > 0) {
            m_engine.prevPreset(); // swipe right = previous
        } else {
            m_engine.nextPreset(); // swipe left = next
        }
        onPresetChanged();
    } else {
        // Tap = exit
        emit userActivityDetected();
    }
}

void AvsView::keyPressEvent(QKeyEvent *event)
{
    switch (event->key()) {
    case Qt::Key_Right:
        m_engine.nextPreset();
        onPresetChanged();
        break;
    case Qt::Key_Left:
        m_engine.prevPreset();
        onPresetChanged();
        break;
    case Qt::Key_Up:
        m_autoCycleEnabled = true;
        if (m_running)
            m_autoCycleTimer->start();
        break;
    case Qt::Key_Down:
        m_autoCycleEnabled = false;
        m_autoCycleTimer->stop();
        break;
    default:
        emit userActivityDetected();
        break;
    }
}
