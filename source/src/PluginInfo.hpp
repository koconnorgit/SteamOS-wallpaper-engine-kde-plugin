#pragma once
#include <QObject>
#include <QString>
#include <QUrl>

namespace wekde
{

class PluginInfo : public QObject {
    Q_OBJECT
    Q_PROPERTY(QUrl cache_path READ cache_path CONSTANT)
    Q_PROPERTY(QString version READ version CONSTANT)

public:
    PluginInfo(QObject* parent = nullptr);
    virtual ~PluginInfo();

    QUrl cache_path() const;

    QString version() const { return "0.6.0"; };

};
} // namespace wekde
