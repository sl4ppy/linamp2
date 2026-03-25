#include "viewtransition.h"
#include <QPixmap>

ViewTransition::ViewTransition(QWidget *container, QStackedLayout *stack, QObject *parent)
    : QObject(parent)
    , m_container(container)
    , m_stack(stack)
{
}

void ViewTransition::fadeTo(int targetIndex, int durationMs)
{
    if (m_transitioning) {
        // Already animating — just snap to the new target
        if (m_animation) m_animation->stop();
        onFadeFinished();
        m_stack->setCurrentIndex(targetIndex);
        return;
    }

    // Grab a screenshot of the current view
    QPixmap snapshot = m_container->grab();

    // Swap the stack instantly (hidden behind the overlay)
    m_stack->setCurrentIndex(targetIndex);

    // Create overlay label showing the snapshot
    m_overlay = new QLabel(m_container);
    m_overlay->setPixmap(snapshot);
    m_overlay->setGeometry(m_container->rect());
    m_overlay->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_overlay->show();
    m_overlay->raise();

    // Apply opacity effect
    m_effect = new QGraphicsOpacityEffect(m_overlay);
    m_effect->setOpacity(1.0);
    m_overlay->setGraphicsEffect(m_effect);

    // Animate opacity 1.0 → 0.0
    m_animation = new QPropertyAnimation(m_effect, "opacity", this);
    m_animation->setDuration(durationMs);
    m_animation->setStartValue(1.0);
    m_animation->setEndValue(0.0);
    m_animation->setEasingCurve(QEasingCurve::InOutQuad);

    connect(m_animation, &QPropertyAnimation::finished, this, &ViewTransition::onFadeFinished);

    m_transitioning = true;
    m_animation->start();
}

void ViewTransition::onFadeFinished()
{
    m_transitioning = false;

    if (m_overlay) {
        m_overlay->hide();
        m_overlay->deleteLater();
        m_overlay = nullptr;
    }

    m_effect = nullptr; // Owned by m_overlay, deleted with it

    if (m_animation) {
        m_animation->deleteLater();
        m_animation = nullptr;
    }
}
