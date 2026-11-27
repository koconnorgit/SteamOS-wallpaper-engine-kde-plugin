#include "PlaylistStore.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QJsonDocument>
#include <QSaveFile>
#include <QTimer>
#include <QDebug>
#include <QStandardPaths>

using namespace wekde;

namespace
{

// One per process, shared by all PlaylistStore objects.
class Backend : public QObject {
    Q_OBJECT
public:
    static Backend* instance() {
        static Backend* backend = new Backend();
        return backend;
    }

    const QString& text() const { return m_text; }
    const QString& path() const { return m_path; }

    bool save(const QString& text) {
        QDir().mkpath(QFileInfo(m_path).absolutePath());
        QSaveFile file(m_path);
        if (! file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
        file.write(text.toUtf8());
        if (! file.commit()) return false;
        setText(text);
        watch();
        return true;
    }

signals:
    void textChanged();

private:
    Backend() {
        m_path = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) +
                 QStringLiteral("/wallpaper-engine-kde-playlists.json");
        // An editor saving in place is seen as several changes, the first of
        // them possibly mid-write. Wait for it to go quiet.
        m_reload.setSingleShot(true);
        m_reload.setInterval(200);
        connect(&m_reload, &QTimer::timeout, this, &Backend::reload);
        connect(&m_watcher, &QFileSystemWatcher::fileChanged, &m_reload, qOverload<>(&QTimer::start));
        connect(&m_watcher,
                &QFileSystemWatcher::directoryChanged,
                &m_reload,
                qOverload<>(&QTimer::start));
        reload();
    }

    void reload() {
        QFile file(m_path);
        if (! file.exists()) {
            // deleted: there are no playlists any more
            setText(QString());
        } else if (file.open(QIODevice::ReadOnly)) {
            const QByteArray data = file.readAll();
            // A file that doesn't parse (half written, or a typo from editing
            // by hand) is not taken for "no playlists": every monitor would
            // drop what it is showing. What was read last stays in effect.
            if (data.trimmed().isEmpty() || ! QJsonDocument::fromJson(data).isNull())
                setText(QString::fromUtf8(data));
            else
                qWarning() << "PlaylistStore: ignoring unparsable" << m_path;
        }
        watch();
    }

    // The file is replaced on every save, which drops it from the watcher, so
    // it is added back each time. Until the file exists its directory is
    // watched instead.
    void watch() {
        const bool    exists = QFile::exists(m_path);
        const QString dir    = QFileInfo(m_path).absolutePath();
        if (exists) {
            if (! m_watcher.files().contains(m_path)) m_watcher.addPath(m_path);
            if (m_watcher.directories().contains(dir)) m_watcher.removePath(dir);
        } else if (! m_watcher.directories().contains(dir)) {
            m_watcher.addPath(dir);
        }
    }

    void setText(const QString& text) {
        if (text == m_text) return;
        m_text = text;
        Q_EMIT textChanged();
    }

    QString            m_path;
    QString            m_text;
    QFileSystemWatcher m_watcher;
    QTimer             m_reload;
};

} // namespace

PlaylistStore::PlaylistStore(QObject* parent): QObject(parent) {
    connect(Backend::instance(), &Backend::textChanged, this, &PlaylistStore::textChanged);
}

QString PlaylistStore::text() const { return Backend::instance()->text(); }
QString PlaylistStore::path() const { return Backend::instance()->path(); }

bool PlaylistStore::save(const QString& text) { return Backend::instance()->save(text); }

#include "PlaylistStore.moc"
