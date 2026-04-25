#pragma once
#include <QQuickItem>
#include <QDBusConnection>
#include <QDBusVariant>

namespace wekde {

class DayNightMonitor : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(bool available READ isAvailable NOTIFY availableChanged)
    Q_PROPERTY(bool enabled READ isEnabled NOTIFY enabledChanged)
    Q_PROPERTY(bool running READ isRunning NOTIFY runningChanged)
    Q_PROPERTY(bool daylight READ isDaylight NOTIFY daylightChanged)

public:
    DayNightMonitor(QQuickItem *parent = nullptr);

    bool isAvailable() const { return m_available; }
    bool isEnabled() const { return m_enabled; }
    bool isRunning() const { return m_running; }
    bool isDaylight() const { return m_daylight; }

signals:
    void availableChanged(bool available);
    void enabledChanged(bool enabled);
    void runningChanged(bool running);
    void daylightChanged(bool daylight);
    void phaseChanged(bool isDay);

public slots:
    void handlePropertiesChanged(const QString &interface,
                                  const QVariantMap &changed,
                                  const QStringList &invalidated);

private:
    void refreshAll();
    QVariant readProperty(const QString &name);
    void applyProperty(const QString &name, const QVariant &value);

    bool m_available { false };
    bool m_enabled { false };
    bool m_running { false };
    bool m_daylight { true };
};

}
