#pragma once
#include <QObject>
#include <QString>

namespace wekde {

// Playlists shared by every monitor.
//
// Wallpaper settings are stored per containment (per monitor), so anything
// meant to be common to all of them needs a home of its own: one JSON file in
// the user's config directory. Every PlaylistStore reflects that file, and a
// save through any of them (or a change made by another process) is announced
// by all of them.
class PlaylistStore : public QObject {
    Q_OBJECT
    // File contents; empty when nothing has been saved yet.
    Q_PROPERTY(QString text READ text NOTIFY textChanged)
    Q_PROPERTY(QString path READ path CONSTANT)

public:
    PlaylistStore(QObject* parent = nullptr);

    QString text() const;
    QString path() const;

    // Replaces the file atomically. False when it could not be written.
    Q_INVOKABLE bool save(const QString& text);

signals:
    void textChanged();
};

} // namespace wekde
