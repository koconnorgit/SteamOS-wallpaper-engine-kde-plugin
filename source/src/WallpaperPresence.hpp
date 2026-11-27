#pragma once
#include <QObject>
#include <QString>
#include <QStringList>

namespace wekde {

// What each monitor's wallpaper is showing, so that a playlist on one monitor
// can avoid a wallpaper already up on another. Every wallpaper instance in the
// process owns one of these and keeps `wid` current.
class WallpaperPresence : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString wid READ wid WRITE setWid NOTIFY widChanged)

public:
    WallpaperPresence(QObject* parent = nullptr);
    ~WallpaperPresence() override;

    QString wid() const { return m_wid; }
    void    setWid(const QString& wid);

    // Wallpapers showing on the other monitors right now.
    Q_INVOKABLE QStringList others() const;

signals:
    void widChanged();

private:
    QString m_wid;
};

} // namespace wekde
