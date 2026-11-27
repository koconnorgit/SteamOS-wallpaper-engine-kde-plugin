#include "TTYSwitchMonitor.hpp"
#include <QDebug>

using namespace wekde;

TTYSwitchMonitor::TTYSwitchMonitor(QObject *parent)
    : QObject(parent), m_sleeping(false) {
    QDBusConnection systemBus = QDBusConnection::systemBus();
    if (!systemBus.isConnected()) {
        // not worth taking the shell down for, the wallpaper just won't know about sleep
        qWarning("TTYSwitchMonitor: cannot connect to the D-Bus system bus");
        return;
    }

    bool connected = systemBus.connect(
        "org.freedesktop.login1",
        "/org/freedesktop/login1",
        "org.freedesktop.login1.Manager",
        "PrepareForSleep",
        this,
        SLOT(handlePrepareForSleep(bool))
    );

    if (!connected) {
        qWarning("TTYSwitchMonitor: failed to connect to the PrepareForSleep signal");
    }
}

void TTYSwitchMonitor::handlePrepareForSleep(bool sleep) {
    if (m_sleeping != sleep) {
        m_sleeping = sleep;
        emit ttySwitch(sleep);
    }
}
