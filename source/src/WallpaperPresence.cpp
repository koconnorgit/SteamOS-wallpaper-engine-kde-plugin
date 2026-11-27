#include "WallpaperPresence.hpp"

#include <QList>
#include <QVariantMap>

using namespace wekde;

namespace
{
QList<WallpaperPresence*>& registry() {
    static QList<WallpaperPresence*> list;
    return list;
}
} // namespace

WallpaperPresence::WallpaperPresence(QObject* parent): QObject(parent) { registry().append(this); }

WallpaperPresence::~WallpaperPresence() { registry().removeAll(this); }

void WallpaperPresence::setWid(const QString& wid) {
    if (wid == m_wid) return;
    m_wid = wid;
    Q_EMIT widChanged();
}

QStringList WallpaperPresence::others() const {
    QStringList out;
    for (const WallpaperPresence* p : registry()) {
        if (p != this && ! p->m_wid.isEmpty()) out.append(p->m_wid);
    }
    return out;
}

void WallpaperPresence::setUntil(double until) {
    if (until == m_until) return;
    m_until = until;
    Q_EMIT untilChanged();
}

QVariantList WallpaperPresence::othersDetailed() const {
    QVariantList out;
    for (const WallpaperPresence* p : registry()) {
        if (p == this || p->m_wid.isEmpty()) continue;
        out.append(QVariantMap { { "wid", p->m_wid }, { "until", p->m_until } });
    }
    return out;
}
