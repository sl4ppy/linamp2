#ifndef SCREENSAVERVIEW_H
#define SCREENSAVERVIEW_H

#include <QWidget>
#include <QTimer>
#include <QDateTime>
#include <QImage>
#include "clockthemes.h"

namespace Ui {
class ScreenSaverView;
}

class ScreenSaverView : public QWidget
{
    Q_OBJECT

public:
    enum ClockMode { Digital, Analog };

    explicit ScreenSaverView(QWidget *parent = nullptr);
    ~ScreenSaverView();

public slots:
    void start();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

signals:
    void userActivityDetected();

private slots:
    void animate();

private:
    Ui::ScreenSaverView *ui;
    QTimer *m_animTimer = nullptr;
    ClockMode m_clockMode = Digital;

    // Clock text (digital mode)
    QString m_timeStr;
    QString m_ampm;
    QString m_dateStr;
    void formatTime();

    // Painting helpers
    void paintDigitalClock(QPainter &painter);
    void paintAnalogClock(QPainter &painter);

    // Themed analog clock drawing methods
    void drawDialBackground(QPainter &p, float cx, float cy, float radius, const ClockTheme &theme);
    void drawDecorativeRings(QPainter &p, float cx, float cy, float radius, const ClockTheme &theme);
    void drawTicks(QPainter &p, float cx, float cy, float radius, const ClockTheme &theme);
    void drawSingleTick(QPainter &p, float cx, float cy, float radius,
                        float angle, TickShape shape, float size,
                        const QColor &color, const ClockTheme &theme);
    void drawNumerals(QPainter &p, float cx, float cy, float radius, const ClockTheme &theme);
    void drawHand(QPainter &p, float cx, float cy, float radius,
                  float angleDeg, HandShape shape, float lengthFrac, float widthFrac,
                  const QColor &color, bool outlineOnly, bool drawCounterweight = false,
                  float counterweightLen = 0.0f);
    void drawCenterPin(QPainter &p, float cx, float cy, float radius, const ClockTheme &theme);
    QColor applyHueCycle(const QColor &color, float hueShift) const;

    // Theme state
    ClockTheme m_currentTheme;
    int m_themeIndex = 0;

    // Pre-allocated glow buffers
    QImage m_glowBuffer;
    QImage m_glowTmp;
    int m_cachedGlowSize = 0;

    // Floating position (shared between digital and analog modes)
    float m_posX = -1;
    float m_posY = -1;
    float m_velX = 0.45f;
    float m_velY = 0.3f;

    // Color cycling & breathing
    float m_hue = 180.0f;
    float m_breathePhase = 0.0f;
};

#endif // SCREENSAVERVIEW_H
