#include "SunSchedule.hpp"
#include <QTimer>

#ifdef WEKDE_HAVE_KNIGHTTIME
#    include <KDarkLightScheduleProvider>
#endif

using namespace wekde;

namespace
{
// How long to wait for the real schedule before settling for the placeholder.
constexpr int kReadyTimeoutMs = 5000;
} // namespace

SunSchedule::SunSchedule(QQuickItem* parent): QQuickItem(parent) {
#ifdef WEKDE_HAVE_KNIGHTTIME
    m_provider = new KDarkLightScheduleProvider(QString(), this);
    connect(m_provider, &KDarkLightScheduleProvider::scheduleChanged, this, [this] {
        refresh();
        setReady();
    });
    refresh();
    QTimer::singleShot(kReadyTimeoutMs, this, &SunSchedule::setReady);
#else
    m_ready = true;
#endif
}

bool SunSchedule::isAvailable() const {
#ifdef WEKDE_HAVE_KNIGHTTIME
    return true;
#else
    return false;
#endif
}

void SunSchedule::setReady() {
    if (m_ready) return;
    m_ready = true;
    emit readyChanged();
}

void SunSchedule::refresh() {
#ifdef WEKDE_HAVE_KNIGHTTIME
    QVariantList sunrises, sunsets;
    const auto   cycles = m_provider->schedule().cycles();
    for (const auto& cycle : cycles) {
        const QDateTime sunrise = cycle.morning().endDateTime();
        const QDateTime sunset  = cycle.evening().startDateTime();
        if (sunrise.isValid()) sunrises.append(double(sunrise.toMSecsSinceEpoch()));
        if (sunset.isValid()) sunsets.append(double(sunset.toMSecsSinceEpoch()));
    }
    if (sunrises == m_sunrises && sunsets == m_sunsets) return;
    m_sunrises = sunrises;
    m_sunsets  = sunsets;
    emit scheduleChanged();
#endif
}
