#ifndef VIEWTRANSITION_H
#define VIEWTRANSITION_H

#include <QWidget>
#include <QLabel>
#include <QStackedLayout>
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>

class ViewTransition : public QObject
{
    Q_OBJECT
public:
    explicit ViewTransition(QWidget *container, QStackedLayout *stack, QObject *parent = nullptr);

    // Crossfade to targetIndex over durationMs. Falls back to instant swap if
    // a transition is already in progress.
    void fadeTo(int targetIndex, int durationMs = 2000);

private slots:
    void onFadeFinished();

private:
    QWidget *m_container;
    QStackedLayout *m_stack;
    QLabel *m_overlay = nullptr;
    QGraphicsOpacityEffect *m_effect = nullptr;
    QPropertyAnimation *m_animation = nullptr;
    bool m_transitioning = false;
};

#endif // VIEWTRANSITION_H
