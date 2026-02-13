#include "screensaverview.h"
#include "ui_screensaverview.h"
#include "scale.h"
#include <QPainter>
#include <QPainterPath>
#include <QFont>
#include <QFontMetrics>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QRandomGenerator>
#include <cmath>

// --- Optimized box blur: raw pointer access, integer division ---

static void boxBlurH(QImage &src, QImage &dst, int radius)
{
    const int w = src.width();
    const int h = src.height();
    const int divisor = 2 * radius + 1;
    const int srcStride = src.bytesPerLine() / 4;
    const int dstStride = dst.bytesPerLine() / 4;
    const QRgb *srcBits = reinterpret_cast<const QRgb *>(src.constBits());
    QRgb *dstBits = reinterpret_cast<QRgb *>(dst.bits());
    const int wMax = w - 1;

    for (int y = 0; y < h; y++) {
        const QRgb *sl = srcBits + y * srcStride;
        QRgb *dl = dstBits + y * dstStride;
        int ra = 0, ga = 0, ba = 0, aa = 0;

        for (int x = -radius; x <= radius; x++) {
            QRgb p = sl[qBound(0, x, wMax)];
            ra += qRed(p); ga += qGreen(p); ba += qBlue(p); aa += qAlpha(p);
        }
        for (int x = 0; x < w; x++) {
            dl[x] = qRgba(ra / divisor, ga / divisor, ba / divisor, aa / divisor);
            QRgb add = sl[qMin(x + radius + 1, wMax)];
            QRgb sub = sl[qMax(x - radius, 0)];
            ra += qRed(add) - qRed(sub);
            ga += qGreen(add) - qGreen(sub);
            ba += qBlue(add) - qBlue(sub);
            aa += qAlpha(add) - qAlpha(sub);
        }
    }
}

static void boxBlurV(QImage &src, QImage &dst, int radius)
{
    const int w = src.width();
    const int h = src.height();
    const int divisor = 2 * radius + 1;
    const int srcStride = src.bytesPerLine() / 4;
    const int dstStride = dst.bytesPerLine() / 4;
    const QRgb *srcBits = reinterpret_cast<const QRgb *>(src.constBits());
    QRgb *dstBits = reinterpret_cast<QRgb *>(dst.bits());
    const int hMax = h - 1;

    for (int x = 0; x < w; x++) {
        int ra = 0, ga = 0, ba = 0, aa = 0;

        for (int y = -radius; y <= radius; y++) {
            QRgb p = srcBits[qBound(0, y, hMax) * srcStride + x];
            ra += qRed(p); ga += qGreen(p); ba += qBlue(p); aa += qAlpha(p);
        }
        for (int y = 0; y < h; y++) {
            dstBits[y * dstStride + x] =
                qRgba(ra / divisor, ga / divisor, ba / divisor, aa / divisor);
            QRgb add = srcBits[qMin(y + radius + 1, hMax) * srcStride + x];
            QRgb sub = srcBits[qMax(y - radius, 0) * srcStride + x];
            ra += qRed(add) - qRed(sub);
            ga += qGreen(add) - qGreen(sub);
            ba += qBlue(add) - qBlue(sub);
            aa += qAlpha(add) - qAlpha(sub);
        }
    }
}

static void blurImage(QImage &img, QImage &tmp, int radius, int passes)
{
    if (radius < 1) return;
    for (int p = 0; p < passes; p++) {
        boxBlurH(img, tmp, radius);
        boxBlurV(tmp, img, radius);
    }
}

// --- ScreenSaverView ---

ScreenSaverView::ScreenSaverView(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::ScreenSaverView)
{
    ui->setupUi(this);

    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, Qt::black);
    setPalette(pal);

    formatTime();

    m_animTimer = new QTimer(this);
    m_animTimer->setInterval(33); // ~30 FPS
    connect(m_animTimer, &QTimer::timeout, this, &ScreenSaverView::animate);
    m_animTimer->start();
}

ScreenSaverView::~ScreenSaverView()
{
    delete ui;
}

void ScreenSaverView::start()
{
    m_clockMode = QRandomGenerator::global()->bounded(2) == 0 ? Digital : Analog;
    m_hue = QRandomGenerator::global()->bounded(360);
    m_posX = -1;
    m_posY = -1;
}

void ScreenSaverView::formatTime()
{
    QDateTime now = QDateTime::currentDateTime();
    QTime t = now.time();

    int hour = t.hour() % 12;
    if (hour == 0) hour = 12;
    int minute = t.minute();

    bool showColon = t.msec() < 500;
    QString sep = showColon ? ":" : " ";

    m_timeStr = QString::number(hour) + sep + QString("%1").arg(minute, 2, 10, QChar('0'));
    m_ampm = t.hour() >= 12 ? "PM" : "AM";
    m_dateStr = now.toString("dddd, MMMM d");
}

void ScreenSaverView::animate()
{
    formatTime();

    m_posX += m_velX;
    m_posY += m_velY;

    m_hue = fmodf(m_hue + 0.2f, 360.0f);
    m_breathePhase = fmodf(m_breathePhase + 0.06f, 6.2831853f); // wrap at 2*pi

    update();
}

void ScreenSaverView::precomputeAnalogGeometry()
{
    const int W = width();
    const int H = height();
    if (W == m_cachedW && H == m_cachedH) return;
    m_cachedW = W;
    m_cachedH = H;

    // Allocate glow buffers once (reused every frame)
    const int gW = W / 4;
    const int gH = H / 4;
    m_glowBuffer = QImage(gW, gH, QImage::Format_ARGB32_Premultiplied);
    m_glowTmp = QImage(gW, gH, QImage::Format_ARGB32_Premultiplied);

    // Precompute 60 tick line segments
    const float cx = W * 0.5f;
    const float cy = H * 0.5f;
    const float margin = 3.0f * UI_SCALE;

    auto rayToEdge = [&](float angleRad) -> QPointF {
        float dx = sinf(angleRad);
        float dy = -cosf(angleRad);
        float left = margin, right = W - margin;
        float top = margin, bottom = H - margin;
        float tMin = 1e9f;

        if (fabsf(dx) > 1e-9f) {
            float t = (left - cx) / dx;
            if (t > 0) tMin = qMin(tMin, t);
            t = (right - cx) / dx;
            if (t > 0) tMin = qMin(tMin, t);
        }
        if (fabsf(dy) > 1e-9f) {
            float t = (top - cy) / dy;
            if (t > 0) tMin = qMin(tMin, t);
            t = (bottom - cy) / dy;
            if (t > 0) tMin = qMin(tMin, t);
        }
        return QPointF(cx + dx * tMin, cy + dy * tMin);
    };

    int majorIdx = 0, minorIdx = 0, minuteIdx = 0;

    for (int i = 0; i < 60; i++) {
        float angle = i * (float)(M_PI * 2.0 / 60.0);
        QPointF edgePt = rayToEdge(angle);

        float edgeDx = edgePt.x() - cx;
        float edgeDy = edgePt.y() - cy;
        float edgeDist = sqrtf(edgeDx * edgeDx + edgeDy * edgeDy);
        float nx = edgeDx / edgeDist;
        float ny = edgeDy / edgeDist;

        float tickLen;
        QLineF line(edgePt.x(), edgePt.y(), 0, 0); // outer is p1

        if (i % 15 == 0) {
            tickLen = 6.0f * UI_SCALE;
            line.setP2(QPointF(edgePt.x() - nx * tickLen, edgePt.y() - ny * tickLen));
            // Swap so inner→outer for consistent drawing direction
            m_majorTicks[majorIdx++] = QLineF(line.p2(), line.p1());
        } else if (i % 5 == 0) {
            tickLen = 3.5f * UI_SCALE;
            line.setP2(QPointF(edgePt.x() - nx * tickLen, edgePt.y() - ny * tickLen));
            m_minorTicks[minorIdx++] = QLineF(line.p2(), line.p1());
        } else {
            tickLen = 1.5f * UI_SCALE;
            line.setP2(QPointF(edgePt.x() - nx * tickLen, edgePt.y() - ny * tickLen));
            m_minuteTicks[minuteIdx++] = QLineF(line.p2(), line.p1());
        }
    }
}

void ScreenSaverView::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), Qt::black);

    if (m_clockMode == Digital) {
        paintDigitalClock(painter);
    } else {
        paintAnalogClock(painter);
    }
}

void ScreenSaverView::paintDigitalClock(QPainter &painter)
{
    // Fonts
    QFont timeFont("DejaVu Sans Mono");
    timeFont.setPixelSize(44 * UI_SCALE);
    timeFont.setBold(true);

    QFont ampmFont("DejaVu Sans");
    ampmFont.setPixelSize(18 * UI_SCALE);
    ampmFont.setBold(true);

    QFont dateFont("DejaVu Sans");
    dateFont.setPixelSize(13 * UI_SCALE);

    // Measure text block
    QFontMetrics timeFm(timeFont);
    QFontMetrics ampmFm(ampmFont);
    QFontMetrics dateFm(dateFont);

    int timeW = timeFm.horizontalAdvance(m_timeStr);
    int ampmGap = 4 * UI_SCALE;
    int ampmW = ampmFm.horizontalAdvance(m_ampm);
    int topLineW = timeW + ampmGap + ampmW;
    int dateW = dateFm.horizontalAdvance(m_dateStr);

    int blockW = qMax(topLineW, dateW);
    int lineSpacing = 6 * UI_SCALE;
    int timeH = timeFm.height();
    int dateH = dateFm.height();
    int blockH = timeH + lineSpacing + dateH;

    // Pad for glow overflow
    int pad = 6 * UI_SCALE;
    int totalW = blockW + pad * 2;
    int totalH = blockH + pad * 2;

    // Initialize position to center on first paint
    if (m_posX < 0) {
        m_posX = (width() - totalW) * 0.35f;
        m_posY = (height() - totalH) * 0.4f;
    }

    // Bounce off edges
    if (m_posX <= 0)              { m_posX = 0;                   m_velX = fabsf(m_velX);  }
    if (m_posY <= 0)              { m_posY = 0;                   m_velY = fabsf(m_velY);  }
    if (m_posX + totalW >= width())  { m_posX = width() - totalW;   m_velX = -fabsf(m_velX); }
    if (m_posY + totalH >= height()) { m_posY = height() - totalH;  m_velY = -fabsf(m_velY); }

    int bx = static_cast<int>(m_posX) + pad;
    int by = static_cast<int>(m_posY) + pad;

    // Colors from cycling hue + breathing intensity
    float breathe = 0.75f + 0.25f * sinf(m_breathePhase);
    QColor baseColor = QColor::fromHsvF(m_hue / 360.0f, 0.7f, breathe);
    QColor dateColor = QColor::fromHsvF(fmodf(m_hue + 30.0f, 360.0f) / 360.0f, 0.5f, breathe * 0.6f);

    // Helper: draw text with neon glow via QPainterPath strokes
    auto drawNeonText = [&](const QString &text, const QFont &font, int x, int y, QColor color) {
        QPainterPath path;
        path.addText(x, y, font, text);

        // Outer glow — wide, very dim
        QColor outer = color;
        outer.setAlphaF(0.12f * breathe);
        painter.setPen(QPen(outer, 6.0f * UI_SCALE, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(path);

        // Mid glow
        QColor mid = color;
        mid.setAlphaF(0.25f * breathe);
        painter.setPen(QPen(mid, 2.5f * UI_SCALE, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawPath(path);

        // Core fill — bright white-tinted center
        int r = color.red()   + (255 - color.red())   * 0.45f;
        int g = color.green() + (255 - color.green()) * 0.45f;
        int b = color.blue()  + (255 - color.blue())  * 0.45f;
        QColor core(qMin(r, 255), qMin(g, 255), qMin(b, 255));
        painter.setPen(Qt::NoPen);
        painter.setBrush(core);
        painter.drawPath(path);
    };

    // Time — center the top line within the block
    int topLineX = bx + (blockW - topLineW) / 2;
    int timeBaseY = by + timeFm.ascent();
    drawNeonText(m_timeStr, timeFont, topLineX, timeBaseY, baseColor);

    // AM/PM — baseline-aligned with time
    int ampmX = topLineX + timeW + ampmGap;
    int ampmBaseY = timeBaseY - timeFm.descent() + ampmFm.descent();
    drawNeonText(m_ampm, ampmFont, ampmX, ampmBaseY, baseColor);

    // Date — centered below
    int dateX = bx + (blockW - dateW) / 2;
    int dateBaseY = by + timeH + lineSpacing + dateFm.ascent();
    drawNeonText(m_dateStr, dateFont, dateX, dateBaseY, dateColor);
}

void ScreenSaverView::paintAnalogClock(QPainter &painter)
{
    precomputeAnalogGeometry();

    const int W = width();
    const int H = height();
    const float cx = W * 0.5f;
    const float cy = H * 0.5f;
    const float margin = 3.0f * UI_SCALE;
    constexpr float GLOW_SCALE = 4.0f;

    float breathe = 0.75f + 0.25f * sinf(m_breathePhase);

    // Core color: white-tinted rainbow
    QColor baseColor = QColor::fromHsvF(m_hue / 360.0f, 0.6f, 1.0f);
    int cr = baseColor.red()   + (255 - baseColor.red())   * 0.5f;
    int cg = baseColor.green() + (255 - baseColor.green()) * 0.5f;
    int cb = baseColor.blue()  + (255 - baseColor.blue())  * 0.5f;
    QColor coreColor(qMin(cr, 255), qMin(cg, 255), qMin(cb, 255));

    QColor redAccent(255, 60, 40);
    int rr = 255 + (255 - 255) * 0.5f;
    int rg = 60  + (255 - 60)  * 0.5f;
    int rb = 40  + (255 - 40)  * 0.5f;
    QColor coreRed(qMin(rr, 255), qMin(rg, 255), qMin(rb, 255));

    // rayToEdge for dynamic elements only (3 calls per frame)
    auto rayToEdge = [&](float angleRad) -> QPointF {
        float dx = sinf(angleRad);
        float dy = -cosf(angleRad);
        float left = margin, right = W - margin;
        float top = margin, bottom = H - margin;
        float tMin = 1e9f;
        if (fabsf(dx) > 1e-9f) {
            float t = (left - cx) / dx;
            if (t > 0) tMin = qMin(tMin, t);
            t = (right - cx) / dx;
            if (t > 0) tMin = qMin(tMin, t);
        }
        if (fabsf(dy) > 1e-9f) {
            float t = (top - cy) / dy;
            if (t > 0) tMin = qMin(tMin, t);
            t = (bottom - cy) / dy;
            if (t > 0) tMin = qMin(tMin, t);
        }
        return QPointF(cx + dx * tMin, cy + dy * tMin);
    };

    // Compute dynamic hand positions
    QTime t = QTime::currentTime();
    float hours = (t.hour() % 12) + t.minute() / 60.0f;
    float minutes = t.minute() + t.second() / 60.0f;
    float seconds = t.second() + t.msec() / 1000.0f;
    QPointF center(cx, cy);

    QPointF hourEnd, minuteEnd;
    {
        QPointF e = rayToEdge(hours * (float)(M_PI * 2.0 / 12.0));
        hourEnd = QPointF(cx + (e.x() - cx) * 0.45f, cy + (e.y() - cy) * 0.45f);
        e = rayToEdge(minutes * (float)(M_PI * 2.0 / 60.0));
        minuteEnd = QPointF(cx + (e.x() - cx) * 0.70f, cy + (e.y() - cy) * 0.70f);
    }

    // Second marker vertices — stack-allocated, no QPolygonF heap
    QPointF secVerts[4];
    {
        QPointF secPt = rayToEdge(seconds * (float)(M_PI * 2.0 / 60.0));
        float sdx = cx - secPt.x(), sdy = cy - secPt.y();
        float sdist = sqrtf(sdx * sdx + sdy * sdy);
        float snx = sdx / sdist, sny = sdy / sdist;
        float spx = -sny, spy = snx;
        float hL = 3.0f * UI_SCALE, hW = 1.5f * UI_SCALE;
        secVerts[0] = QPointF(secPt.x() + snx*hL + spx*hW, secPt.y() + sny*hL + spy*hW);
        secVerts[1] = QPointF(secPt.x() + snx*hL - spx*hW, secPt.y() + sny*hL - spy*hW);
        secVerts[2] = QPointF(secPt.x() - snx*hL - spx*hW, secPt.y() - sny*hL - spy*hW);
        secVerts[3] = QPointF(secPt.x() - snx*hL + spx*hW, secPt.y() - sny*hL + spy*hW);
    }

    // Pre-create pens once (avoids 60+ per-frame constructions)
    QPen majorPen(coreColor, 2.5f * UI_SCALE, Qt::SolidLine, Qt::RoundCap);
    QPen minorPen(coreColor, 1.5f * UI_SCALE, Qt::SolidLine, Qt::RoundCap);
    QPen minutePen(coreColor, 0.75f * UI_SCALE, Qt::SolidLine, Qt::RoundCap);
    QPen hourPen(coreColor, 3.0f * UI_SCALE, Qt::SolidLine, Qt::RoundCap);
    QPen minuteHandPen(coreColor, 2.0f * UI_SCALE, Qt::SolidLine, Qt::RoundCap);

    // Draw all clock elements onto a painter (called twice: glow + screen)
    auto drawElements = [&](QPainter &p) {
        // Batched tick draws: 3 setPen + drawLines instead of 60 individual calls
        p.setPen(majorPen);
        p.drawLines(m_majorTicks, 4);
        p.setPen(minorPen);
        p.drawLines(m_minorTicks, 8);
        p.setPen(minutePen);
        p.drawLines(m_minuteTicks, 48);

        // Hands
        p.setPen(hourPen);
        p.drawLine(center, hourEnd);
        p.setPen(minuteHandPen);
        p.drawLine(center, minuteEnd);

        // Second marker (drawConvexPolygon avoids complex fill-rule processing)
        p.setPen(Qt::NoPen);
        p.setBrush(coreRed);
        p.drawConvexPolygon(secVerts, 4);

        // Center dot
        p.setBrush(coreColor);
        p.drawEllipse(center, 2.5f * UI_SCALE, 2.5f * UI_SCALE);
    };

    // --- Glow pass: paint to 1/4-size buffer, blur, upscale ---
    m_glowBuffer.fill(Qt::transparent);
    {
        QPainter gp(&m_glowBuffer);
        // No antialiasing — blur will smooth all edges
        gp.scale(1.0f / GLOW_SCALE, 1.0f / GLOW_SCALE);
        drawElements(gp);
    }

    // Single blur layer: radius 3, 2 passes (approximates Gaussian)
    blurImage(m_glowBuffer, m_glowTmp, 3, 2);

    // Composite glow to screen (bilinear upscale)
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.setOpacity(0.7f * breathe);
    painter.drawImage(QRect(0, 0, W, H), m_glowBuffer);

    // --- Sharp core pass: draw directly to screen (AA from paintEvent) ---
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    painter.setOpacity(breathe);
    drawElements(painter);

    painter.setOpacity(1.0f);
}

void ScreenSaverView::mousePressEvent(QMouseEvent *event)
{
    Q_UNUSED(event);
    emit userActivityDetected();
}

void ScreenSaverView::keyPressEvent(QKeyEvent *event)
{
    Q_UNUSED(event);
    emit userActivityDetected();
}
