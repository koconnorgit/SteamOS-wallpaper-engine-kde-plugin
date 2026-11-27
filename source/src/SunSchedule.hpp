#pragma once
#include <QObject>
#include <QVariantList>

class KDarkLightScheduleProvider;

namespace wekde {

// Sunrise and sunset times from KDE's day/night cycle (KNightTime), the same
// source Night Light and automatic theme switching use.
//
// KNightTime reports each transition as a window: morning runs from dawn to
// sunrise, evening from sunset to dusk. Sunrise is therefore the end of the
// morning window and sunset the start of the evening one.
class SunSchedule : public QObject {
    Q_OBJECT
    // False when the plugin was built without KNightTime (Plasma < 6.5).
    Q_PROPERTY(bool available READ isAvailable CONSTANT)
    // KNightTime starts with a placeholder schedule and fetches the real one
    // asynchronously; ready turns true once the real one has arrived.
    Q_PROPERTY(bool ready READ isReady NOTIFY readyChanged)
    // Epoch milliseconds, ascending, covering yesterday and the coming days.
    Q_PROPERTY(QVariantList sunrises READ sunrises NOTIFY scheduleChanged)
    Q_PROPERTY(QVariantList sunsets READ sunsets NOTIFY scheduleChanged)

public:
    SunSchedule(QObject* parent = nullptr);

    bool         isAvailable() const;
    bool         isReady() const { return m_ready; }
    QVariantList sunrises() const { return m_sunrises; }
    QVariantList sunsets() const { return m_sunsets; }

signals:
    void readyChanged();
    void scheduleChanged();

private:
    void refresh();
    void setReady();

    KDarkLightScheduleProvider* m_provider { nullptr };
    bool                        m_ready { false };
    QVariantList                m_sunrises;
    QVariantList                m_sunsets;
};

} // namespace wekde
