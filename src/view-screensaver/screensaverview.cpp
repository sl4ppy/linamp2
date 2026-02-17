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
