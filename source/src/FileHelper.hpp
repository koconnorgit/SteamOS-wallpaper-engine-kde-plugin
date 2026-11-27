#pragma once
#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariantMap>
#include <QVariantList>

#include <functional>

namespace wekde {

class FileHelper : public QObject {
    Q_OBJECT

public:
    explicit FileHelper(QObject* parent = nullptr);
    virtual ~FileHelper();

    // File operations
    Q_INVOKABLE QByteArray readFile(const QString& path);
    Q_INVOKABLE bool exists(const QString& path);
    Q_INVOKABLE qint64 getDirSize(const QString& path, int depth = 3);
    Q_INVOKABLE QVariantMap getFolderList(const QString& path, const QVariantMap& opt = {});

    // The same three on a worker thread, for the settings dialog, where a
    // library scan means hundreds of them. Each returns an id; the result
    // arrives with asyncFinished(id, result). Files are returned as text.
    Q_INVOKABLE int readFileAsync(const QString& path);
    Q_INVOKABLE int getDirSizeAsync(const QString& path, int depth = 3);
    Q_INVOKABLE int getFolderListAsync(const QString& path, const QVariantMap& opt = {});

    // Wallpaper config operations
    Q_INVOKABLE QVariantMap readWallpaperConfig(const QString& id);
    Q_INVOKABLE void writeWallpaperConfig(const QString& id, const QVariantMap& changed);
    Q_INVOKABLE void resetWallpaperConfig(const QString& id);

signals:
    void asyncFinished(int id, const QVariant& result);

private:
    int runAsync(std::function<QVariant()> job);
    int m_nextId { 0 };

    QString configDir() const;
    QString wallpaperConfigDir() const;
    QString wallpaperConfigFile(const QString& id) const;
    QVariantMap readWallpaperConfig(const QString& id, bool* ok);
};

} // namespace wekde
