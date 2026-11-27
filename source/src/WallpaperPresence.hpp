#pragma once
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>

namespace wekde {

// What each monitor's wallpaper is showing, so that a playlist on one monitor
// can avoid a wallpaper already up on another. Every wallpaper instance in the
// process owns one of these and keeps `wid` current.
class WallpaperPresence : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString wid READ wid WRITE setWid NOTIFY widChanged)
    // Until when (ms since the epoch) this monitor expects to keep showing
    // `wid`; 0 when that is not known.
    Q_PROPERTY(double until READ until WRITE setUntil NOTIFY untilChanged)

public:
    WallpaperPresence(QObject* parent = nullptr);
    ~WallpaperPresence() override;

    QString wid() const { return m_wid; }
    void    setWid(const QString& wid);
    double  until() const { return m_until; }
    void    setUntil(double until);

    // Wallpapers showing on the other monitors right now.
    Q_INVOKABLE QStringList others() const;
    // The same with each one's `until`: a list of { wid, until }.
    Q_INVOKABLE QVariantList othersDetailed() const;

signals:
    void widChanged();
    void untilChanged();

private:
    QString m_wid;
    double  m_until { 0 };
};

} // namespace wekde
