#pragma once
#include <QObject>
#include <QDBusConnection>

namespace wekde {

// Tells when the machine is about to sleep and when it has woken up, from
// logind's PrepareForSleep. (The name is from an earlier version that watched
// virtual terminal switches.)
class TTYSwitchMonitor : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool sleeping READ isSleeping NOTIFY ttySwitch)

public:
    TTYSwitchMonitor(QObject *parent = nullptr);

    bool isSleeping() const { return m_sleeping; }

signals:
    void ttySwitch(bool sleep);

public slots:
    void handlePrepareForSleep(bool sleep);

private:
    bool m_sleeping;
};

}