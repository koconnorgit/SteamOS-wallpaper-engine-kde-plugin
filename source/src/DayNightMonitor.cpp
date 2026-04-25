#include "DayNightMonitor.hpp"
#include <QDBusInterface>
#include <QDBusReply>
#include <QDebug>

using namespace wekde;

namespace {
constexpr const char *kService   = "org.kde.KWin";
constexpr const char *kPath      = "/org/kde/KWin/NightLight";
constexpr const char *kInterface = "org.kde.KWin.NightLight";
}

DayNightMonitor::DayNightMonitor(QQuickItem *parent)
    : QQuickItem(parent) {
    QDBusConnection sessionBus = QDBusConnection::sessionBus();
    if (!sessionBus.isConnected()) {
        qWarning() << "DayNightMonitor: cannot connect to D-Bus session bus";
        return;
    }

    bool connected = sessionBus.connect(
        kService,
        kPath,
        "org.freedesktop.DBus.Properties",
        "PropertiesChanged",
        this,
        SLOT(handlePropertiesChanged(QString, QVariantMap, QStringList))
    );

    if (!connected) {
        qWarning() << "DayNightMonitor: failed to subscribe to PropertiesChanged on" << kPath;
    }

    refreshAll();
}

QVariant DayNightMonitor::readProperty(const QString &name) {
    QDBusInterface iface(kService, kPath, "org.freedesktop.DBus.Properties",
                          QDBusConnection::sessionBus());
    if (!iface.isValid()) return {};
    QDBusReply<QDBusVariant> reply = iface.call("Get", QString(kInterface), name);
    if (!reply.isValid()) return {};
    return reply.value().variant();
}

void DayNightMonitor::refreshAll() {
    for (const QString &name : { QStringLiteral("available"),
                                  QStringLiteral("enabled"),
                                  QStringLiteral("running"),
                                  QStringLiteral("daylight") }) {
        QVariant v = readProperty(name);
        if (v.isValid()) applyProperty(name, v);
    }
}

void DayNightMonitor::applyProperty(const QString &name, const QVariant &value) {
    if (name == QLatin1String("available")) {
        bool v = value.toBool();
        if (m_available != v) { m_available = v; emit availableChanged(v); }
    } else if (name == QLatin1String("enabled")) {
        bool v = value.toBool();
        if (m_enabled != v) { m_enabled = v; emit enabledChanged(v); }
    } else if (name == QLatin1String("running")) {
        bool v = value.toBool();
        if (m_running != v) { m_running = v; emit runningChanged(v); }
    } else if (name == QLatin1String("daylight")) {
        bool v = value.toBool();
        if (m_daylight != v) {
            m_daylight = v;
            emit daylightChanged(v);
            emit phaseChanged(v);
        }
    }
}

void DayNightMonitor::handlePropertiesChanged(const QString &interface,
                                               const QVariantMap &changed,
                                               const QStringList &invalidated) {
    if (interface != QLatin1String(kInterface)) return;

    for (auto it = changed.constBegin(); it != changed.constEnd(); ++it) {
        applyProperty(it.key(), it.value());
    }
    for (const QString &name : invalidated) {
        QVariant v = readProperty(name);
        if (v.isValid()) applyProperty(name, v);
    }
}
