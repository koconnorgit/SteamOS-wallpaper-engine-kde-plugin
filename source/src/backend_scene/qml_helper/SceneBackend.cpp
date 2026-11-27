#include "SceneBackend.hpp"

#include <QtGlobal>
#include <QtCore/QObject>
#include <QtCore/QDir>
#include <QtCore/QThread>

#include <QtGui/QGuiApplication>
#include <QtGui/QOpenGLContext>
#include <QtQuick/QQuickWindow>

#include <QtGui/QOffscreenSurface>
#include <QtQuick/QSGSimpleTextureNode>
#include <QSGTexture>

#include <algorithm>
#include <clocale>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <atomic>
#include <array>
#include <functional>

#include "glExtra.hpp"
#include "SceneWallpaper.hpp"
#include "SceneWallpaperSurface.hpp"
#include "Type.hpp"
#include "Utils/Platform.hpp"
#include <cstdio>
#include <qobjectdefs.h>
#include <unistd.h>

using namespace scenebackend;

Q_LOGGING_CATEGORY(wekdeScene, "wekde.scene")

#define _Q_INFO(fmt, ...) qCInfo(wekdeScene, fmt, __VA_ARGS__)

namespace
{
void* get_proc_address(const char* name) {
    QOpenGLContext* glctx = QOpenGLContext::currentContext();
    if (! glctx) return nullptr;

    return reinterpret_cast<void*>(glctx->getProcAddress(QByteArray(name)));
}

QSGTexture* createTextureFromGl(uint32_t handle, QSize size, QQuickWindow* window) {
    return QNativeInterface::QSGOpenGLTexture::fromNative(handle, window, size);
}

wallpaper::FillMode ToWPFillMode(int fillMode) {
    switch ((SceneObject::FillMode)fillMode) {
    case SceneObject::FillMode::STRETCH: return wallpaper::FillMode::STRETCH;
    case SceneObject::FillMode::ASPECTFIT: return wallpaper::FillMode::ASPECTFIT;
    case SceneObject::FillMode::ASPECTCROP:
    default: return wallpaper::FillMode::ASPECTCROP;
    }
}

} // namespace

using sp_scene_t = std::shared_ptr<wallpaper::SceneWallpaper>;

namespace
{
// Destroying a renderer waits for its threads and tears down its vulkan
// device, tens of milliseconds. The last reference is usually dropped on the
// gui or the scene graph render thread, where that is a missed frame for the
// whole desktop. References are handed to this thread to drop instead.
class SceneReaper {
public:
    static SceneReaper& instance() {
        static SceneReaper reaper;
        return reaper;
    }
    void release(sp_scene_t&& scene) {
        if (! scene) return;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_queue.push_back(std::move(scene));
        }
        m_cv.notify_one();
    }

private:
    SceneReaper(): m_thread([this]() {
        run();
    }) {}
    // at process exit: finish what is queued, so no renderer is cut off mid teardown
    ~SceneReaper() {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_stop = true;
        }
        m_cv.notify_one();
        if (m_thread.joinable()) m_thread.join();
    }
    void run() {
        std::unique_lock<std::mutex> lock(m_mutex);
        while (true) {
            m_cv.wait(lock, [this]() {
                return m_stop || ! m_queue.empty();
            });
            if (m_queue.empty()) return;
            sp_scene_t scene = std::move(m_queue.front());
            m_queue.pop_front();
            lock.unlock();
            scene.reset();
            lock.lock();
        }
    }

    std::mutex              m_mutex;
    std::condition_variable m_cv;
    std::deque<sp_scene_t>  m_queue;
    bool                    m_stop { false };
    std::thread             m_thread;
};
} // namespace

namespace scenebackend
{

class TextureNode : public QObject, public QSGSimpleTextureNode {
    Q_OBJECT
public:
    TextureNode(QQuickWindow* window, sp_scene_t scene, bool valid)
        : m_texture(nullptr),
          m_scene(scene),
          m_enable_valid(valid),
          m_window(window),
          m_first_frame(false),
          m_link(std::make_shared<Link>()) {
        m_link->node = this;
        // texture node must have a texture, so use the default 0 texture.
        m_texture      = createTextureFromGl(0, QSize(64, 64), window);
        m_init_texture = m_texture;
        setTexture(m_texture);
        setFiltering(QSGTexture::Linear);
        setOwnsTexture(false);
    }

    ~TextureNode() override {
        {
            // the renderer's threads may still hold the callbacks
            std::lock_guard<std::mutex> lock(m_link->mtx);
            m_link->node = nullptr;
        }
        for (auto& item : texs_map) {
            auto& exh = item.second;
            if (exh.gltex != 0) m_glex.deleteTexture(exh.gltex);
            delete exh.qsg;
        }
        delete m_init_texture;
        SceneReaper::instance().release(std::move(m_scene));
        _Q_INFO("Destroy texnode", "");
    }

    // only at qt render thread
    bool initGl() { return m_glex.init(get_proc_address); }

    // after gl, can run at any thread
    void initVulkan(uint16_t w, uint16_t h) {
        wallpaper::RenderInitInfo info;
        info.enable_valid_layer = m_enable_valid;
        info.offscreen          = true;
        info.offscreen_tiling   = m_glex.tiling();
        auto uuid = m_glex.uuid();
        info.uuid.assign(uuid.begin(), uuid.end());
        info.width           = w;
        info.height          = h;
        // The callbacks run on the renderer's threads and can outlive this node,
        // so they reach it through the link, which the destructor clears.
        info.redraw_callback = [link = m_link]() {
            std::lock_guard<std::mutex> lock(link->mtx);
            if (link->node) Q_EMIT link->node->redraw();
        };

        auto cb = std::make_shared<wallpaper::FirstFrameCallback>([link = m_link]() {
            std::lock_guard<std::mutex> lock(link->mtx);
            if (! link->node) return;
            link->node->m_first_frame = true;
            Q_EMIT link->node->redraw();
        });
        m_scene->setPropertyObject(wallpaper::PROPERTY_FIRST_FRAME_CALLBACK, cb);
        // this send to looper, not in this thread
        m_scene->initVulkan(info);
    }

signals:
    void redraw();
    void sceneFirstFrame();

public slots:
    void newTexture() {
        if (! m_scene->inited() || m_scene->exSwapchain() == nullptr) return;

        wallpaper::ExHandle* exh = m_scene->exSwapchain()->eatFrame();
        if (exh != nullptr) {
            int id = exh->id();
            if (texs_map.count(id) == 0) {
                _Q_INFO("receive external texture(%dx%d) from fd: %d",
                        exh->width,
                        exh->height,
                        exh->fd);
                ExTex ex_tex;
                // takes the fd on success, no close here
                uint gltex = m_glex.genExTexture(*exh);

                ex_tex.gltex = gltex;
                ex_tex.qsg   = gltex == 0 ? nullptr
                                          : createTextureFromGl(
                                                gltex, QSize(exh->width, exh->height), m_window);
                texs_map[id] = ex_tex;
            }
            auto& newtex = texs_map.at(id);
            if (newtex.qsg != nullptr)
                m_texture = newtex.qsg;
            else
                m_texture = m_init_texture;

            setTexture(m_texture);
            markDirty(DirtyMaterial);

            bool expected = true;
            if (m_first_frame.compare_exchange_strong(expected, false)) {
                Q_EMIT sceneFirstFrame();
            }
        }
    }

private:
    sp_scene_t m_scene;
    bool       m_enable_valid;

    QSGTexture*       m_init_texture;
    QSGTexture*       m_texture;
    QQuickWindow*     m_window;
    std::atomic<bool> m_first_frame;

    GlExtra m_glex;

    struct Link {
        std::mutex   mtx;
        TextureNode* node { nullptr };
    };
    std::shared_ptr<Link> m_link;

    struct ExTex {
        // int fd;
        uint        gltex;
        QSGTexture* qsg;
    };
    std::unordered_map<int, ExTex> texs_map;
};

} // namespace scenebackend

SceneObject::SceneObject(QQuickItem* parent)
    : QQuickItem(parent), m_scene(std::make_shared<wallpaper::SceneWallpaper>()) {
    setFlag(ItemHasContents, true);
    m_scene->init();
    m_scene->setPropertyString(wallpaper::PROPERTY_CACHE_PATH, GetDefaultCachePath());
}

// Qt can drop the paint node while this item lives (the item moved to another
// window, the scene graph was invalidated). The renderer's exported images were
// imported into the old node's gl context and can't be imported again, so the
// item starts over with a new renderer.
void SceneObject::resetScene() {
    SceneReaper::instance().release(std::move(m_scene));
    m_scene = std::make_shared<wallpaper::SceneWallpaper>();
    m_scene->init();
    m_scene->setPropertyString(wallpaper::PROPERTY_CACHE_PATH, GetDefaultCachePath());
    m_scene->setPropertyInt32(wallpaper::PROPERTY_FPS, m_fps);
    m_scene->setPropertyInt32(wallpaper::PROPERTY_FILLMODE, (int32_t)ToWPFillMode(m_fillMode));
    m_scene->setPropertyFloat(wallpaper::PROPERTY_SPEED, m_speed);
    m_scene->setPropertyFloat(wallpaper::PROPERTY_VOLUME, m_volume);
    m_scene->setPropertyBool(wallpaper::PROPERTY_MUTED, m_muted);
    if (! m_assets.isEmpty()) setScenePropertyQurl(wallpaper::PROPERTY_ASSETS, m_assets);
    if (! m_source.isEmpty()) setScenePropertyQurl(wallpaper::PROPERTY_SOURCE, m_source);
    if (! m_userProperties.isEmpty())
        m_scene->setPropertyString(wallpaper::PROPERTY_USER_PROPS, m_userProperties.toStdString());
    if (m_paused) m_scene->pause();
}

SceneObject::~SceneObject() {
    SceneReaper::instance().release(std::move(m_scene));
    _Q_INFO("Destroy sceneobject", "");
}

QSGNode* SceneObject::updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*) {
    TextureNode* node = static_cast<TextureNode*>(oldNode);
    if (! node) {
        if (m_had_node) resetScene();
        m_had_node = true;
        node = new TextureNode(window(), m_scene, m_enable_valid);
        if (node->initGl()) {
            // rounded: with fractional scaling, truncating loses a pixel and
            // every frame gets resampled to fit
            const qreal dpr = window()->devicePixelRatio();
            node->initVulkan((uint16_t)std::clamp(qRound(width() * dpr), 1, 16384),
                             (uint16_t)std::clamp(qRound(height() * dpr), 1, 16384));

            connect(
                node, &TextureNode::redraw, window(), &QQuickWindow::update, Qt::QueuedConnection);
            connect(window(),
                    &QQuickWindow::beforeRendering,
                    node,
                    &TextureNode::newTexture,
                    Qt::DirectConnection);
            connect(node, &TextureNode::sceneFirstFrame, this, &SceneObject::firstFrame);
        }
    }

    node->setRect(boundingRect());
    return node;
}

#define SET_PROPERTY(type, name, value) m_scene->setProperty##type(name, value);

void SceneObject::setScenePropertyQurl(std::string_view name, QUrl value) {
    auto str_value = QDir::toNativeSeparators(value.toLocalFile()).toStdString();
    SET_PROPERTY(String, name, str_value);
}
// qobject

QUrl SceneObject::source() const { return m_source; }
QUrl SceneObject::assets() const { return m_assets; }

int   SceneObject::fps() const { return m_fps; }
int   SceneObject::fillMode() const { return m_fillMode; }
float SceneObject::speed() const { return m_speed; }
float SceneObject::volume() const { return m_volume; }
bool  SceneObject::muted() const { return m_muted; }

void SceneObject::setSource(const QUrl& source) {
    if (source == m_source) return;
    m_source = source;
    setScenePropertyQurl(wallpaper::PROPERTY_SOURCE, m_source);
    Q_EMIT sourceChanged();
}

void SceneObject::setAssets(const QUrl& assets) {
    if (m_assets == assets) return;
    m_assets = assets;
    setScenePropertyQurl(wallpaper::PROPERTY_ASSETS, m_assets);
}

void SceneObject::setFps(int value) {
    if (m_fps == value) return;
    m_fps = value;
    SET_PROPERTY(Int32, wallpaper::PROPERTY_FPS, value);
    Q_EMIT fpsChanged();
}
void SceneObject::setFillMode(int value) {
    if (m_fillMode == value) return;
    m_fillMode = value;
    SET_PROPERTY(Int32, wallpaper::PROPERTY_FILLMODE, (int32_t)ToWPFillMode(value));
    Q_EMIT fillModeChanged();
}
void SceneObject::setSpeed(float value) {
    if (m_speed == value) return;
    m_speed = value;
    SET_PROPERTY(Float, wallpaper::PROPERTY_SPEED, value);
    Q_EMIT speedChanged();
}
void SceneObject::setVolume(float value) {
    if (m_volume == value) return;
    m_volume = value;
    SET_PROPERTY(Float, wallpaper::PROPERTY_VOLUME, value);
    Q_EMIT volumeChanged();
}
void SceneObject::setMuted(bool value) {
    if (m_muted == value) return;
    m_muted = value;
    SET_PROPERTY(Bool, wallpaper::PROPERTY_MUTED, value);
}

QString SceneObject::userProperties() const { return m_userProperties; }

void SceneObject::setUserProperties(const QString& value) {
    if (m_userProperties == value) return;
    m_userProperties = value;
    SET_PROPERTY(String, wallpaper::PROPERTY_USER_PROPS, value.toStdString());
    Q_EMIT userPropertiesChanged();
}

void SceneObject::play() {
    m_paused = false;
    m_scene->play();
}
void SceneObject::pause() {
    m_paused = true;
    m_scene->pause();
}

bool SceneObject::vulkanValid() const { return m_enable_valid; }
void SceneObject::enableVulkanValid() { m_enable_valid = true; }
void SceneObject::enableGenGraphviz() { SET_PROPERTY(Bool, wallpaper::PROPERTY_GRAPHIVZ, true); }

void SceneObject::setAcceptMouse(bool value) {
    if (value)
        setAcceptedMouseButtons(Qt::LeftButton);
    else
        setAcceptedMouseButtons(Qt::NoButton);
}

void SceneObject::setAcceptHover(bool value) { setAcceptHoverEvents(value); }

void SceneObject::mousePressEvent(QMouseEvent* event) {}
void SceneObject::mouseMoveEvent(QMouseEvent* event) {
    auto pos = event->position();
    m_scene->mouseInput(pos.x() / width(), pos.y() / height());
}

void SceneObject::hoverMoveEvent(QHoverEvent* event) {
    auto pos = event->position();
    m_scene->mouseInput(pos.x() / width(), pos.y() / height());
}

std::string SceneObject::GetDefaultCachePath() {
    return wallpaper::platform::GetCachePath(CACHE_DIR);
}

#include "SceneBackend.moc"
