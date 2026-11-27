#include "VideoDecoder.hpp"

#include "Utils/Logging.h"

#ifdef WP_HAVE_MPV

#include <mpv/client.h>
#include <mpv/render.h>
#include <mpv/stream_cb.h>

#include <algorithm>
#include <atomic>
#include <clocale>
#include <cstring>
#include <string>

namespace wallpaper
{
namespace
{

// The file as mpv reads it: a stream over the bytes in memory. mpv opens the
// stream more than once (probing, then playing), each with its own position.
struct MemStream {
    const std::vector<uint8_t>* data;
    int64_t                     pos { 0 };
};

int64_t StreamRead(void* cookie, char* buf, uint64_t nbytes) {
    auto*   s    = static_cast<MemStream*>(cookie);
    int64_t left = (int64_t)s->data->size() - s->pos;
    if (left <= 0) return 0;
    int64_t n = std::min<int64_t>(left, (int64_t)nbytes);
    std::memcpy(buf, s->data->data() + s->pos, (size_t)n);
    s->pos += n;
    return n;
}
int64_t StreamSeek(void* cookie, int64_t offset) {
    auto* s = static_cast<MemStream*>(cookie);
    if (offset < 0 || offset > (int64_t)s->data->size()) return MPV_ERROR_GENERIC;
    s->pos = offset;
    return offset;
}
int64_t StreamSize(void* cookie) {
    return (int64_t)static_cast<MemStream*>(cookie)->data->size();
}
void StreamClose(void* cookie) { delete static_cast<MemStream*>(cookie); }

int StreamOpen(void* user_data, char*, mpv_stream_cb_info* info) {
    info->cookie  = new MemStream { static_cast<const std::vector<uint8_t>*>(user_data) };
    info->read_fn = StreamRead;
    info->seek_fn = StreamSeek;
    info->size_fn = StreamSize;
    info->close_fn = StreamClose;
    return 0;
}

class MpvVideo : public IVideoSource {
public:
    MpvVideo(std::vector<uint8_t> data, int32_t width, int32_t height)
        : m_data(std::move(data)), m_width(width), m_height(height) {}

    ~MpvVideo() override {
        if (m_render) mpv_render_context_free(m_render);
        // stops playback and with it every use of m_data
        if (m_mpv) mpv_terminate_destroy(m_mpv);
    }

    bool Init() {
        // mpv refuses to start otherwise; the plugin sets this too
        std::setlocale(LC_NUMERIC, "C");
        m_mpv = mpv_create();
        if (! m_mpv) return false;

        mpv_set_option_string(m_mpv, "terminal", "no");
        mpv_set_option_string(m_mpv, "config", "no");
        mpv_set_option_string(m_mpv, "vo", "libmpv");
        // the scene has its own sound objects; a texture is picture only
        mpv_set_option_string(m_mpv, "audio", "no");
        mpv_set_option_string(m_mpv, "loop-file", "inf");
        mpv_set_option_string(m_mpv, "input-default-bindings", "no");
        mpv_set_option_string(m_mpv, "osc", "no");
        // Decode on the gpu where possible, with the frames copied back for
        // the software renderer. Measured on a 4K h264 loop this and the fast
        // converter below take 10 ms of cpu a frame; mpv's defaults take 25.
        mpv_set_option_string(m_mpv, "hwdec", "auto-copy");
        mpv_set_option_string(m_mpv, "sws-allow-zimg", "no");
        mpv_set_option_string(m_mpv, "sws-fast", "yes");
        mpv_set_option_string(m_mpv, "sws-scaler", "fast-bilinear");
        if (mpv_initialize(m_mpv) < 0) return false;

        if (mpv_stream_cb_add_ro(m_mpv, "wekmem", const_cast<std::vector<uint8_t>*>(&m_data), StreamOpen) < 0) return false;

        mpv_render_param params[] = {
            { MPV_RENDER_PARAM_API_TYPE, const_cast<char*>(MPV_RENDER_API_TYPE_SW) },
            { MPV_RENDER_PARAM_INVALID, nullptr },
        };
        if (mpv_render_context_create(&m_render, m_mpv, params) < 0) return false;
        mpv_render_context_set_update_callback(
            m_render,
            [](void* self) {
                static_cast<MpvVideo*>(self)->m_update = true;
            },
            this);

        const char* cmd[] = { "loadfile", "wekmem://video", nullptr };
        return mpv_command(m_mpv, cmd) >= 0;
    }

    bool NextFrame(uint8_t* rgba) override {
        // nobody reads the events, but the queue must not fill up
        while (mpv_wait_event(m_mpv, 0)->event_id != MPV_EVENT_NONE) {
        }
        if (! m_update.exchange(false)) return false;
        if (! (mpv_render_context_update(m_render) & MPV_RENDER_UPDATE_FRAME)) return false;

        int               size[2] { m_width, m_height };
        size_t            stride = (size_t)m_width * 4;
        mpv_render_param  params[] = {
            { MPV_RENDER_PARAM_SW_SIZE, size },
            { MPV_RENDER_PARAM_SW_FORMAT, const_cast<char*>("rgb0") },
            { MPV_RENDER_PARAM_SW_STRIDE, &stride },
            { MPV_RENDER_PARAM_SW_POINTER, rgba },
            { MPV_RENDER_PARAM_INVALID, nullptr },
        };
        if (mpv_render_context_render(m_render, params) < 0) return false;

        // the fourth byte of each pixel is padding; the texture's view reads alpha as 1
        return true;
    }

    void SetPaused(bool paused) override {
        int flag = paused ? 1 : 0;
        mpv_set_property(m_mpv, "pause", MPV_FORMAT_FLAG, &flag);
    }

private:
    const std::vector<uint8_t> m_data;
    const int32_t              m_width;
    const int32_t              m_height;

    mpv_handle*         m_mpv { nullptr };
    mpv_render_context* m_render { nullptr };
    std::atomic<bool>   m_update { false };
};

} // namespace

std::shared_ptr<IVideoSource> CreateVideoDecoder(std::vector<uint8_t> data, int32_t width,
                                                 int32_t height) {
    auto video = std::make_shared<MpvVideo>(std::move(data), width, height);
    if (! video->Init()) {
        LOG_ERROR("can't start the video player for a video texture");
        return nullptr;
    }
    return video;
}

} // namespace wallpaper

#else

namespace wallpaper
{
std::shared_ptr<IVideoSource> CreateVideoDecoder(std::vector<uint8_t>, int32_t, int32_t) {
    return nullptr;
}
} // namespace wallpaper

#endif
