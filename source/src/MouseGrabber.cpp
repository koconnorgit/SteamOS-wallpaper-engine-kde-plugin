#include "MouseGrabber.hpp"
#include <iostream>
#include <QLoggingCategory>
#include <QCoreApplication>
#include <QHoverEvent>
#include <QMouseEvent>
#include <QtGlobal>

using namespace wekde;

MouseGrabber::MouseGrabber(QQuickItem* parent): QQuickItem(parent) {
    setAcceptedMouseButtons(Qt::LeftButton);
    setAcceptHoverEvents(true);
}

QQuickItem* MouseGrabber::target() const { return m_target; }

void MouseGrabber::setTarget(QQuickItem* item) {
    if (item == m_target) return;
    m_target = item;
    if (m_target) m_target->setAcceptedMouseButtons(Qt::LeftButton);
    Q_EMIT targetChanged();
}

void MouseGrabber::sendMouseEvent(QMouseEvent* event) {
    if (m_target) {
        QMouseEvent temp(event->type(),
                         mapToItem(m_target, event->position()),
                         event->globalPosition(),
                         event->button(),
                         event->buttons(),
                         event->modifiers());
        QCoreApplication::sendEvent(m_target, &temp);
    }
}

void MouseGrabber::sendHoverEvent(QHoverEvent* event) {
    if (m_target) {
        auto newPos = mapToItem(m_target, event->position());
        QHoverEvent temp(event->type(),
                         newPos,
                         event->globalPosition(),
                         mapToItem(m_target, event->oldPosF()),
                         event->modifiers());
        QCoreApplication::sendEvent(m_target, &temp);
    }
}

void MouseGrabber::mousePressEvent(QMouseEvent* event) {
    sendMouseEvent(event);
    // need accept press to receive release
    // this break long press on desktop
    event->accept();
}

void MouseGrabber::mouseMoveEvent(QMouseEvent* event) {
    sendMouseEvent(event);
    event->ignore();
}

void MouseGrabber::mouseReleaseEvent(QMouseEvent* event) {
    sendMouseEvent(event);
    event->ignore();
}

void MouseGrabber::mouseDoubleClickEvent(QMouseEvent* event) {
    sendMouseEvent(event);
    event->ignore();
}

void MouseGrabber::hoverMoveEvent(QHoverEvent* event) {
    sendHoverEvent(event);
    event->ignore();
}
