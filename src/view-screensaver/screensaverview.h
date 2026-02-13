#ifndef SCREENSAVERVIEW_H
#define SCREENSAVERVIEW_H

#include <QWidget>
#include <QTimer>
#include <QDateTime>
#include <QImage>
#include <QLineF>

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

    // Clock text
    QString m_timeStr;
    QString m_ampm;
    QString m_dateStr;
    void formatTime();

    // Painting helpers
    void paintDigitalClock(QPainter &painter);
    void paintAnalogClock(QPainter &painter);
    void precomputeAnalogGeometry();

    // Precomputed tick geometry (batched by weight class)
    QLineF m_majorTicks[4];   // 12, 3, 6, 9
    QLineF m_minorTicks[8];   // other hour marks
    QLineF m_minuteTicks[48]; // minute marks

    // Pre-allocated glow buffers (eliminates per-frame heap allocation)
    QImage m_glowBuffer;
    QImage m_glowTmp;
    int m_cachedW = 0;
    int m_cachedH = 0;

    // Floating position
    float m_posX = -1;
    float m_posY = -1;
    float m_velX = 0.45f;
    float m_velY = 0.3f;

    // Color cycling & breathing
    float m_hue = 180.0f;
    float m_breathePhase = 0.0f;
};

#endif // SCREENSAVERVIEW_H
