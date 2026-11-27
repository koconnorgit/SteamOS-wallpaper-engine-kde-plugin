#include "FileHelper.hpp"
#include <QFile>
#include <functional>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QCoreApplication>
#include <QPointer>
#include <QSaveFile>
#include <QThreadPool>
#include <QStandardPaths>
#include <QDateTime>

namespace wekde {

FileHelper::FileHelper(QObject* parent) : QObject(parent) {
    // Ensure config directory exists
    QDir dir(wallpaperConfigDir());
    if (!dir.exists()) {
        dir.mkpath(".");
    }
}

FileHelper::~FileHelper() {}

QString FileHelper::configDir() const {
    QString xdgConfig = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    return xdgConfig + "/wekde";
}

QString FileHelper::wallpaperConfigDir() const {
    return configDir() + "/wallpaper";
}

// The id becomes a file name. Anything that could leave the config directory
// gives an empty path, which the callers treat as "no config".
QString FileHelper::wallpaperConfigFile(const QString& id) const {
    if (id.isEmpty() || id == "." || id == ".." || id.contains('/') || id.contains('\\') ||
        id.contains(QChar(0))) {
        qWarning() << "FileHelper: not a usable wallpaper id:" << id;
        return QString();
    }
    return wallpaperConfigDir() + "/" + id + ".json";
}

QByteArray FileHelper::readFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "FileHelper: Cannot open file:" << path;
        return QByteArray();
    }
    return file.readAll();
}

bool FileHelper::exists(const QString& path) { return QFileInfo::exists(path); }

// Runs the job on the global thread pool and reports back on this object's
// thread. The job must not touch `this`: the helper may be gone by the time
// it runs (Plasma creates the settings page twice and drops the first).
int FileHelper::runAsync(std::function<QVariant()> job) {
    const int            id = ++m_nextId;
    QPointer<FileHelper> self(this);
    QThreadPool::globalInstance()->start([self, id, job = std::move(job)]() {
        QVariant result = job();
        QMetaObject::invokeMethod(
            qApp,
            [self, id, result]() {
                if (self) Q_EMIT self->asyncFinished(id, result);
            },
            Qt::QueuedConnection);
    });
    return id;
}

int FileHelper::readFileAsync(const QString& path) {
    return runAsync([path]() {
        return QVariant(QString::fromUtf8(FileHelper().readFile(path)));
    });
}

int FileHelper::getDirSizeAsync(const QString& path, int depth) {
    return runAsync([path, depth]() {
        return QVariant(FileHelper().getDirSize(path, depth));
    });
}

int FileHelper::getFolderListAsync(const QString& path, const QVariantMap& opt) {
    return runAsync([path, opt]() {
        return QVariant(FileHelper().getFolderList(path, opt));
    });
}

qint64 FileHelper::getDirSize(const QString& path, int depth) {
    qint64 totalSize = 0;
    QDir dir(path);

    if (!dir.exists()) {
        return 0;
    }

    if (depth <= 0) {
        // Recursive with no depth limit
        QDirIterator it(path, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            it.next();
            totalSize += it.fileInfo().size();
        }
    } else {
        // Limited depth
        std::function<qint64(const QString&, int)> calcSize = [&](const QString& dirPath, int currentDepth) -> qint64 {
            if (currentDepth > depth) return 0;

            qint64 size = 0;
            QDir d(dirPath);

            for (const QFileInfo& info : d.entryInfoList(QDir::Files | QDir::NoDotAndDotDot)) {
                size += info.size();
            }

            if (currentDepth < depth) {
                for (const QFileInfo& info : d.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
                    size += calcSize(info.absoluteFilePath(), currentDepth + 1);
                }
            }

            return size;
        };

        totalSize = calcSize(path, 1);
    }

    return totalSize;
}

QVariantMap FileHelper::getFolderList(const QString& path, const QVariantMap& opt) {
    QVariantMap result;

    bool onlyDir = opt.value("only_dir", true).toBool();
    QStringList fallbacks = opt.value("fallbacks", QStringList()).toStringList();

    // Find first existing directory
    QString folder = path;
    QDir dir(folder);

    if (!dir.exists()) {
        for (const QString& fb : fallbacks) {
            QDir fbDir(fb);
            if (fbDir.exists()) {
                folder = fb;
                dir = fbDir;
                break;
            }
        }
    }

    if (!dir.exists()) {
        return QVariantMap(); // Return null/empty
    }

    result["folder"] = folder;

    QVariantList items;
    QDir::Filters filters = onlyDir ? QDir::Dirs : (QDir::Dirs | QDir::Files);
    filters |= QDir::NoDotAndDotDot;

    for (const QFileInfo& info : dir.entryInfoList(filters)) {
        QVariantMap item;
        item["name"] = info.fileName();
        item["mtime"] = static_cast<qint64>(info.lastModified().toSecsSinceEpoch());
        items.append(item);
    }

    result["items"] = items;
    return result;
}

QVariantMap FileHelper::readWallpaperConfig(const QString& id) {
    bool ok = false;
    return readWallpaperConfig(id, &ok);
}

// *ok is false when there is a file but it can't be read or isn't a json object
QVariantMap FileHelper::readWallpaperConfig(const QString& id, bool* ok) {
    *ok              = true;
    QString filePath = wallpaperConfigFile(id);
    if (filePath.isEmpty()) return QVariantMap();
    QFile file(filePath);

    if (!file.exists()) {
        return QVariantMap();
    }

    *ok = false;
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "FileHelper: Cannot read config:" << filePath;
        return QVariantMap();
    }

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (doc.isNull() || !doc.isObject()) {
        qWarning() << "FileHelper: Config is not valid json:" << filePath;
        return QVariantMap();
    }

    *ok = true;
    return doc.object().toVariantMap();
}

void FileHelper::writeWallpaperConfig(const QString& id, const QVariantMap& changed) {
    QString filePath = wallpaperConfigFile(id);
    if (filePath.isEmpty()) return;

    // Read existing config. A file that is there but unreadable is left alone:
    // writing now would replace everything in it with just these changes.
    bool        readable = false;
    QVariantMap config   = readWallpaperConfig(id, &readable);
    if (!readable) {
        qWarning() << "FileHelper: Not overwriting unreadable config:" << filePath;
        return;
    }

    // Merge changes
    for (auto it = changed.constBegin(); it != changed.constEnd(); ++it) {
        config[it.key()] = it.value();
    }

    // Write back, through a temporary file so a crash or a full disk can't
    // leave a truncated config behind
    QSaveFile file(filePath);

    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "FileHelper: Cannot write config:" << filePath;
        return;
    }

    QJsonDocument doc(QJsonObject::fromVariantMap(config));
    const QByteArray data = doc.toJson();
    if (file.write(data) != data.size() || !file.commit()) {
        qWarning() << "FileHelper: Cannot write config:" << filePath;
    }
}

void FileHelper::resetWallpaperConfig(const QString& id) {
    QString filePath = wallpaperConfigFile(id);
    if (!filePath.isEmpty()) QFile::remove(filePath);
}

} // namespace wekde
