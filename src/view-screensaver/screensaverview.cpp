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
#include <QSet>
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
    start(-1);
}

void ScreenSaverView::start(int themeIndex)
{
    m_hue = QRandomGenerator::global()->bounded(360);
    m_posInit = false;
    m_posX = -1;
    m_posY = -1;

    auto themes = getAllClockThemes();
    if (themeIndex < 0 || themeIndex >= themes.size())
        themeIndex = QRandomGenerator::global()->bounded(themes.size());

    m_themeIndex = themeIndex;
    m_currentTheme = themes[m_themeIndex];
    m_clockMode = m_currentTheme.isDigital ? Digital : Analog;
    m_pongInit = false;
}

QStringList ScreenSaverView::faceNames()
{
    QStringList names;
    for (const ClockTheme &t : getAllClockThemes())
        names << QString::fromUtf8(t.name);
    return names;
}

int ScreenSaverView::faceIndexForName(const QString &name)
{
    auto themes = getAllClockThemes();
    for (int i = 0; i < themes.size(); ++i)
        if (name.compare(QString::fromUtf8(themes[i].name), Qt::CaseInsensitive) == 0)
            return i;
    return -1;
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
    // Center on first paint (use a dedicated flag, not the sign of m_posX —
    // normal leftward/upward drift can step m_posX slightly negative, which
    // must bounce, not re-center).
    if (!m_posInit) {
        m_posX = (width()  - totalW) * 0.5f;
        m_posY = (height() - totalH) * 0.5f;
        m_posInit = true;
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
    case DigitalStyle::VFD:          paintDigitalVFD(painter);       break;
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
    if (!m_posInit) {
        m_posX = (width() - totalW) * 0.35f;
        m_posY = (height() - totalH) * 0.4f;
        m_posInit = true;
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
        grad.setColorAt(0.0, mid.lighter(135));
        grad.setColorAt(0.6, mid);
        grad.setColorAt(1.0, edge);
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

    if (theme.orbital)   { paintOrbitalClock(painter);   return; }
    if (theme.wandering) { paintWanderingClock(painter); return; }
    if (theme.regulator) { paintRegulatorClock(painter); return; }
    if (theme.wordClock) { paintWordClock(painter);      return; }
    if (theme.berlinUhr) { paintBerlinUhr(painter);      return; }
    if (theme.pong)      { paintPongClock(painter);      return; }
    if (theme.binary)    { paintBinaryClock(painter);    return; }
    if (theme.fibonacci) { paintFibonacciClock(painter); return; }
    if (theme.sundial)   { paintSundialClock(painter);   return; }
    if (theme.flipDot)   { paintFlipDotClock(painter);   return; }

    float radius = qMin(W, H) * theme.dialRadiusFraction;
    float dialSize = radius * 2.0f + 8.0f * UI_SCALE; // bounding box with margin

    // Initialize position to center on first paint
    if (!m_posInit) {
        m_posX = (W - dialSize) * 0.5f;
        m_posY = (H - dialSize) * 0.5f;
        m_posInit = true;
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

    float dh = 32.0f * UI_SCALE;
    float dw = 17.0f * UI_SCALE;
    float th = 3.5f  * UI_SCALE;       // segment thickness
    float dgap = 4.5f * UI_SCALE;
    float colonW = 9.0f * UI_SCALE;

    QString digits = hh + mm;
    int nDigits = digits.size();
    float totalW = nDigits * dw + (nDigits - 1) * dgap + colonW;
    float totalH = dh;
    float pad = 15.0f * UI_SCALE;       // room for glow + label

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
    lf.setPixelSize(static_cast<int>(8 * UI_SCALE));
    lf.setBold(true);
    painter.setFont(lf);
    QColor lcol = on; lcol.setAlphaF(0.6f);
    painter.setPen(lcol);
    QString label = QString(tm.hour() >= 12 ? "PM" : "AM") +
                    QString("   %1s").arg(tm.second(), 2, 10, QChar('0'));
    painter.drawText(QRectF(x, y + dh + 4 * UI_SCALE, totalW, 12 * UI_SCALE),
                     Qt::AlignCenter, label);
}

// --- Split-flap / Solari ---

void ScreenSaverView::paintDigitalSplitFlap(QPainter &painter)
{
    QTime tm = QTime::currentTime();
    int hour = tm.hour() % 12; if (hour == 0) hour = 12;
    QString hh = QString("%1").arg(hour, 2, 10, QChar('0'));
    QString mm = QString("%1").arg(tm.minute(), 2, 10, QChar('0'));
    QString ss = QString("%1").arg(tm.second(), 2, 10, QChar('0'));

    // Scale to fit the display height (caps at UI_SCALE on tall screens).
    float s = qMin(static_cast<float>(UI_SCALE), height() * 0.0035f);

    float tileW = 110.0f * s;
    float tileH = 150.0f * s;
    float gap   = 18.0f * s;
    float labelH = 26.0f * s;
    float totalW = tileW * 3 + gap * 2;
    float totalH = tileH + labelH;

    placeFloatingBlock(totalW, totalH);
    float x0 = m_posX;
    float y0 = m_posY;

    painter.setRenderHint(QPainter::Antialiasing, true);

    auto drawTile = [&](float x, const QString &txt, const QString &lbl) {
        QRectF tile(x, y0, tileW, tileH);
        QLinearGradient g(x, y0, x, y0 + tileH);
        g.setColorAt(0.0,  QColor(52, 52, 60));
        g.setColorAt(0.49, QColor(38, 38, 44));
        g.setColorAt(0.51, QColor(25, 25, 32));
        g.setColorAt(1.0,  QColor(32, 32, 40));
        painter.setPen(QPen(QColor(0, 0, 0), 1.0f * s));
        painter.setBrush(g);
        painter.drawRoundedRect(tile, 10 * s, 10 * s);

        // center seam
        painter.setPen(QPen(QColor(5, 5, 7), 3.0f * s));
        painter.drawLine(QPointF(x, y0 + tileH * 0.5f),
                         QPointF(x + tileW, y0 + tileH * 0.5f));
        // side axles
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(10, 10, 12));
        painter.drawRect(QRectF(x - 4 * s, y0 + tileH * 0.5f - 6 * s, 4 * s, 12 * s));
        painter.drawRect(QRectF(x + tileW, y0 + tileH * 0.5f - 6 * s, 4 * s, 12 * s));
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
        lf.setPixelSize(static_cast<int>(13 * s));
        painter.setFont(lf);
        painter.setPen(QColor(154, 154, 160));
        painter.drawText(QRectF(x, y0 + tileH + 4 * s, tileW, labelH),
                         Qt::AlignCenter, lbl);
    };

    drawTile(x0, hh, "HOURS");
    drawTile(x0 + tileW + gap, mm, "MINUTES");
    drawTile(x0 + (tileW + gap) * 2, ss, "SECONDS");
}

// --- Nixie tubes ---

void ScreenSaverView::paintDigitalNixie(QPainter &painter)
{
    QTime tm = QTime::currentTime();
    int hour = tm.hour() % 12; if (hour == 0) hour = 12;
    QString digits = QString("%1%2")
                       .arg(hour, 2, 10, QChar('0'))
                       .arg(tm.minute(), 2, 10, QChar('0'));

    // Scale to fit the display height (caps at UI_SCALE on tall screens).
    float s = qMin(static_cast<float>(UI_SCALE), height() * 0.0033f);
    float tubeW = 60.0f * s;
    float tubeH = 140.0f * s;
    float gap   = 12.0f * s;
    float totalW = tubeW * 4 + gap * 3;
    float totalH = tubeH;
    float pad = 22.0f * s;

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
        gg.setColorAt(0.0, QColor(60, 55, 48, 140));
        gg.setColorAt(0.5, QColor(30, 28, 26, 90));
        gg.setColorAt(1.0, QColor(50, 46, 40, 140));
        painter.setPen(QPen(QColor(120, 110, 95, 130), 1.0f * s));
        painter.setBrush(gg);
        painter.drawRoundedRect(tube, tubeW * 0.45f, tubeW * 0.45f);
        // caps
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(34, 29, 24));
        painter.drawRoundedRect(QRectF(x + tubeW * 0.18f, y - 6 * s,
                                       tubeW * 0.64f, 8 * s), 3, 3);
        painter.drawRoundedRect(QRectF(x + tubeW * 0.18f, y + tubeH - 2 * s,
                                       tubeW * 0.64f, 8 * s), 3, 3);
        // ghost cathode
        painter.setFont(df);
        QColor ghost = orange; ghost.setAlphaF(0.08f);
        painter.setPen(ghost);
        painter.drawText(tube, Qt::AlignCenter, "8");
        // lit digit
        glowText(QString(digits.at(i)), df, tube, orange, 7.0f * s);
        x += tubeW + gap;
    }

    // neon colon
    float midX = m_posX + pad + totalW * 0.5f;
    QColor cglow = orange; cglow.setAlphaF(0.5f);
    painter.setPen(Qt::NoPen);
    painter.setBrush(cglow);
    painter.drawEllipse(QPointF(midX, y + tubeH * 0.38f), 4 * s, 4 * s);
    painter.drawEllipse(QPointF(midX, y + tubeH * 0.62f), 4 * s, 4 * s);
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

    // Scale to fit the display height (caps at UI_SCALE on tall screens).
    float s = qMin(static_cast<float>(UI_SCALE), height() * 0.0032f);
    QFont mono("DejaVu Sans Mono");
    mono.setPixelSize(static_cast<int>(16 * s));
    QFont big("DejaVu Sans Mono");
    big.setBold(true);
    big.setPixelSize(static_cast<int>(34 * s));

    QFontMetrics fmM(mono), fmB(big);
    float pad = 22.0f * s;
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
    painter.setPen(QPen(green.darker(220), 1.0f * s));
    painter.setBrush(QColor(2, 6, 4));
    painter.drawRoundedRect(panel, 10 * s, 10 * s);

    painter.save();
    QPainterPath clip;
    clip.addRoundedRect(panel, 10 * s, 10 * s);
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
    for (float yy = panel.top(); yy < panel.bottom(); yy += 3.0f * s)
        painter.drawLine(QPointF(panel.left(), yy), QPointF(panel.right(), yy));

    painter.restore();
}

// --- VFD: vacuum-fluorescent display (reuses the seven-segment digit) ---

void ScreenSaverView::paintDigitalVFD(QPainter &painter)
{
    QTime tm = QTime::currentTime();
    int hour = tm.hour() % 12; if (hour == 0) hour = 12;
    QString hh = QString("%1").arg(hour, 2, 10, QChar(' '));   // VFD blanks the leading zero
    QString mm = QString("%1").arg(tm.minute(), 2, 10, QChar('0'));

    float u = qMin(height() / 400.0f, width() / 1280.0f);
    float dh = 170.0f * u, dw = 104.0f * u, th = dh * 0.10f;
    float gap = 26.0f * u, colonW = 46.0f * u;
    float digitsW = dw * 4 + gap * 2 + colonW;
    float padX = 56.0f * u, padY = 64.0f * u;
    float panelW = digitsW + padX * 2;
    float panelH = dh + padY * 2;

    QPointF tl = placeFloatingBlock(panelW, panelH);
    QRectF panel(tl.x(), tl.y(), panelW, panelH);

    painter.setRenderHint(QPainter::Antialiasing, true);

    // Smoky glass panel
    QLinearGradient pg(panel.topLeft(), panel.bottomLeft());
    pg.setColorAt(0.0, QColor(12, 20, 22));
    pg.setColorAt(0.5, QColor(10, 16, 18));
    pg.setColorAt(1.0, QColor(7, 12, 13));
    painter.setPen(QPen(QColor(80, 120, 120, 70), 2.0f * u));
    painter.setBrush(pg);
    painter.drawRoundedRect(panel, 14.0f * u, 14.0f * u);

    QColor teal = m_currentTheme.colors.secondHand.isValid()
                ? m_currentTheme.colors.secondHand : QColor(52, 231, 200);

    float x = panel.left() + (panelW - digitsW) * 0.5f;
    float y = panel.center().y() - dh * 0.5f;

    drawSevenSegDigit(painter, x, y, dw, dh, th, hh.at(0).digitValue(), teal); x += dw + gap;
    drawSevenSegDigit(painter, x, y, dw, dh, th, hh.at(1).digitValue(), teal); x += dw;

    // Colon (blinks)
    float colonX = x + colonW * 0.5f;
    x += colonW;
    if (tm.msec() < 500) {
        QColor cg = teal; cg.setAlphaF(0.30f);
        painter.setPen(Qt::NoPen); painter.setBrush(cg);
        painter.drawEllipse(QPointF(colonX, y + dh * 0.34f), th, th);
        painter.drawEllipse(QPointF(colonX, y + dh * 0.66f), th, th);
        painter.setBrush(teal);
        painter.drawEllipse(QPointF(colonX, y + dh * 0.34f), th * 0.6f, th * 0.6f);
        painter.drawEllipse(QPointF(colonX, y + dh * 0.66f), th * 0.6f, th * 0.6f);
    }

    drawSevenSegDigit(painter, x, y, dw, dh, th, mm.at(0).digitValue(), teal); x += dw + gap;
    drawSevenSegDigit(painter, x, y, dw, dh, th, mm.at(1).digitValue(), teal);

    // AM/PM, upper-right inside the glass
    QFont lf("DejaVu Sans Mono"); lf.setBold(true);
    lf.setPixelSize(static_cast<int>(26.0f * u));
    painter.setFont(lf);
    painter.setPen(teal);
    painter.drawText(QRectF(panel.right() - 90.0f * u, panel.top() + 14.0f * u,
                            76.0f * u, 30.0f * u),
                     Qt::AlignRight | Qt::AlignVCenter, tm.hour() >= 12 ? "PM" : "AM");

    // Fine wire mesh overlay, clipped to the glass
    painter.save();
    QPainterPath clip; clip.addRoundedRect(panel, 14.0f * u, 14.0f * u);
    painter.setClipPath(clip);
    painter.setPen(QPen(QColor(120, 180, 170, 13), 1.0f));
    for (float gx = panel.left(); gx < panel.right(); gx += 7.0f * u)
        painter.drawLine(QPointF(gx, panel.top()), QPointF(gx, panel.bottom()));
    for (float gy = panel.top(); gy < panel.bottom(); gy += 7.0f * u)
        painter.drawLine(QPointF(panel.left(), gy), QPointF(panel.right(), gy));
    painter.restore();
}

// --- Wandering Hours: satellite complication ---

void ScreenSaverView::paintWanderingClock(QPainter &painter)
{
    const ClockTheme &theme = m_currentTheme;
    float u = qMin(height() / 400.0f, width() / 1280.0f);

    float blockW = 720.0f * u, blockH = 344.0f * u;
    QPointF tl = placeFloatingBlock(blockW, blockH);
    float cx = tl.x() + blockW * 0.5f;
    float cy = tl.y() + blockH * 0.57f;          // carousel pivot, with the arc overhead

    QTime t = QTime::currentTime();
    int H = t.hour();
    float mF = t.minute() + (t.second() + t.msec() / 1000.0f) / 60.0f;

    painter.setRenderHint(QPainter::Antialiasing, true);

    const float aR = 160.0f * u;
    const float a0 = static_cast<float>(M_PI) * 1.12f;
    const float a1 = static_cast<float>(M_PI) * 1.88f;
    auto onArc = [&](float ang) { return QPointF(cx + cosf(ang) * aR, cy + sinf(ang) * aR); };

    QColor arcCol = theme.colors.minuteHand;
    QColor accent = theme.colors.hourHand;

    // Arc baseline (minute scale, overhead)
    QPainterPath arcPath; arcPath.moveTo(onArc(a0));
    for (int i = 1; i <= 90; i++) arcPath.lineTo(onArc(a0 + (a1 - a0) * i / 90.0f));
    QColor faint = arcCol; faint.setAlphaF(0.16f);
    painter.setPen(QPen(faint, 2.0f * u, Qt::SolidLine, Qt::RoundCap));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(arcPath);

    // Minute ticks + labels
    QFont nf("DejaVu Sans"); nf.setPixelSize(static_cast<int>(14.0f * u));
    painter.setFont(nf);
    for (int m = 0; m <= 60; m += 5) {
        float ang = a0 + (a1 - a0) * (m / 60.0f);
        float dx = cosf(ang), dy = sinf(ang);
        QColor tc = arcCol; tc.setAlphaF(0.4f);
        painter.setPen(QPen(tc, 2.0f * u));
        painter.drawLine(QPointF(cx + dx * aR, cy + dy * aR),
                         QPointF(cx + dx * (aR + 11.0f * u), cy + dy * (aR + 11.0f * u)));
        QColor lc = arcCol; lc.setAlphaF(0.55f);
        painter.setPen(lc);
        painter.drawText(QRectF(cx + dx * (aR + 24.0f * u) - 17.0f * u,
                                cy + dy * (aR + 24.0f * u) - 11.0f * u, 34.0f * u, 22.0f * u),
                         Qt::AlignCenter, QString::number(m));
    }

    // Active-minute marker (just outside the disc, on the scale)
    float aAct = a0 + (a1 - a0) * (mF / 60.0f);
    {
        QPointF mk = onArc(aAct);
        QColor g = accent; g.setAlphaF(0.4f);
        painter.setPen(Qt::NoPen); painter.setBrush(g);
        painter.drawEllipse(mk, 9.0f * u, 9.0f * u);
        painter.setBrush(accent);
        painter.drawEllipse(mk, 5.0f * u, 5.0f * u);
    }

    // Three hour discs on the carousel (active rides just under the active minute)
    int h12 = (H % 12); if (h12 == 0) h12 = 12;
    struct Disc { int h; float ang; bool on; };
    Disc discs[3] = {
        { ((h12 + 10) % 12) + 1, aAct - 0.72f, false },
        { h12,                   aAct,         true  },
        { (h12 % 12) + 1,        aAct + 0.72f, false }
    };
    const float dr = 116.0f * u, discR = 38.0f * u;

    // Carousel hub
    painter.setBrush(QColor(28, 31, 39));
    painter.setPen(QPen(QColor(160, 170, 190, 64), 2.0f * u));
    painter.drawEllipse(QPointF(cx, cy), 34.0f * u, 34.0f * u);

    QFont df("DejaVu Sans"); df.setBold(true); df.setPixelSize(static_cast<int>(31.0f * u));
    painter.setFont(df);
    for (const Disc &d : discs) {
        float dx = cosf(d.ang), dy = sinf(d.ang);
        float x = cx + dx * dr, y = cy + dy * dr;
        QColor arm = QColor(150, 160, 180); arm.setAlphaF(d.on ? 0.5f : 0.12f);
        painter.setPen(QPen(arm, 3.0f * u)); painter.setBrush(Qt::NoBrush);
        painter.drawLine(QPointF(cx, cy), QPointF(x, y));

        if (d.on) {
            QColor glow = accent; glow.setAlphaF(0.5f);
            painter.setPen(Qt::NoPen); painter.setBrush(glow);
            painter.drawEllipse(QPointF(x, y), discR + 6.0f * u, discR + 6.0f * u);
            painter.setBrush(QColor(16, 32, 43));
            painter.setPen(QPen(accent, 2.5f * u));
        } else {
            painter.setBrush(QColor(21, 23, 29));
            painter.setPen(QPen(QColor(140, 150, 170, 64), 2.0f * u));
        }
        painter.drawEllipse(QPointF(x, y), discR, discR);
        painter.setPen(d.on ? QColor(234, 246, 255) : QColor(160, 170, 190, 110));
        painter.drawText(QRectF(x - discR, y - discR, discR * 2, discR * 2),
                         Qt::AlignCenter, QString::number(d.h));
    }
}

// --- Regulator: separate H / M / S sub-dials ---

void ScreenSaverView::paintRegulatorClock(QPainter &painter)
{
    const ClockTheme &theme = m_currentTheme;
    float u = qMin(height() / 400.0f, width() / 1280.0f);

    float blockW = 940.0f * u, blockH = 356.0f * u;
    QPointF tl = placeFloatingBlock(blockW, blockH);
    float cxc = tl.x() + blockW * 0.5f;
    float baseY = tl.y() + blockH * 0.46f;

    QTime t = QTime::currentTime();
    float hours   = (t.hour() % 12) + t.minute() / 60.0f;
    float minutes = t.minute() + t.second() / 60.0f;
    float seconds = t.second() + t.msec() / 1000.0f;

    painter.setRenderHint(QPainter::Antialiasing, true);

    auto dial = [&](float cx, float cy, float R, float val, float maxVal,
                    float handLen, const QString &label, bool big) {
        QRadialGradient fg(cx, cy - R * 0.3f, R);
        fg.setColorAt(0.0, QColor(27, 32, 41));
        fg.setColorAt(1.0, QColor(14, 17, 22));
        painter.setBrush(fg);
        painter.setPen(QPen(QColor(190, 200, 220, 46), 2.0f * u));
        painter.drawEllipse(QPointF(cx, cy), R, R);

        int ticks = (maxVal == 12.0f) ? 12 : 60;
        for (int i = 0; i < ticks; i++) {
            float a = -static_cast<float>(M_PI) / 2 + 2.0f * static_cast<float>(M_PI) * i / ticks;
            bool maj = (ticks == 12) ? true : (i % 5 == 0);
            float r1 = maj ? R - 14.0f * u : R - 8.0f * u, r2 = R - 3.0f * u;
            QColor tc = maj ? QColor(220, 228, 240, 178) : QColor(180, 190, 205, 76);
            painter.setPen(QPen(tc, maj ? 2.5f * u : 1.2f * u));
            painter.drawLine(QPointF(cx + cosf(a) * r1, cy + sinf(a) * r1),
                             QPointF(cx + cosf(a) * r2, cy + sinf(a) * r2));
        }

        if (maxVal == 12.0f) {
            QFont nf("Georgia"); nf.setBold(true); nf.setPixelSize(static_cast<int>(18.0f * u));
            painter.setFont(nf); painter.setPen(QColor(225, 232, 245, 217));
            for (int n = 1; n <= 12; n++) {
                float a = -static_cast<float>(M_PI) / 2 + 2.0f * static_cast<float>(M_PI) * n / 12.0f;
                painter.drawText(QRectF(cx + cosf(a) * (R - 30.0f * u) - 14.0f * u,
                                        cy + sinf(a) * (R - 30.0f * u) - 12.0f * u,
                                        28.0f * u, 24.0f * u),
                                 Qt::AlignCenter, QString::number(n));
            }
        }

        float a = -static_cast<float>(M_PI) / 2 + 2.0f * static_cast<float>(M_PI) * (val / maxVal);
        QColor hc = big ? theme.colors.minuteHand : theme.colors.hourHand;
        painter.setPen(QPen(hc, big ? 5.0f * u : 3.0f * u, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(QPointF(cx - cosf(a) * R * 0.14f, cy - sinf(a) * R * 0.14f),
                         QPointF(cx + cosf(a) * handLen, cy + sinf(a) * handLen));
        painter.setPen(Qt::NoPen); painter.setBrush(theme.colors.centerPin);
        painter.drawEllipse(QPointF(cx, cy), big ? 7.0f * u : 5.0f * u, big ? 7.0f * u : 5.0f * u);

        QFont lf("DejaVu Sans"); lf.setPixelSize(static_cast<int>(13.0f * u));
        painter.setFont(lf); painter.setPen(QColor(150, 160, 178, 204));
        painter.drawText(QRectF(cx - R, cy + R + 8.0f * u, R * 2, 20.0f * u),
                         Qt::AlignHCenter | Qt::AlignTop, label);
    };

    dial(cxc - 330.0f * u, baseY, 110.0f * u, hours,   12.0f, 86.0f * u,  "HOURS",   false);
    dial(cxc,              baseY, 145.0f * u, minutes, 60.0f, 115.0f * u, "MINUTES", true);
    dial(cxc + 330.0f * u, baseY, 110.0f * u, seconds, 60.0f, 86.0f * u,  "SECONDS", false);
}

// --- Word Clock: QLOCKTWO-style letter matrix ---

static const char *const WORD_GRID[10] = {
    "ITLISASAMPM", "ACQUARTERDC", "TWENTYFIVEX", "HALFBTENFTO", "PASTERUNINE",
    "ONESIXTHREE", "FOURFIVETWO", "EIGHTELEVEN", "SEVENTWELVE", "TENSEOCLOCK"
};

static QSet<int> wordClockLitCells(int hour24, int minute)
{
    const int C = 11;
    QSet<int> s;
    auto add = [&](int r, int c0, int c1) { for (int c = c0; c <= c1; c++) s.insert(r * C + c); };
    add(0, 0, 1); add(0, 3, 4);                       // IT IS

    int mm = (minute + 2) / 5, hh = hour24;
    if (mm == 12) { mm = 0; hh = hour24 + 1; }
    int m5 = mm * 5;
    bool toCase = m5 > 30;
    int dh = (toCase ? hh + 1 : hh) % 12; if (dh == 0) dh = 12;

    switch (m5) {
    case 5:  add(2, 6, 9); add(4, 0, 3); break;                 // FIVE PAST
    case 10: add(3, 5, 7); add(4, 0, 3); break;                 // TEN PAST
    case 15: add(1, 2, 8); add(4, 0, 3); break;                 // QUARTER PAST
    case 20: add(2, 0, 5); add(4, 0, 3); break;                 // TWENTY PAST
    case 25: add(2, 0, 5); add(2, 6, 9); add(4, 0, 3); break;   // TWENTY FIVE PAST
    case 30: add(3, 0, 3); add(4, 0, 3); break;                 // HALF PAST
    case 35: add(2, 0, 5); add(2, 6, 9); add(3, 9, 10); break;  // TWENTY FIVE TO
    case 40: add(2, 0, 5); add(3, 9, 10); break;                // TWENTY TO
    case 45: add(1, 2, 8); add(3, 9, 10); break;                // QUARTER TO
    case 50: add(3, 5, 7); add(3, 9, 10); break;                // TEN TO
    case 55: add(2, 6, 9); add(3, 9, 10); break;                // FIVE TO
    default: break;                                             // :00 -> O'CLOCK
    }

    switch (dh) {
    case 1:  add(5, 0, 2);  break;  case 2:  add(6, 8, 10); break;
    case 3:  add(5, 6, 10); break;  case 4:  add(6, 0, 3);  break;
    case 5:  add(6, 4, 7);  break;  case 6:  add(5, 3, 5);  break;
    case 7:  add(8, 0, 4);  break;  case 8:  add(7, 0, 4);  break;
    case 9:  add(4, 7, 10); break;  case 10: add(9, 0, 2);  break;
    case 11: add(7, 5, 10); break;  case 12: add(8, 5, 10); break;
    }
    if (m5 == 0) add(9, 5, 10);     // O'CLOCK
    return s;
}

void ScreenSaverView::paintWordClock(QPainter &painter)
{
    const ClockTheme &theme = m_currentTheme;
    float u = qMin(height() / 400.0f, width() / 1280.0f);

    const int cols = 11, rows = 10;
    float cw = 44.0f * u, ch = 34.0f * u, pad = 20.0f * u;
    float gridW = cols * cw, gridH = rows * ch;

    QPointF tl = placeFloatingBlock(gridW + pad * 2, gridH + pad * 2);
    float gx = tl.x() + pad, gy = tl.y() + pad;

    QTime t = QTime::currentTime();
    QSet<int> lit = wordClockLitCells(t.hour(), t.minute());

    QFont f("DejaVu Sans Mono"); f.setBold(true); f.setPixelSize(static_cast<int>(29.0f * u));
    painter.setFont(f);
    painter.setRenderHint(QPainter::Antialiasing, true);

    QColor litCol  = theme.colors.numerals;
    QColor dimCol  = theme.colors.ticks;  dimCol.setAlphaF(0.30f);
    QColor glowCol = theme.colors.secondHand.isValid() ? theme.colors.secondHand : QColor(120, 200, 255);
    QFontMetricsF fm(f);

    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            QString ch1(QChar(WORD_GRID[r][c]));
            QRectF cell(gx + c * cw, gy + r * ch, cw, ch);
            if (lit.contains(r * cols + c)) {
                float bx = cell.center().x() - fm.horizontalAdvance(ch1) * 0.5f;
                float by = cell.center().y() + (fm.ascent() - fm.descent()) * 0.5f;
                QPainterPath path; path.addText(bx, by, f, ch1);
                QColor g1 = glowCol; g1.setAlphaF(0.35f);
                painter.setPen(QPen(g1, 5.0f * u, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                painter.setBrush(Qt::NoBrush); painter.drawPath(path);
                QColor g2 = glowCol; g2.setAlphaF(0.5f);
                painter.setPen(QPen(g2, 2.0f * u, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                painter.drawPath(path);
                painter.setPen(Qt::NoPen); painter.setBrush(litCol); painter.drawPath(path);
            } else {
                painter.setPen(dimCol);
                painter.drawText(cell, Qt::AlignCenter, ch1);
            }
        }
    }
}

// --- Berlin Uhr: Mengenlehreuhr set-theory lamp clock ---

void ScreenSaverView::paintBerlinUhr(QPainter &painter)
{
    float u = qMin(height() / 400.0f, width() / 1280.0f);

    float blockW = 1120.0f * u, blockH = 352.0f * u;
    QPointF tl = placeFloatingBlock(blockW, blockH);
    float cx = tl.x() + blockW * 0.5f, top = tl.y();

    QTime tm = QTime::currentTime();
    int H = tm.hour(), M = tm.minute(), S = tm.second();

    painter.setRenderHint(QPainter::Antialiasing, true);

    const QColor red(255, 59, 48),  redOff(58, 20, 18);
    const QColor yel(255, 214, 10), yelOff(58, 52, 16);
    const float lampH = 56.0f * u;

    auto lamp = [&](float x, float y, float w, bool on, const QColor &col, const QColor &off) {
        QRectF rc(x, y, w, lampH);
        if (on) {  // soft glow halo behind the lit lamp
            QColor g = col; g.setAlphaF(0.30f);
            painter.setPen(Qt::NoPen); painter.setBrush(g);
            painter.drawRoundedRect(rc.adjusted(-5.0f * u, -5.0f * u, 5.0f * u, 5.0f * u),
                                    8.0f * u, 8.0f * u);
        }
        painter.setBrush(on ? col : off);
        painter.setPen(QPen(QColor(0, 0, 0, 128), 1.5f * u));
        painter.drawRoundedRect(rc, 5.0f * u, 5.0f * u);
    };

    // Seconds lamp (round, blinks each second)
    {
        float r = 22.0f * u, y = top + 40.0f * u;
        bool on = (S % 2 == 0);
        if (on) {
            QColor g = yel; g.setAlphaF(0.30f);
            painter.setPen(Qt::NoPen); painter.setBrush(g);
            painter.drawEllipse(QPointF(cx, y), r + 5.0f * u, r + 5.0f * u);
        }
        painter.setBrush(on ? yel : yelOff);
        painter.setPen(QPen(QColor(0, 0, 0, 128), 1.5f * u));
        painter.drawEllipse(QPointF(cx, y), r, r);
    }

    float innerW = 1060.0f * u, x0 = cx - innerW * 0.5f;
    int h5 = H / 5, h1 = H % 5, m5 = M / 5, m1 = M % 5;

    auto row4 = [&](int litN, float y, const QColor &col, const QColor &off) {
        float gap = 10.0f * u, w = (innerW - gap * 3) / 4;
        for (int i = 0; i < 4; i++)
            lamp(x0 + i * (w + gap), y, w, i < litN, col, off);
    };

    row4(h5, top + 80.0f * u,  red, redOff);    // 5-hour lamps
    row4(h1, top + 146.0f * u, red, redOff);    // 1-hour lamps

    // 5-minute row: 11 lamps, quarters (3rd, 6th, 9th) are red
    {
        float gap = 8.0f * u, w = (innerW - gap * 10) / 11, y = top + 212.0f * u;
        for (int i = 0; i < 11; i++) {
            bool q = ((i + 1) % 3 == 0);
            lamp(x0 + i * (w + gap), y, w, i < m5, q ? red : yel, q ? redOff : yelOff);
        }
    }

    row4(m1, top + 278.0f * u, yel, yelOff);    // 1-minute lamps
}

// --- 5x7 dot font (shared by Pong score + Flip-Dot) ---

static const char *const DOT57[10][7] = {
    {"01110","10001","10011","10101","11001","10001","01110"}, // 0
    {"00100","01100","00100","00100","00100","00100","01110"}, // 1
    {"01110","10001","00001","00010","00100","01000","11111"}, // 2
    {"11111","00010","00100","00010","00001","10001","01110"}, // 3
    {"00010","00110","01010","10010","11111","00010","00010"}, // 4
    {"11111","10000","11110","00001","00001","10001","01110"}, // 5
    {"00110","01000","10000","11110","10001","10001","01110"}, // 6
    {"11111","00001","00010","00100","01000","01000","01000"}, // 7
    {"01110","10001","10001","01110","10001","10001","01110"}, // 8
    {"01110","10001","10001","01111","00001","00010","01100"}, // 9
};

static void drawDot57(QPainter &p, const QString &s, float x, float y, float dot, const QColor &col)
{
    p.setPen(Qt::NoPen); p.setBrush(col);
    float cx = x;
    for (const QChar &ch : s) {
        int d = ch.digitValue();
        if (d >= 0 && d <= 9)
            for (int r = 0; r < 7; r++)
                for (int c = 0; c < 5; c++)
                    if (DOT57[d][r][c] == '1')
                        p.drawRect(QRectF(cx + c * dot, y + r * dot, dot - 1.0f, dot - 1.0f));
        cx += 6.0f * dot;
    }
}

// --- Pong: the court self-plays, the score is the time ---

void ScreenSaverView::paintPongClock(QPainter &p)
{
    float u = qMin(height() / 400.0f, width() / 1280.0f);
    float bw = width() * 0.94f, bh = height() * 0.90f;
    QPointF tl = placeFloatingBlock(bw, bh);
    float ox = tl.x(), oy = tl.y();

    float ph = 84.0f * u, pw = 14.0f * u, lx = 30.0f * u, rx = bw - 30.0f * u - pw;
    float top = 8.0f * u, bot = bh - 8.0f * u, br = 7.0f * u;

    if (!m_pongInit) {
        m_pongBallX = bw * 0.5f; m_pongBallY = bh * 0.5f;
        m_pongVX = 4.4f * u; m_pongVY = 2.7f * u;
        m_pongPL = bh * 0.5f - ph * 0.5f; m_pongPR = m_pongPL;
        m_pongInit = true;
    }

    m_pongBallX += m_pongVX; m_pongBallY += m_pongVY;
    if (m_pongBallY < top + br) { m_pongBallY = top + br; m_pongVY = fabsf(m_pongVY); }
    if (m_pongBallY > bot - br) { m_pongBallY = bot - br; m_pongVY = -fabsf(m_pongVY); }

    m_pongPL += ((m_pongBallY - ph * 0.5f) - m_pongPL) * 0.09f;
    m_pongPR += ((m_pongBallY - ph * 0.5f) - m_pongPR) * 0.09f;
    m_pongPL = qBound(top, m_pongPL, bot - ph);
    m_pongPR = qBound(top, m_pongPR, bot - ph);

    // Continuous rally: the ball always bounces off the paddle plane (paddles
    // track to stay under it), with a little "english" so the angle varies.
    float maxVY = 5.0f * u;
    if (m_pongBallX < lx + pw + br) {
        m_pongBallX = lx + pw + br; m_pongVX = fabsf(m_pongVX);
        m_pongVY = qBound(-maxVY, m_pongVY + (m_pongBallY - (m_pongPL + ph * 0.5f)) / (ph * 0.5f) * 1.3f * u, maxVY);
    }
    if (m_pongBallX > rx - br) {
        m_pongBallX = rx - br; m_pongVX = -fabsf(m_pongVX);
        m_pongVY = qBound(-maxVY, m_pongVY + (m_pongBallY - (m_pongPR + ph * 0.5f)) / (ph * 0.5f) * 1.3f * u, maxVY);
    }

    QColor white = m_currentTheme.colors.numerals;
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setPen(Qt::NoPen); p.setBrush(white);
    p.drawRect(QRectF(ox, oy, bw, 6.0f * u));
    p.drawRect(QRectF(ox, oy + bh - 6.0f * u, bw, 6.0f * u));
    for (float y = oy + 12.0f * u; y < oy + bh - 12.0f * u; y += 30.0f * u)
        p.drawRect(QRectF(ox + bw * 0.5f - 4.0f * u, y, 8.0f * u, 16.0f * u));

    QTime tm = QTime::currentTime();
    int H = tm.hour() % 12; if (H == 0) H = 12;
    QString hh = QString("%1").arg(H, 2, 10, QChar('0'));
    QString mm = QString("%1").arg(tm.minute(), 2, 10, QChar('0'));
    float dot = 9.0f * u, scoreW = 11.0f * dot;
    drawDot57(p, hh, ox + bw * 0.5f - 40.0f * u - scoreW, oy + 34.0f * u, dot, white);
    drawDot57(p, mm, ox + bw * 0.5f + 40.0f * u,          oy + 34.0f * u, dot, white);

    p.drawRect(QRectF(ox + lx, oy + m_pongPL, pw, ph));
    p.drawRect(QRectF(ox + rx, oy + m_pongPR, pw, ph));
    p.drawRect(QRectF(ox + m_pongBallX - br, oy + m_pongBallY - br, 2 * br, 2 * br));
    p.setRenderHint(QPainter::Antialiasing, true);
}

// --- Binary: BCD dot columns (H H : M M : S S) ---

void ScreenSaverView::paintBinaryClock(QPainter &p)
{
    float u = qMin(height() / 400.0f, width() / 1280.0f);
    float bw = 900.0f * u, bh = 300.0f * u;
    QPointF tl = placeFloatingBlock(bw, bh);

    QTime tm = QTime::currentTime();
    int H = tm.hour(), M = tm.minute(), S = tm.second();
    int cols[6] = { H / 10, H % 10, M / 10, M % 10, S / 10, S % 10 };
    int maxV[6] = { 2, 9, 5, 9, 5, 9 };
    static const char *labels[6] = { "H", "H", "M", "M", "S", "S" };

    QColor accent = m_currentTheme.colors.secondHand.isValid()
                  ? m_currentTheme.colors.secondHand : QColor(51, 224, 255);

    float colW = bw / 6.0f, x0 = tl.x() + colW * 0.5f;
    float baseY = tl.y() + bh * 0.62f, pitch = 56.0f * u, dotR = 18.0f * u;

    p.setRenderHint(QPainter::Antialiasing, true);
    for (int i = 0; i < 6; i++) {
        float cx = x0 + i * colW;
        for (int e = 0; e < 4; e++) {
            int bv = 1 << e; if (bv > maxV[i]) break;
            bool on = (cols[i] >> e) & 1;
            float y = baseY - e * pitch;
            if (on) {
                QColor g = accent; g.setAlphaF(0.30f);
                p.setPen(Qt::NoPen); p.setBrush(g);
                p.drawEllipse(QPointF(cx, y), dotR + 6.0f * u, dotR + 6.0f * u);
                p.setBrush(accent);
                p.drawEllipse(QPointF(cx, y), dotR, dotR);
            } else {
                p.setPen(QPen(QColor(120, 160, 180, 70), 1.5f * u));
                p.setBrush(QColor(70, 110, 130, 40));
                p.drawEllipse(QPointF(cx, y), dotR, dotR);
            }
        }
        QFont lf("DejaVu Sans"); lf.setBold(true); lf.setPixelSize(static_cast<int>(19.0f * u));
        p.setFont(lf); p.setPen(QColor(150, 170, 190, 153));
        p.drawText(QRectF(cx - colW * 0.5f, baseY + 28.0f * u, colW, 26.0f * u),
                   Qt::AlignCenter, QString::fromLatin1(labels[i]));
        QFont vf("DejaVu Sans Mono"); vf.setPixelSize(static_cast<int>(14.0f * u));
        p.setFont(vf); p.setPen(QColor(110, 200, 230, 115));
        p.drawText(QRectF(cx - colW * 0.5f, baseY + 52.0f * u, colW, 20.0f * u),
                   Qt::AlignCenter, QString::number(cols[i]));
    }
}

// --- Fibonacci: colour-square clock ---

static void fibDecode(int h, int mm, int *a)   // values [5,3,2,1,1]; a[i] in {0 off,1 red,2 green,3 blue}
{
    static const int val[5] = { 5, 3, 2, 1, 1 };
    for (int mask = 0; mask < 1024; mask++) {
        int m = mask, hs = 0, ms = 0, tmp[5];
        for (int i = 0; i < 5; i++) {
            int c = m & 3; m >>= 2; tmp[i] = c;
            if (c == 1 || c == 3) hs += val[i];
            if (c == 2 || c == 3) ms += val[i];
        }
        if (hs == h && ms == mm) { for (int i = 0; i < 5; i++) a[i] = tmp[i]; return; }
    }
    for (int i = 0; i < 5; i++) a[i] = 0;
}

void ScreenSaverView::paintFibonacciClock(QPainter &p)
{
    float u = qMin(height() / 400.0f, width() / 1280.0f);

    QTime tm = QTime::currentTime();
    int H = tm.hour() % 12; if (H == 0) H = 12;
    int mm = (tm.minute() + 2) / 5; if (mm >= 12) mm = 0;
    int a[5]; fibDecode(H, mm, a);

    float unit = 64.0f * u;
    float bw = 8.0f * unit, bh = 5.0f * unit + 38.0f * u;
    QPointF tl = placeFloatingBlock(bw, bh);
    float ox = tl.x(), oy = tl.y();

    QColor COL[4] = { m_currentTheme.colors.ticks,       // off (dark)
                      m_currentTheme.colors.hourHand,     // red
                      m_currentTheme.colors.minuteHand,   // green
                      m_currentTheme.colors.secondHand }; // blue

    struct Sq { int s, c, r; };
    static const Sq sq[5] = { {5,0,0}, {3,5,0}, {2,5,3}, {1,7,3}, {1,7,4} };
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    for (int i = 0; i < 5; i++) {
        p.setBrush(COL[a[i]]);
        p.drawRoundedRect(QRectF(ox + sq[i].c * unit + 3.0f * u, oy + sq[i].r * unit + 3.0f * u,
                                 sq[i].s * unit - 6.0f * u, sq[i].s * unit - 6.0f * u),
                          6.0f * u, 6.0f * u);
    }

    QFont lf("DejaVu Sans"); lf.setPixelSize(static_cast<int>(13.0f * u));
    p.setFont(lf);
    struct Leg { QColor c; const char *t; };
    Leg leg[3] = { { COL[1], "hours" }, { COL[2], "minutes" }, { COL[3], "both" } };
    float legX = ox, legY = oy + 5.0f * unit + 10.0f * u;
    QFontMetricsF fm(lf);
    for (const Leg &L : leg) {
        p.setPen(Qt::NoPen); p.setBrush(L.c);
        p.drawRoundedRect(QRectF(legX, legY, 14.0f * u, 14.0f * u), 3, 3);
        p.setPen(QColor(170, 178, 190, 204));
        p.drawText(QPointF(legX + 20.0f * u, legY + 12.0f * u), QString::fromLatin1(L.t));
        legX += 20.0f * u + fm.horizontalAdvance(QString::fromLatin1(L.t)) + 22.0f * u;
    }
}

// --- Sundial: virtual sun/moon + gnomon shadow over a time-of-day sky ---

void ScreenSaverView::paintSundialClock(QPainter &p)
{
    float u = qMin(height() / 400.0f, width() / 1280.0f);
    float bw = width() * 0.96f, bh = height() * 0.94f;
    QPointF tl = placeFloatingBlock(bw, bh);
    float ox = tl.x(), oy = tl.y();

    QTime tm = QTime::currentTime();
    int H = tm.hour(), M = tm.minute();
    float hf = H + M / 60.0f;

    static const int KFtop[5][3] = { {8,12,32}, {40,58,110}, {58,123,213}, {42,40,92}, {8,12,32} };
    static const int KFbot[5][3] = { {20,24,52}, {232,140,86}, {180,222,255}, {232,120,72}, {20,24,52} };
    float seg = hf / 6.0f; int si = qMin(3, static_cast<int>(seg)); float ft = seg - si;
    auto mix = [&](const int a[5][3], int i, float t) {
        return QColor(static_cast<int>(a[i][0] + (a[i+1][0]-a[i][0]) * t),
                      static_cast<int>(a[i][1] + (a[i+1][1]-a[i][1]) * t),
                      static_cast<int>(a[i][2] + (a[i+1][2]-a[i][2]) * t));
    };
    QColor topC = mix(KFtop, si, ft), botC = mix(KFbot, si, ft);

    float horizon = oy + bh * 0.74f;
    p.setRenderHint(QPainter::Antialiasing, true);
    QLinearGradient sky(0, oy, 0, horizon);
    sky.setColorAt(0.0, topC); sky.setColorAt(1.0, botC);
    p.setPen(Qt::NoPen); p.setBrush(sky);
    p.drawRect(QRectF(ox, oy, bw, horizon - oy));
    p.setBrush(QColor(21, 17, 13));
    p.drawRect(QRectF(ox, horizon, bw, oy + bh - horizon));

    bool isDay = (hf >= 6.0f && hf < 18.0f);
    if (!isDay) {
        p.setBrush(QColor(255, 255, 255, 180));
        static const float sxf[10] = {0.10f,0.24f,0.40f,0.60f,0.76f,0.89f,0.16f,0.50f,0.69f,0.82f};
        static const float syf[10] = {0.15f,0.30f,0.10f,0.22f,0.18f,0.33f,0.40f,0.13f,0.38f,0.28f};
        for (int k = 0; k < 10; k++)
            p.drawRect(QRectF(ox + sxf[k]*bw, oy + syf[k]*bh*0.6f, 2.0f*u, 2.0f*u));
    }

    float cx = ox + bw * 0.5f;
    float pp = isDay ? (hf - 6.0f) / 12.0f : fmodf(hf + 6.0f, 24.0f) / 12.0f;
    float bx = ox + 0.10f*bw + pp * 0.80f*bw;
    float by = horizon - sinf(pp * static_cast<float>(M_PI)) * (bh * 0.55f);

    float elev = qMax(0.08f, sinf(pp * static_cast<float>(M_PI)));
    float dir = (bx < cx) ? 1.0f : -1.0f;
    float shLen = qMin(70.0f * u / elev * (fabsf(cx - bx) / (bw*0.5f) + 0.25f), bw * 0.42f);
    p.setPen(QPen(QColor(0, 0, 0, 128), 10.0f * u, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(QPointF(cx, horizon), QPointF(cx + dir * shLen, horizon - 4.0f * u));

    QFont tf("DejaVu Sans"); tf.setPixelSize(static_cast<int>(12.0f * u));
    p.setFont(tf);
    for (int hh = 6; hh <= 18; hh += 2) {
        float ppx = (hh - 6) / 12.0f, txx = ox + 0.10f*bw + ppx * 0.80f*bw;
        p.setPen(QPen(QColor(255, 255, 255, 46), 2.0f * u));
        p.drawLine(QPointF(txx, horizon), QPointF(txx, horizon + 14.0f * u));
        p.setPen(QColor(255, 255, 255, 100));
        int lbl = hh > 12 ? hh - 12 : hh;
        p.drawText(QRectF(txx - 14.0f*u, horizon + 16.0f*u, 28.0f*u, 18.0f*u),
                   Qt::AlignCenter, QString::number(lbl));
    }

    if (isDay) {
        QColor g = QColor(255, 210, 127); g.setAlphaF(0.5f);
        p.setPen(Qt::NoPen); p.setBrush(g);
        p.drawEllipse(QPointF(bx, by), 44.0f*u, 44.0f*u);
        p.setBrush(QColor(255, 227, 154));
        p.drawEllipse(QPointF(bx, by), 30.0f*u, 30.0f*u);
    } else {
        p.setPen(Qt::NoPen); p.setBrush(QColor(231, 236, 255));
        p.drawEllipse(QPointF(bx, by), 24.0f*u, 24.0f*u);
        p.setBrush(botC);
        p.drawEllipse(QPointF(bx - 9.0f*u, by - 5.0f*u), 22.0f*u, 22.0f*u);
    }

    p.setBrush(QColor(12, 10, 8)); p.setPen(Qt::NoPen);
    QPointF gp[4] = { {cx - 9.0f*u, horizon}, {cx - 3.0f*u, horizon - 78.0f*u},
                      {cx + 3.0f*u, horizon - 78.0f*u}, {cx + 9.0f*u, horizon} };
    p.drawConvexPolygon(gp, 4);

    QFont df("DejaVu Sans Mono"); df.setBold(true); df.setPixelSize(static_cast<int>(22.0f*u));
    p.setFont(df); p.setPen(QColor(255, 255, 255, 217));
    p.drawText(QPointF(ox + 24.0f*u, oy + 36.0f*u),
               QString("%1:%2").arg(H,2,10,QChar('0')).arg(M,2,10,QChar('0')));
}

// --- Flip-Dot: electromechanical dot-matrix board ---

void ScreenSaverView::paintFlipDotClock(QPainter &p)
{
    float u = qMin(height() / 400.0f, width() / 1280.0f);
    QTime tm = QTime::currentTime();
    int H = tm.hour() % 12; if (H == 0) H = 12;
    QString s = QString("%1:%2").arg(H,2,10,QChar('0')).arg(tm.minute(),2,10,QChar('0'));

    QVector<QVector<int>> grid(7);
    auto pushBlank = [&]() { for (int r = 0; r < 7; r++) grid[r].append(0); };
    for (const QChar &ch : s) {
        if (ch == QLatin1Char(':')) {
            pushBlank();
            static const int colon[7] = {0,0,1,0,1,0,0};
            for (int r = 0; r < 7; r++) grid[r].append(colon[r]);
            pushBlank();
            continue;
        }
        int d = ch.digitValue();
        for (int c = 0; c < 5; c++)
            for (int r = 0; r < 7; r++)
                grid[r].append(DOT57[d][r][c] == '1' ? 1 : 0);
        pushBlank();
    }
    int nCols = grid[0].size(), nRows = 7;

    float pitch = qMin((width() * 0.86f) / nCols, (height() * 0.60f) / nRows);
    float dotR = pitch * 0.40f;
    float gridW = nCols * pitch, gridH = nRows * pitch;
    float frame = 32.0f * u;

    QPointF tl = placeFloatingBlock(gridW + frame * 2, gridH + frame * 2);
    float ox = tl.x() + frame + pitch * 0.5f;
    float oy = tl.y() + frame + pitch * 0.5f;

    QColor lit = m_currentTheme.colors.secondHand.isValid()
               ? m_currentTheme.colors.secondHand : QColor(255, 210, 58);

    p.setRenderHint(QPainter::Antialiasing, true);
    p.setBrush(QColor(13, 13, 12));
    p.setPen(QPen(QColor(120, 110, 60, 64), 2.0f * u));
    p.drawRoundedRect(QRectF(tl.x() + frame * 0.4f, tl.y() + frame * 0.4f,
                             gridW + frame * 1.2f, gridH + frame * 1.2f), 14.0f * u, 14.0f * u);

    for (int r = 0; r < nRows; r++) {
        for (int c = 0; c < nCols; c++) {
            float x = ox + c * pitch, y = oy + r * pitch;
            if (grid[r][c]) {
                QColor g = lit; g.setAlphaF(0.35f);
                p.setPen(Qt::NoPen); p.setBrush(g);
                p.drawEllipse(QPointF(x, y), dotR + 3.0f * u, dotR + 3.0f * u);
                p.setBrush(lit);
                p.drawEllipse(QPointF(x, y), dotR, dotR);
                p.setBrush(QColor(255, 255, 255, 90));
                p.drawEllipse(QPointF(x - dotR*0.3f, y - dotR*0.3f), dotR*0.32f, dotR*0.32f);
            } else {
                p.setPen(QPen(QColor(0, 0, 0, 150), 1.0f));
                p.setBrush(QColor(26, 26, 24));
                p.drawEllipse(QPointF(x, y), dotR, dotR);
            }
        }
    }
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
