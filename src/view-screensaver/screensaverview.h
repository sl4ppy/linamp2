#ifndef SCREENSAVERVIEW_H
#define SCREENSAVERVIEW_H

#include <QWidget>
#include <QTimer>
#include <QDateTime>
#include <QImage>
#include <QStringList>
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

    // Face enumeration for the HTTP API
    static QStringList faceNames();
    static int faceIndexForName(const QString &name); // -1 if not found

public slots:
    void start();
    void start(int themeIndex); // start with a specific theme (-1 = random)

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
    void paintOrbitalClock(QPainter &painter);
    void paintWanderingClock(QPainter &painter);
    void paintRegulatorClock(QPainter &painter);
    void paintWordClock(QPainter &painter);
    void paintBerlinUhr(QPainter &painter);
    void paintPongClock(QPainter &painter);
    void paintBinaryClock(QPainter &painter);
    void paintFibonacciClock(QPainter &painter);
    void paintSundialClock(QPainter &painter);
    void paintFlipDotClock(QPainter &painter);

    // Digital style renderers (dispatched from paintDigitalClock)
    void paintDigitalNeon(QPainter &painter);
    void paintDigitalSevenSeg(QPainter &painter);
    void paintDigitalSplitFlap(QPainter &painter);
    void paintDigitalNixie(QPainter &painter);
    void paintDigitalTerminal(QPainter &painter);
    void paintDigitalVFD(QPainter &painter);

    // Position a content block of the given size, bouncing off edges.
    // Updates m_posX/m_posY/velocity and returns the block's top-left corner.
    QPointF placeFloatingBlock(float totalW, float totalH);

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
    bool  m_posInit = false;   // false until the block is first centered (per activation)
    float m_posX = -1;
    float m_posY = -1;
    float m_velX = 0.45f;
    float m_velY = 0.3f;

    // Color cycling & breathing
    float m_hue = 180.0f;
    float m_breathePhase = 0.0f;

    // Pong face state (persists across frames; reset in start())
    bool  m_pongInit = false;
    float m_pongBallX = 0, m_pongBallY = 0;
    float m_pongVX = 0, m_pongVY = 0;
    float m_pongPL = 0, m_pongPR = 0;
};

#endif // SCREENSAVERVIEW_H
