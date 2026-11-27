#ifndef MPVRENDERER_H_
#define MPVRENDERER_H_

#include <mpv/client.h>
#include <mpv/render_gl.h>

#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickFramebufferObject>
#include <QtCore/QLoggingCategory>
#include <memory>

#include "qthelper.hpp"

Q_DECLARE_LOGGING_CATEGORY(wekdeMpv)

namespace mpv
{

struct MpvHandle {
    MpvHandle(mpv_handle* mpv): handle(mpv) {}
    ~MpvHandle() {
        if (handle) mpv_terminate_destroy(handle);
    }
    mpv_handle* handle;
};

class MpvRender;

class MpvObject : public QQuickFramebufferObject {
    Q_OBJECT
    Q_PROPERTY(QUrl source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(Status status READ status NOTIFY statusChanged)
    Q_PROPERTY(bool mute READ mute WRITE setMute)
    Q_PROPERTY(int volume READ volume WRITE setVolume)

    friend class MpvRender;

public:

    explicit MpvObject(QQuickItem* parent = nullptr);
    virtual ~MpvObject();
    Renderer* createRenderer() const override;

    enum Status
    {
        Stopped,
        Playing,
        Paused,
    };
    Q_ENUM(Status)
    Status  status() const;
    QUrl    source() const;
    bool    mute() const;
    int     volume() const;

    void setSource(const QUrl& source);
    void setMute(const bool& mute);
    void setVolume(const int& volume);

public slots:
    void play();
    void pause();
    void stop();

    bool     command(const QVariant& params);
    bool     setProperty(const QString& name, const QVariant& value);
    QVariant getProperty(const QString& name, bool* ok = nullptr) const;
    void     initCallback();
    void     checkAndEmitFirstFrame();
    // empties mpv's event queue; nothing here acts on the events
    void     drainEvents();

signals:
    void statusChanged();
    void sourceChanged();
    void firstFrame();

private:
    bool   inited = false;
    QUrl   m_source;

private:
    mpv_handle*                m_mpv { nullptr };
    std::shared_ptr<MpvHandle> m_shared_mpv { nullptr };
    bool                       m_first_frame { true };
};
} // namespace mpv

#endif
