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
#include <QTransform>
#include <QRadialGradient>
#include <QLinearGradient>
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
    m_currentTheme = makeLuxuryTheme();

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
    m_hue = QRandomGenerator::global()->bounded(360);
    m_posX = -1;
    m_posY = -1;

    // Select random theme (includes digital as one option)
    auto themes = getAllClockThemes();
    m_themeIndex = QRandomGenerator::global()->bounded(themes.size());
    m_currentTheme = themes[m_themeIndex];
    m_clockMode = m_currentTheme.isDigital ? Digital : Analog;
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
    m_breathePhase = fmodf(m_breathePhase + 0.06f, 6.2831853f);

    update();
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

QPointF ScreenSaverView::placeFloatingBlock(float totalW, float totalH)
{
    // Center on first paint
    if (m_posX < 0) {
        m_posX = (width()  - totalW) * 0.5f;
        m_posY = (height() - totalH) * 0.5f;
    }
    // Bounce off edges
    if (m_posX <= 0)                      { m_posX = 0;                m_velX = fabsf(m_velX);  }
    if (m_posY <= 0)                      { m_posY = 0;                m_velY = fabsf(m_velY);  }
    if (m_posX + totalW >= width())       { m_posX = width()  - totalW; m_velX = -fabsf(m_velX); }
    if (m_posY + totalH >= height())      { m_posY = height() - totalH; m_velY = -fabsf(m_velY); }
    return QPointF(m_posX, m_posY);
}

void ScreenSaverView::paintDigitalClock(QPainter &painter)
{
    switch (m_currentTheme.digitalStyle) {
    case DigitalStyle::SevenSegment: paintDigitalSevenSeg(painter);  break;
    case DigitalStyle::SplitFlap:    paintDigitalSplitFlap(painter); break;
    case DigitalStyle::Nixie:        paintDigitalNixie(painter);     break;
    case DigitalStyle::Terminal:     paintDigitalTerminal(painter);  break;
    case DigitalStyle::Neon:
    default:                         paintDigitalNeon(painter);      break;
    }
}

void ScreenSaverView::paintDigitalNeon(QPainter &painter)
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

        // Outer glow
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

        // Core fill
        int r = color.red()   + (255 - color.red())   * 0.45f;
        int g = color.green() + (255 - color.green()) * 0.45f;
        int b = color.blue()  + (255 - color.blue())  * 0.45f;
        QColor core(qMin(r, 255), qMin(g, 255), qMin(b, 255));
        painter.setPen(Qt::NoPen);
        painter.setBrush(core);
        painter.drawPath(path);
    };

    // Time
    int topLineX = bx + (blockW - topLineW) / 2;
    int timeBaseY = by + timeFm.ascent();
    drawNeonText(m_timeStr, timeFont, topLineX, timeBaseY, baseColor);

    // AM/PM
    int ampmX = topLineX + timeW + ampmGap;
    int ampmBaseY = timeBaseY - timeFm.descent() + ampmFm.descent();
    drawNeonText(m_ampm, ampmFont, ampmX, ampmBaseY, baseColor);

    // Date
    int dateX = bx + (blockW - dateW) / 2;
    int dateBaseY = by + timeH + lineSpacing + dateFm.ascent();
    drawNeonText(m_dateStr, dateFont, dateX, dateBaseY, dateColor);
}

// --- Analog Clock: Themed Rendering ---

QColor ScreenSaverView::applyHueCycle(const QColor &color, float hueShift) const
{
    float h, s, v, a;
    color.getHsvF(&h, &s, &v, &a);
    if (h < 0) h = 0; // achromatic
    h = fmodf(h + hueShift / 360.0f, 1.0f);
    if (h < 0) h += 1.0f;
    s = qMax(s, 0.5f); // ensure color stays vivid
    return QColor::fromHsvF(h, s, v, a);
}

void ScreenSaverView::drawDialBackground(QPainter &p, float cx, float cy, float radius,
                                          const ClockTheme &theme)
{
    if (theme.guilloche) {
        // Radial gradient dial: lightened center -> dial -> dark edge
        QColor mid  = theme.colors.dial;
        QColor edge = theme.colors.dialEdge.isValid() ? theme.colors.dialEdge : mid.darker(160);
        QRadialGradient grad(QPointF(cx, cy - radius * 0.15f), radius);
        grad.setColorStop(0.0, mid.lighter(135));
        grad.setColorStop(0.6, mid);
        grad.setColorStop(1.0, edge);
        p.setPen(Qt::NoPen);
        p.setBrush(grad);
        p.drawEllipse(QPointF(cx, cy), radius, radius);

        // Sunburst guilloche lines, clipped to the dial
        p.save();
        QPainterPath clip;
        clip.addEllipse(QPointF(cx, cy), radius * 0.97f, radius * 0.97f);
        p.setClipPath(clip);
        for (int i = 0; i < 180; i++) {
            float angle = i * 2.0f * static_cast<float>(M_PI) / 180.0f;
            QColor lineCol = (i % 2 == 0) ? QColor(0, 0, 0, 26) : QColor(255, 255, 255, 12);
            p.setPen(QPen(lineCol, 0.5f * UI_SCALE));
            p.drawLine(QPointF(cx, cy),
                       QPointF(cx + sinf(angle) * radius, cy - cosf(angle) * radius));
        }
        p.restore();

        // Gold rim
        QColor rimColor = theme.colors.rim;
        p.setPen(QPen(rimColor, 2.0f * UI_SCALE, Qt::SolidLine));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(QPointF(cx, cy), radius, radius);
        return;
    }

    // Dial circle
    QColor dialColor = theme.colors.dial;
    if (theme.hueCycling)
        dialColor = applyHueCycle(dialColor, m_hue);

    p.setPen(Qt::NoPen);
    p.setBrush(dialColor);
    p.drawEllipse(QPointF(cx, cy), radius, radius);

    // Rim
    QColor rimColor = theme.colors.rim;
    if (theme.hueCycling)
        rimColor = applyHueCycle(rimColor, m_hue);

    p.setPen(QPen(rimColor, 1.5f * UI_SCALE, Qt::SolidLine));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(QPointF(cx, cy), radius, radius);
}

void ScreenSaverView::drawDecorativeRings(QPainter &p, float cx, float cy, float radius,
                                           const ClockTheme &theme)
{
    QColor ringColor = theme.colors.decorativeRing;
    if (theme.hueCycling)
        ringColor = applyHueCycle(ringColor, m_hue);

    float ringPenWidth = 0.5f * UI_SCALE;

    for (int i = 0; i < theme.decorativeRings; i++) {
        float ringRadius = radius * (0.88f - i * 0.12f);
        p.setPen(QPen(ringColor, ringPenWidth, Qt::SolidLine));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(QPointF(cx, cy), ringRadius, ringRadius);
    }

    // Tachymeter outer ring
    if (theme.tachymeterRing) {
        float tachRadius = radius * 0.96f;
        QColor tachColor = theme.colors.ticks;
        tachColor.setAlphaF(0.3f);
        if (theme.hueCycling)
            tachColor = applyHueCycle(tachColor, m_hue);

        p.setPen(QPen(tachColor, 0.4f * UI_SCALE, Qt::SolidLine));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(QPointF(cx, cy), tachRadius, tachRadius);

        // Small tach tick marks every 6 degrees
        for (int i = 0; i < 60; i++) {
            float angle = i * 6.0f * static_cast<float>(M_PI) / 180.0f;
            float inner = tachRadius - 2.0f * UI_SCALE;
            float outer = tachRadius;
            float dx = sinf(angle);
            float dy = -cosf(angle);
            p.drawLine(QPointF(cx + dx * inner, cy + dy * inner),
                       QPointF(cx + dx * outer, cy + dy * outer));
        }
    }
}

void ScreenSaverView::drawTicks(QPainter &p, float cx, float cy, float radius,
                                 const ClockTheme &theme)
{
    for (int i = 0; i < 60; i++) {
        float angle = i * 6.0f * static_cast<float>(M_PI) / 180.0f;

        if (i % 15 == 0) {
            // Cardinal positions (12, 3, 6, 9)
            QColor color = theme.colors.cardinalTicks;
            if (theme.hueCycling) color = applyHueCycle(color, m_hue);
            drawSingleTick(p, cx, cy, radius, angle, theme.ticks.cardinalShape,
                           theme.ticks.cardinalSize, color, theme);
        } else if (i % 5 == 0) {
            // Hour positions
            QColor color = theme.colors.ticks;
            if (theme.hueCycling) color = applyHueCycle(color, m_hue);
            drawSingleTick(p, cx, cy, radius, angle, theme.ticks.hourShape,
                           theme.ticks.hourSize, color, theme);

            // Luminous dots
            if (theme.ticks.luminousDots) {
                float dotDist = radius * 0.78f;
                float dotR = 1.5f * UI_SCALE * theme.ticks.hourSize;
                float dx = sinf(angle);
                float dy = -cosf(angle);
                QColor lumeColor = theme.colors.lume;
                if (theme.hueCycling) lumeColor = applyHueCycle(lumeColor, m_hue);
                p.setPen(Qt::NoPen);
                p.setBrush(lumeColor);
                p.drawEllipse(QPointF(cx + dx * dotDist, cy + dy * dotDist), dotR, dotR);
            }
        } else {
            // Minute positions
            if (theme.ticks.minuteShape != TickShape::None) {
                QColor color = theme.colors.ticks;
                color.setAlphaF(color.alphaF() * 0.6f);
                if (theme.hueCycling) color = applyHueCycle(color, m_hue);
                drawSingleTick(p, cx, cy, radius, angle, theme.ticks.minuteShape,
                               theme.ticks.minuteSize, color, theme);
            }
        }
    }
}

void ScreenSaverView::drawSingleTick(QPainter &p, float cx, float cy, float radius,
                                       float angle, TickShape shape, float size,
                                       const QColor &color, const ClockTheme &theme)
{
    if (shape == TickShape::None) return;

    float dx = sinf(angle);
    float dy = -cosf(angle);

    // Tick sits at the edge of dial, inward
    float outerDist = radius * 0.92f;
    float baseLen = 4.0f * UI_SCALE * size;

    switch (shape) {
    case TickShape::Line: {
        float innerDist = outerDist - baseLen;
        p.setPen(QPen(color, 0.6f * UI_SCALE * size, Qt::SolidLine, Qt::RoundCap));
        p.setBrush(Qt::NoBrush);
        p.drawLine(QPointF(cx + dx * innerDist, cy + dy * innerDist),
                   QPointF(cx + dx * outerDist, cy + dy * outerDist));
        break;
    }
    case TickShape::Rect: {
        float innerDist = outerDist - baseLen;
        float halfW = 1.0f * UI_SCALE * size;
        float px = -dy; // perpendicular
        float py = dx;
        QPointF pts[4] = {
            { cx + dx * innerDist - px * halfW, cy + dy * innerDist - py * halfW },
            { cx + dx * innerDist + px * halfW, cy + dy * innerDist + py * halfW },
            { cx + dx * outerDist + px * halfW, cy + dy * outerDist + py * halfW },
            { cx + dx * outerDist - px * halfW, cy + dy * outerDist - py * halfW }
        };
        p.setPen(Qt::NoPen);
        if (theme.outlineOnly) {
            p.setPen(QPen(color, 0.5f * UI_SCALE));
            p.setBrush(Qt::NoBrush);
        } else {
            p.setBrush(color);
        }
        p.drawConvexPolygon(pts, 4);
        break;
    }
    case TickShape::Triangle: {
        float innerDist = outerDist - baseLen * 1.2f;
        float halfW = 1.8f * UI_SCALE * size;
        float px = -dy;
        float py = dx;
        QPointF pts[3] = {
            { cx + dx * outerDist, cy + dy * outerDist },          // tip (outer)
            { cx + dx * innerDist - px * halfW, cy + dy * innerDist - py * halfW },
            { cx + dx * innerDist + px * halfW, cy + dy * innerDist + py * halfW }
        };
        p.setPen(Qt::NoPen);
        if (theme.outlineOnly) {
            p.setPen(QPen(color, 0.5f * UI_SCALE));
            p.setBrush(Qt::NoBrush);
        } else {
            p.setBrush(color);
        }
        p.drawConvexPolygon(pts, 3);
        break;
    }
    case TickShape::Dot: {
        float dotDist = outerDist - baseLen * 0.5f;
        float dotR = 1.2f * UI_SCALE * size;
        p.setPen(Qt::NoPen);
        if (theme.outlineOnly) {
            p.setPen(QPen(color, 0.5f * UI_SCALE));
            p.setBrush(Qt::NoBrush);
        } else {
            p.setBrush(color);
        }
        p.drawEllipse(QPointF(cx + dx * dotDist, cy + dy * dotDist), dotR, dotR);
        break;
    }
    case TickShape::Diamond: {
        float midDist = outerDist - baseLen * 0.5f;
        float halfLen = baseLen * 0.6f;
        float halfW = 1.2f * UI_SCALE * size;
        float px = -dy;
        float py = dx;
        QPointF pts[4] = {
            { cx + dx * (midDist + halfLen), cy + dy * (midDist + halfLen) }, // outer point
            { cx + dx * midDist + px * halfW, cy + dy * midDist + py * halfW },
            { cx + dx * (midDist - halfLen), cy + dy * (midDist - halfLen) }, // inner point
            { cx + dx * midDist - px * halfW, cy + dy * midDist - py * halfW }
        };
        p.setPen(Qt::NoPen);
        if (theme.outlineOnly) {
            p.setPen(QPen(color, 0.5f * UI_SCALE));
            p.setBrush(Qt::NoBrush);
        } else {
            p.setBrush(color);
        }
        p.drawConvexPolygon(pts, 4);
        break;
    }
    case TickShape::None:
        break;
    }
}

void ScreenSaverView::drawNumerals(QPainter &p, float cx, float cy, float radius,
                                    const ClockTheme &theme)
{
    if (theme.numeralStyle == NumeralStyle::None) return;

    QFont font("DejaVu Sans");
    font.setPixelSize(static_cast<int>(radius * theme.numeralSizeFraction));
    font.setBold(true);
    p.setFont(font);

    QColor color = theme.colors.numerals;
    if (theme.hueCycling)
        color = applyHueCycle(color, m_hue);
    p.setPen(color);
    p.setBrush(Qt::NoBrush);

    static const char *arabicNums[4] = { "12", "3", "6", "9" };
    static const char *romanNums[4]  = { "XII", "III", "VI", "IX" };
    static const float angles[4] = { 0.0f, 90.0f, 180.0f, 270.0f };

    const char **nums = (theme.numeralStyle == NumeralStyle::Arabic) ? arabicNums : romanNums;

    QFontMetrics fm(font);
    float numDist = radius * 0.72f;

    for (int i = 0; i < 4; i++) {
        float angleRad = angles[i] * static_cast<float>(M_PI) / 180.0f;
        float nx = cx + sinf(angleRad) * numDist;
        float ny = cy - cosf(angleRad) * numDist;

        QString text = QString::fromLatin1(nums[i]);
        QRect br = fm.boundingRect(text);
        p.drawText(static_cast<int>(nx - br.width() * 0.5f),
                   static_cast<int>(ny + fm.ascent() * 0.35f),
                   text);
    }
}

void ScreenSaverView::drawHand(QPainter &p, float cx, float cy, float radius,
                                float angleDeg, HandShape shape, float lengthFrac,
                                float widthFrac, const QColor &color, bool outlineOnly,
                                bool drawCounterweight, float counterweightLen)
{
    float length = radius * lengthFrac;
    float width = radius * widthFrac;

    QVector<QPointF> poly = generateHandPolygon(shape, length, width);

    // Rotate and translate
    float angleRad = angleDeg * static_cast<float>(M_PI) / 180.0f;
    QTransform xform;
    xform.translate(cx, cy);
    xform.rotateRadians(angleRad);

    QVector<QPointF> transformed;
    transformed.reserve(poly.size());
    for (const QPointF &pt : poly)
        transformed.append(xform.map(pt));

    if (outlineOnly) {
        p.setPen(QPen(color, 1.0f * UI_SCALE, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
    } else {
        p.setPen(QPen(color.darker(130), 0.3f * UI_SCALE));
        p.setBrush(color);
    }
    p.drawPolygon(transformed.data(), transformed.size());

    // Mercedes circle cutout
    if (shape == HandShape::Mercedes && !outlineOnly) {
        QPointF circCenter;
        float circRadius;
        getMercedesCircle(length, width, circCenter, circRadius);
        QPointF mappedCenter = xform.map(circCenter);
        QPainterPath clip;
        clip.addEllipse(mappedCenter, circRadius, circRadius);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 180));
        p.drawPath(clip);
        // Ring around the cutout
        p.setPen(QPen(color, 0.4f * UI_SCALE));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(mappedCenter, circRadius, circRadius);
    }

    // Breguet moon-hole
    if (shape == HandShape::Breguet && !outlineOnly) {
        QPointF holeCenter;
        float holeRadius;
        getBreguetMoonHole(length, width, holeCenter, holeRadius);
        QPointF mappedCenter = xform.map(holeCenter);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 180));
        p.drawEllipse(mappedCenter, holeRadius, holeRadius);
        p.setPen(QPen(color, 0.3f * UI_SCALE));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(mappedCenter, holeRadius, holeRadius);
    }

    // Counterweight
    if (drawCounterweight) {
        float cwLen = radius * counterweightLen;
        float cwWidth = width * 1.5f;
        QVector<QPointF> cwPoly = generateCounterweight(cwLen, cwWidth);
        QVector<QPointF> cwTransformed;
        cwTransformed.reserve(cwPoly.size());
        for (const QPointF &pt : cwPoly)
            cwTransformed.append(xform.map(pt));

        if (outlineOnly) {
            p.setPen(QPen(color, 1.0f * UI_SCALE, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.setBrush(Qt::NoBrush);
        } else {
            p.setPen(QPen(color.darker(130), 0.3f * UI_SCALE));
            p.setBrush(color);
        }
        p.drawPolygon(cwTransformed.data(), cwTransformed.size());
    }
}

void ScreenSaverView::drawCenterPin(QPainter &p, float cx, float cy, float radius,
                                     const ClockTheme &theme)
{
    float pinR = radius * 0.03f;
    if (pinR < 2.0f) pinR = 2.0f;

    QColor pinColor = theme.colors.centerPin;
    if (theme.hueCycling)
        pinColor = applyHueCycle(pinColor, m_hue);

    if (theme.outlineOnly) {
        p.setPen(QPen(pinColor, 1.0f * UI_SCALE));
        p.setBrush(Qt::NoBrush);
    } else {
        p.setPen(Qt::NoPen);
        p.setBrush(pinColor);
    }
    p.drawEllipse(QPointF(cx, cy), pinR, pinR);
}

void ScreenSaverView::paintAnalogClock(QPainter &painter)
{
    const int W = width();
    const int H = height();
    const ClockTheme &theme = m_currentTheme;

    if (theme.orbital) {
        paintOrbitalClock(painter);
        return;
    }

    float radius = qMin(W, H) * theme.dialRadiusFraction;
    float dialSize = radius * 2.0f + 8.0f * UI_SCALE; // bounding box with margin

    // Initialize position to center on first paint
    if (m_posX < 0) {
        m_posX = (W - dialSize) * 0.5f;
        m_posY = (H - dialSize) * 0.5f;
    }

    // Bounce off edges
    if (m_posX <= 0)                 { m_posX = 0;               m_velX = fabsf(m_velX);  }
    if (m_posY <= 0)                 { m_posY = 0;               m_velY = fabsf(m_velY);  }
    if (m_posX + dialSize >= W)      { m_posX = W - dialSize;    m_velX = -fabsf(m_velX); }
    if (m_posY + dialSize >= H)      { m_posY = H - dialSize;    m_velY = -fabsf(m_velY); }

    float cx = m_posX + dialSize * 0.5f;
    float cy = m_posY + dialSize * 0.5f;

    // Breathing
    float breathe = 1.0f - theme.breatheAmount + theme.breatheAmount * sinf(m_breathePhase);

    // Time angles
    QTime t = QTime::currentTime();
    float hours = (t.hour() % 12) + t.minute() / 60.0f;
    float minutes = t.minute() + t.second() / 60.0f;
    float seconds = t.second() + t.msec() / 1000.0f;

    float hourAngle   = hours * 30.0f;     // 360/12
    float minuteAngle = minutes * 6.0f;    // 360/60
    float secondAngle = seconds * 6.0f;    // 360/60

    // Resolve colors (hue cycling for Neon)
    QColor hourColor   = theme.colors.hourHand;
    QColor minuteColor = theme.colors.minuteHand;
    QColor secondColor = theme.colors.secondHand;
    if (theme.hueCycling) {
        hourColor   = applyHueCycle(hourColor, m_hue);
        minuteColor = applyHueCycle(minuteColor, m_hue);
        secondColor = applyHueCycle(secondColor, m_hue + 120.0f);
    }

    // Lambda to draw all clock elements (used for both glow and sharp pass)
    auto drawElements = [&](QPainter &p) {
        drawDialBackground(p, cx, cy, radius, theme);
        drawDecorativeRings(p, cx, cy, radius, theme);
        drawTicks(p, cx, cy, radius, theme);
        drawNumerals(p, cx, cy, radius, theme);

        // Hour hand
        drawHand(p, cx, cy, radius, hourAngle,
                 theme.hands.hourShape, theme.hands.hourLength, theme.hands.hourWidth,
                 hourColor, theme.outlineOnly);

        // Minute hand
        drawHand(p, cx, cy, radius, minuteAngle,
                 theme.hands.minuteShape, theme.hands.minuteLength, theme.hands.minuteWidth,
                 minuteColor, theme.outlineOnly);

        // Second hand
        drawHand(p, cx, cy, radius, secondAngle,
                 theme.hands.secondShape, theme.hands.secondLength, theme.hands.secondWidth,
                 secondColor, theme.outlineOnly,
                 theme.hands.secondCounterweight, theme.hands.counterweightLen);

        // Disc on the second hand (Mondaine tip / Bauhaus pivot dot)
        if (theme.hands.secondDisc) {
            float a = secondAngle * static_cast<float>(M_PI) / 180.0f;
            float dx = sinf(a), dy = -cosf(a);
            float dist = radius * theme.hands.secondDiscDist;
            float dr   = radius * theme.hands.secondDiscRadius;
            p.setPen(Qt::NoPen);
            p.setBrush(secondColor);
            p.drawEllipse(QPointF(cx + dx * dist, cy + dy * dist), dr, dr);
        }

        drawCenterPin(p, cx, cy, radius, theme);
    };

    // --- Glow pass: paint to buffer sized to dial bounding box ---
    constexpr float GLOW_SCALE = 4.0f;
    int glowDim = static_cast<int>(ceilf(dialSize / GLOW_SCALE)) + 2;

    if (glowDim != m_cachedGlowSize) {
        m_cachedGlowSize = glowDim;
        m_glowBuffer = QImage(glowDim, glowDim, QImage::Format_ARGB32_Premultiplied);
        m_glowTmp   = QImage(glowDim, glowDim, QImage::Format_ARGB32_Premultiplied);
    }

    m_glowBuffer.fill(Qt::transparent);
    {
        QPainter gp(&m_glowBuffer);
        // Translate so the dial center maps correctly into the glow buffer
        float glowOffset = dialSize * 0.5f / GLOW_SCALE;
        gp.scale(1.0f / GLOW_SCALE, 1.0f / GLOW_SCALE);
        gp.translate(-m_posX, -m_posY);
        drawElements(gp);
    }

    int blurRadius = 3;
    blurImage(m_glowBuffer, m_glowTmp, blurRadius, 2);

    // Composite glow
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.setOpacity(0.7f * breathe * theme.glowIntensity);
    QRectF glowRect(m_posX, m_posY, dialSize, dialSize);
    painter.drawImage(glowRect, m_glowBuffer);

    // --- Sharp pass: draw directly to screen ---
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    painter.setOpacity(breathe);
    drawElements(painter);

    painter.setOpacity(1.0f);
}

// --- Orbital: hands-free arc-trail face ---

void ScreenSaverView::paintOrbitalClock(QPainter &painter)
{
    const int W = width();
    const int H = height();
    const ClockTheme &theme = m_currentTheme;

    float radius   = qMin(W, H) * theme.dialRadiusFraction;
    float dialSize = radius * 2.0f + 8.0f * UI_SCALE;

    placeFloatingBlock(dialSize, dialSize);
    float cx = m_posX + dialSize * 0.5f;
    float cy = m_posY + dialSize * 0.5f;

    float breathe = 1.0f - theme.breatheAmount + theme.breatheAmount * sinf(m_breathePhase);

    QTime t = QTime::currentTime();
    float hours   = (t.hour() % 12) + t.minute() / 60.0f;
    float minutes = t.minute() + t.second() / 60.0f;
    float seconds = t.second() + t.msec() / 1000.0f;
    float hourDeg   = hours   * 30.0f;
    float minuteDeg = minutes * 6.0f;
    float secondDeg = seconds * 6.0f;

    painter.setRenderHint(QPainter::Antialiasing, true);

    // Dial + rim
    painter.setPen(Qt::NoPen);
    painter.setBrush(theme.colors.dial);
    painter.drawEllipse(QPointF(cx, cy), radius, radius);
    painter.setPen(QPen(theme.colors.rim, 1.5f * UI_SCALE));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(QPointF(cx, cy), radius, radius);

    // Faint guide rings
    const float ringR[3] = { radius * 0.82f, radius * 0.60f, radius * 0.38f };
    painter.setPen(QPen(theme.colors.decorativeRing, 0.6f * UI_SCALE));
    for (float r : ringR)
        painter.drawEllipse(QPointF(cx, cy), r, r);

    // Arc: from 12 o'clock clockwise to deg, with glow + end dot
    auto drawArc = [&](float r, float deg, const QColor &col, float widthPx) {
        QRectF box(cx - r, cy - r, r * 2.0f, r * 2.0f);
        int startA = 90 * 16;                       // 12 o'clock
        int spanA  = static_cast<int>(-deg * 16);   // clockwise
        // Glow pass
        QColor glow = col; glow.setAlphaF(0.28f * breathe);
        painter.setPen(QPen(glow, widthPx * 2.4f, Qt::SolidLine, Qt::RoundCap));
        painter.drawArc(box, startA, spanA);
        // Core pass
        painter.setPen(QPen(col, widthPx, Qt::SolidLine, Qt::RoundCap));
        painter.drawArc(box, startA, spanA);
        // End dot
        float a = deg * static_cast<float>(M_PI) / 180.0f;
        QPointF end(cx + sinf(a) * r, cy - cosf(a) * r);
        QColor dotGlow = col; dotGlow.setAlphaF(0.35f * breathe);
        painter.setPen(Qt::NoPen);
        painter.setBrush(dotGlow);
        painter.drawEllipse(end, widthPx * 1.5f, widthPx * 1.5f);
        painter.setBrush(QColor(255, 255, 255));
        painter.drawEllipse(end, widthPx * 0.65f, widthPx * 0.65f);
    };

    drawArc(ringR[0], secondDeg, theme.colors.secondHand, 1.4f * UI_SCALE);
    drawArc(ringR[1], minuteDeg, theme.colors.minuteHand, 3.0f * UI_SCALE);
    drawArc(ringR[2], hourDeg,   theme.colors.hourHand,   4.4f * UI_SCALE);

    // Center digital readout
    QFont font("DejaVu Sans");
    font.setPixelSize(static_cast<int>(radius * 0.30f));
    font.setBold(true);
    painter.setFont(font);
    painter.setPen(theme.colors.numerals);
    painter.drawText(QRectF(cx - radius, cy - radius, radius * 2.0f, radius * 2.0f),
                     Qt::AlignCenter, m_timeStr);
}

// --- Seven-segment LED ---

static void drawSevenSegDigit(QPainter &p, float x, float y, float w, float h,
                              float t, int digit, const QColor &on)
{
    // segment order: a b c d e f g
    static const bool MAP[10][7] = {
        {1,1,1,1,1,1,0}, // 0
        {0,1,1,0,0,0,0}, // 1
        {1,1,0,1,1,0,1}, // 2
        {1,1,1,1,0,0,1}, // 3
        {0,1,1,0,0,1,1}, // 4
        {1,0,1,1,0,1,1}, // 5
        {1,0,1,1,1,1,1}, // 6
        {1,1,1,0,0,0,0}, // 7
        {1,1,1,1,1,1,1}, // 8
        {1,1,1,1,0,1,1}, // 9
    };
    if (digit < 0 || digit > 9) return;
    const bool *seg = MAP[digit];
    float vH = h * 0.5f - 1.5f * t;
    QRectF rects[7] = {
        QRectF(x + t,       y,                 w - 2 * t, t),          // a
        QRectF(x + w - t,   y + t,             t,         vH),         // b
        QRectF(x + w - t,   y + h * 0.5f + t * 0.5f, t,   vH),         // c
        QRectF(x + t,       y + h - t,         w - 2 * t, t),          // d
        QRectF(x,           y + h * 0.5f + t * 0.5f, t,   vH),         // e
        QRectF(x,           y + t,             t,         vH),         // f
        QRectF(x + t,       y + h * 0.5f - t * 0.5f, w - 2 * t, t),    // g
    };
    QColor off = on; off.setAlphaF(0.06f);
    for (int i = 0; i < 7; i++) {
        if (seg[i]) {
            QColor glow = on; glow.setAlphaF(0.30f);
            QRectF gr = rects[i].adjusted(-t * 0.5f, -t * 0.5f, t * 0.5f, t * 0.5f);
            p.setPen(Qt::NoPen);
            p.setBrush(glow);
            p.drawRoundedRect(gr, t * 0.4f, t * 0.4f);
            p.setBrush(on);
            p.drawRoundedRect(rects[i], t * 0.35f, t * 0.35f);
        } else {
            p.setPen(Qt::NoPen);
            p.setBrush(off);
            p.drawRoundedRect(rects[i], t * 0.35f, t * 0.35f);
        }
    }
}

void ScreenSaverView::paintDigitalSevenSeg(QPainter &painter)
{
    QTime tm = QTime::currentTime();
    int hour = tm.hour() % 12; if (hour == 0) hour = 12;
    QString hh = QString::number(hour);
    QString mm = QString("%1").arg(tm.minute(), 2, 10, QChar('0'));

    float dh = 64.0f * UI_SCALE;
    float dw = 34.0f * UI_SCALE;
    float th = 7.0f  * UI_SCALE;       // segment thickness
    float dgap = 9.0f * UI_SCALE;
    float colonW = 18.0f * UI_SCALE;

    QString digits = hh + mm;
    int nDigits = digits.size();
    float totalW = nDigits * dw + (nDigits - 1) * dgap + colonW;
    float totalH = dh;
    float pad = 30.0f * UI_SCALE;       // room for glow + label

    placeFloatingBlock(totalW + pad * 2, totalH + pad * 2);
    float x = m_posX + pad;
    float y = m_posY + pad;

    QColor on = m_currentTheme.colors.secondHand.isValid()
              ? m_currentTheme.colors.secondHand : QColor(31, 227, 255);

    painter.setRenderHint(QPainter::Antialiasing, true);

    // hour digits, colon, minute digits
    float cursor = x;
    for (int i = 0; i < hh.size(); i++) {
        drawSevenSegDigit(painter, cursor, y, dw, dh, th, hh.at(i).digitValue(), on);
        cursor += dw + dgap;
    }
    // colon (centered in the colon gap)
    float colonCenterX = cursor - dgap * 0.5f + colonW * 0.5f;
    QColor cglow = on; cglow.setAlphaF(0.3f);
    float cy1 = y + dh * 0.34f, cy2 = y + dh * 0.66f;
    painter.setPen(Qt::NoPen);
    painter.setBrush(cglow);
    painter.drawEllipse(QPointF(colonCenterX, cy1), th, th);
    painter.drawEllipse(QPointF(colonCenterX, cy2), th, th);
    painter.setBrush(on);
    painter.drawEllipse(QPointF(colonCenterX, cy1), th * 0.6f, th * 0.6f);
    painter.drawEllipse(QPointF(colonCenterX, cy2), th * 0.6f, th * 0.6f);
    cursor += colonW;
    for (int i = 0; i < mm.size(); i++) {
        drawSevenSegDigit(painter, cursor, y, dw, dh, th, mm.at(i).digitValue(), on);
        cursor += dw + dgap;
    }

    // AM/PM + seconds label
    QFont lf("DejaVu Sans Mono");
    lf.setPixelSize(static_cast<int>(13 * UI_SCALE));
    lf.setBold(true);
    painter.setFont(lf);
    QColor lcol = on; lcol.setAlphaF(0.6f);
    painter.setPen(lcol);
    QString label = QString(tm.hour() >= 12 ? "PM" : "AM") +
                    QString("   %1s").arg(tm.second(), 2, 10, QChar('0'));
    painter.drawText(QRectF(x, y + dh + 6 * UI_SCALE, totalW, 20 * UI_SCALE),
                     Qt::AlignCenter, label);
}

// --- Split-flap / Solari ---

void ScreenSaverView::paintDigitalSplitFlap(QPainter &painter)
{
    QTime tm = QTime::currentTime();
    int hour = tm.hour() % 12; if (hour == 0) hour = 12;
    QString hh = QString("%1").arg(hour, 2, 10, QChar('0'));
    QString mm = QString("%1").arg(tm.minute(), 2, 10, QChar('0'));

    float tileW = 110.0f * UI_SCALE;
    float tileH = 150.0f * UI_SCALE;
    float gap   = 18.0f * UI_SCALE;
    float labelH = 26.0f * UI_SCALE;
    float totalW = tileW * 2 + gap;
    float totalH = tileH + labelH;

    placeFloatingBlock(totalW, totalH);
    float x0 = m_posX;
    float y0 = m_posY;

    painter.setRenderHint(QPainter::Antialiasing, true);

    auto drawTile = [&](float x, const QString &txt, const QString &lbl) {
        QRectF tile(x, y0, tileW, tileH);
        QLinearGradient g(x, y0, x, y0 + tileH);
        g.setColorStop(0.0,  QColor(52, 52, 60));
        g.setColorStop(0.49, QColor(38, 38, 44));
        g.setColorStop(0.51, QColor(25, 25, 32));
        g.setColorStop(1.0,  QColor(32, 32, 40));
        painter.setPen(QPen(QColor(0, 0, 0), 1.0f * UI_SCALE));
        painter.setBrush(g);
        painter.drawRoundedRect(tile, 10 * UI_SCALE, 10 * UI_SCALE);

        // center seam
        painter.setPen(QPen(QColor(5, 5, 7), 3.0f * UI_SCALE));
        painter.drawLine(QPointF(x, y0 + tileH * 0.5f),
                         QPointF(x + tileW, y0 + tileH * 0.5f));
        // side axles
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(10, 10, 12));
        painter.drawRect(QRectF(x - 4 * UI_SCALE, y0 + tileH * 0.5f - 6 * UI_SCALE,
                                4 * UI_SCALE, 12 * UI_SCALE));
        painter.drawRect(QRectF(x + tileW, y0 + tileH * 0.5f - 6 * UI_SCALE,
                                4 * UI_SCALE, 12 * UI_SCALE));
        // numerals
        QFont f("DejaVu Sans");
        f.setBold(true);
        f.setStretch(QFont::Condensed);
        f.setPixelSize(static_cast<int>(tileH * 0.62f));
        painter.setFont(f);
        painter.setPen(QColor(244, 244, 242));
        painter.drawText(tile, Qt::AlignCenter, txt);
        // label
        QFont lf("DejaVu Sans"); lf.setBold(true);
        lf.setPixelSize(static_cast<int>(13 * UI_SCALE));
        painter.setFont(lf);
        painter.setPen(QColor(154, 154, 160));
        painter.drawText(QRectF(x, y0 + tileH + 4 * UI_SCALE, tileW, labelH),
                         Qt::AlignCenter, lbl);
    };

    drawTile(x0, hh, "HOURS");
    drawTile(x0 + tileW + gap, mm, "MINUTES");
}

// --- Nixie tubes ---

void ScreenSaverView::paintDigitalNixie(QPainter &painter)
{
    QTime tm = QTime::currentTime();
    int hour = tm.hour() % 12; if (hour == 0) hour = 12;
    QString digits = QString("%1%2")
                       .arg(hour, 2, 10, QChar('0'))
                       .arg(tm.minute(), 2, 10, QChar('0'));

    float tubeW = 60.0f * UI_SCALE;
    float tubeH = 140.0f * UI_SCALE;
    float gap   = 12.0f * UI_SCALE;
    float totalW = tubeW * 4 + gap * 3;
    float totalH = tubeH;
    float pad = 22.0f * UI_SCALE;

    placeFloatingBlock(totalW + pad * 2, totalH + pad * 2);
    float x = m_posX + pad;
    float y = m_posY + pad;

    QColor orange = m_currentTheme.colors.secondHand.isValid()
                  ? m_currentTheme.colors.secondHand : QColor(255, 138, 50);

    painter.setRenderHint(QPainter::Antialiasing, true);

    auto glowText = [&](const QString &s, const QFont &f, const QRectF &box,
                        const QColor &col, float glowW) {
        QFontMetrics fm(f);
        QRectF tb = fm.boundingRect(box.toRect(), Qt::AlignCenter, s);
        QPainterPath path;
        path.addText(tb.left(), box.center().y() + fm.height() * 0.32f, f, s);
        QColor outer = col; outer.setAlphaF(0.18f);
        painter.setPen(QPen(outer, glowW, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(path);
        QColor mid = col; mid.setAlphaF(0.4f);
        painter.setPen(QPen(mid, glowW * 0.4f, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawPath(path);
        painter.setPen(Qt::NoPen);
        painter.setBrush(col.lighter(120));
        painter.drawPath(path);
    };

    QFont df("DejaVu Sans Mono");
    df.setBold(true);
    df.setPixelSize(static_cast<int>(tubeH * 0.58f));

    for (int i = 0; i < 4; i++) {
        QRectF tube(x, y, tubeW, tubeH);
        // glass envelope
        QLinearGradient gg(x, y, x, y + tubeH);
        gg.setColorStop(0.0, QColor(60, 55, 48, 140));
        gg.setColorStop(0.5, QColor(30, 28, 26, 90));
        gg.setColorStop(1.0, QColor(50, 46, 40, 140));
        painter.setPen(QPen(QColor(120, 110, 95, 130), 1.0f * UI_SCALE));
        painter.setBrush(gg);
        painter.drawRoundedRect(tube, tubeW * 0.45f, tubeW * 0.45f);
        // caps
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(34, 29, 24));
        painter.drawRoundedRect(QRectF(x + tubeW * 0.18f, y - 6 * UI_SCALE,
                                       tubeW * 0.64f, 8 * UI_SCALE), 3, 3);
        painter.drawRoundedRect(QRectF(x + tubeW * 0.18f, y + tubeH - 2 * UI_SCALE,
                                       tubeW * 0.64f, 8 * UI_SCALE), 3, 3);
        // ghost cathode
        painter.setFont(df);
        QColor ghost = orange; ghost.setAlphaF(0.08f);
        painter.setPen(ghost);
        painter.drawText(tube, Qt::AlignCenter, "8");
        // lit digit
        glowText(QString(digits.at(i)), df, tube, orange, 7.0f * UI_SCALE);
        x += tubeW + gap;
    }

    // neon colon
    float midX = m_posX + pad + totalW * 0.5f;
    QColor cglow = orange; cglow.setAlphaF(0.5f);
    painter.setPen(Qt::NoPen);
    painter.setBrush(cglow);
    painter.drawEllipse(QPointF(midX, y + tubeH * 0.38f), 4 * UI_SCALE, 4 * UI_SCALE);
    painter.drawEllipse(QPointF(midX, y + tubeH * 0.62f), 4 * UI_SCALE, 4 * UI_SCALE);
}

// --- Terminal / CRT ---

void ScreenSaverView::paintDigitalTerminal(QPainter &painter)
{
    QDateTime now = QDateTime::currentDateTime();
    QTime tm = now.time();
    int hour = tm.hour() % 12; if (hour == 0) hour = 12;
    QString timeLine = QString("%1:%2:%3 %4")
                         .arg(hour)
                         .arg(tm.minute(), 2, 10, QChar('0'))
                         .arg(tm.second(), 2, 10, QChar('0'))
                         .arg(QString(tm.hour() >= 12 ? "PM" : "AM"));
    QString dateLine = now.toString("ddd MMM d yyyy");
    const QString prompt = "linamp@player:~$";

    QColor green = m_currentTheme.colors.secondHand.isValid()
                 ? m_currentTheme.colors.secondHand : QColor(54, 255, 116);
    QColor dim   = green.darker(170);

    QFont mono("DejaVu Sans Mono");
    mono.setPixelSize(static_cast<int>(16 * UI_SCALE));
    QFont big("DejaVu Sans Mono");
    big.setBold(true);
    big.setPixelSize(static_cast<int>(34 * UI_SCALE));

    QFontMetrics fmM(mono), fmB(big);
    float pad = 22.0f * UI_SCALE;
    float lh  = fmM.height() * 1.35f;
    float bigH = fmB.height();
    float contentW = qMax(qMax(fmM.horizontalAdvance(prompt + " date"),
                               fmB.horizontalAdvance(timeLine)),
                          fmM.horizontalAdvance(prompt + "  "));
    float panelW = contentW + pad * 2;
    float panelH = pad * 2 + lh + bigH + lh * 2.2f;

    placeFloatingBlock(panelW, panelH);
    QRectF panel(m_posX, m_posY, panelW, panelH);

    painter.setRenderHint(QPainter::Antialiasing, true);

    // CRT screen panel
    painter.setPen(QPen(green.darker(220), 1.0f * UI_SCALE));
    painter.setBrush(QColor(2, 6, 4));
    painter.drawRoundedRect(panel, 10 * UI_SCALE, 10 * UI_SCALE);

    painter.save();
    QPainterPath clip;
    clip.addRoundedRect(panel, 10 * UI_SCALE, 10 * UI_SCALE);
    painter.setClipPath(clip);

    float tx = m_posX + pad;
    float ty = m_posY + pad + fmM.ascent();

    painter.setFont(mono);
    painter.setPen(green);
    painter.drawText(QPointF(tx, ty), prompt + " date");
    ty += lh + bigH * 0.2f;

    painter.setFont(big);
    painter.drawText(QPointF(tx, ty), timeLine);
    ty += bigH * 0.55f + lh;

    painter.setFont(mono);
    painter.setPen(dim);
    painter.drawText(QPointF(tx, ty), dateLine);
    ty += lh * 1.2f;

    painter.setPen(green);
    painter.drawText(QPointF(tx, ty), prompt);

    // Blinking cursor block
    bool cursorOn = (now.toMSecsSinceEpoch() / 500) % 2 == 0;
    if (cursorOn) {
        float cxp = tx + fmM.horizontalAdvance(prompt + " ");
        painter.fillRect(QRectF(cxp, ty - fmM.ascent() * 0.85f,
                                fmM.averageCharWidth(), fmM.ascent()), green);
    }

    // Scanlines
    painter.setPen(QPen(QColor(0, 0, 0, 46), 1.0f));
    for (float yy = panel.top(); yy < panel.bottom(); yy += 3.0f * UI_SCALE)
        painter.drawLine(QPointF(panel.left(), yy), QPointF(panel.right(), yy));

    painter.restore();
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
