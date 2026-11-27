// Headless benchmark for the scene renderer. It drives SceneWallpaper through the
// same offscreen path the Plasma plugin uses (exported images, no window), and
// prints one JSON object with the measurements. See docs/benchmark.md.
//
// usage: scenebench [options] <assets dir> <scene.pkg|scene.json>...
//   --res WxH       render size (default 3840x2160)
//   --seconds S     length of each steady-state measurement (default 8)
//   --warm N        warm loads per scene (default 5)
//   --cycles N      create/destroy cycles for the leak check (default 30)
//   --cache DIR     shader cache directory (required; use a fresh one for a cold first load)
//   --out FILE      write the JSON here instead of stdout
//   --load-only     just load each scene once and report which ones reach a first
//                   frame; for checking a whole library after a parser change

#include <sys/resource.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "SceneWallpaper.hpp"
#include "SceneWallpaperSurface.hpp"

using Clock = std::chrono::steady_clock;

namespace
{

double msSince(Clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

double cpuMs() {
    rusage ru {};
    getrusage(RUSAGE_SELF, &ru);
    auto tv = [](const timeval& t) {
        return (double)t.tv_sec * 1000.0 + (double)t.tv_usec / 1000.0;
    };
    return tv(ru.ru_utime) + tv(ru.ru_stime);
}

long countDir(const char* path) {
    long            n = 0;
    std::error_code ec;
    for (auto it = std::filesystem::directory_iterator(path, ec);
         ! ec && it != std::filesystem::directory_iterator();
         it.increment(ec))
        n++;
    return n;
}
long openFds() { return countDir("/proc/self/fd") - 1; } // minus the iterator's own fd
long threads() { return countDir("/proc/self/task"); }
long rssKb() {
    std::ifstream f("/proc/self/status");
    std::string   line;
    while (std::getline(f, line)) {
        if (line.rfind("VmRSS:", 0) == 0) return std::strtol(line.c_str() + 6, nullptr, 10);
    }
    return -1;
}

// Average GPU utilisation (%) of this process over `samples` seconds, as reported
// by nvidia-smi. Returns -1 when nvidia-smi is missing or never lists the process.
double gpuUtil(int samples) {
    std::string cmd  = "nvidia-smi pmon -s u -c " + std::to_string(samples) + " 2>/dev/null";
    FILE*       pipe = popen(cmd.c_str(), "r");
    if (pipe == nullptr) return -1;
    const std::string pid = std::to_string(getpid());
    char              line[512];
    double            sum = 0;
    int               n   = 0;
    while (std::fgets(line, sizeof(line), pipe) != nullptr) {
        std::istringstream is(line);
        std::string        gpu, p, type, sm;
        if (! (is >> gpu >> p >> type >> sm) || p != pid) continue;
        sum += sm == "-" ? 0.0 : std::atof(sm.c_str());
        n++;
    }
    pclose(pipe);
    return n > 0 ? sum / n : -1;
}

struct Options {
    uint16_t                 width { 3840 };
    uint16_t                 height { 2160 };
    double                   seconds { 8 };
    int                      warm { 5 };
    int                      cycles { 30 };
    std::string              cache;
    std::string              out;
    bool                     load_only { false };
    int                      timeout { 60 };
    std::string              assets;
    std::vector<std::string> scenes;
};

// One live wallpaper, set up the way qml_helper/SceneBackend.cpp does it.
struct Instance {
    std::unique_ptr<wallpaper::SceneWallpaper> sw;
    std::atomic<long>                          frames { 0 };
    std::mutex                                 mtx;
    std::condition_variable                    cv;
    bool                                       first_frame { false };
    double                                     load_ms { -1 };

    bool load(const Options& opt, const std::string& scene, int fps) {
        auto t0 = Clock::now();
        sw      = std::make_unique<wallpaper::SceneWallpaper>();
        sw->init();
        sw->setPropertyString(wallpaper::PROPERTY_CACHE_PATH, opt.cache);
        sw->setPropertyBool(wallpaper::PROPERTY_MUTED, true);
        sw->setPropertyInt32(wallpaper::PROPERTY_FPS, fps);
        sw->setPropertyString(wallpaper::PROPERTY_ASSETS, opt.assets);
        sw->setPropertyString(wallpaper::PROPERTY_SOURCE, scene);

        auto cb = std::make_shared<wallpaper::FirstFrameCallback>([this]() {
            {
                std::lock_guard<std::mutex> lock(mtx);
                first_frame = true;
            }
            cv.notify_all();
        });
        sw->setPropertyObject(wallpaper::PROPERTY_FIRST_FRAME_CALLBACK, cb);

        wallpaper::RenderInitInfo info;
        info.offscreen       = true;
        info.width           = opt.width;
        info.height          = opt.height;
        info.redraw_callback = [this]() {
            frames++;
            cv.notify_all();
        };
        sw->initVulkan(info);

        std::unique_lock<std::mutex> lock(mtx);
        bool ok = cv.wait_for(lock, std::chrono::seconds(opt.timeout), [this] {
            return first_frame;
        });
        if (ok) load_ms = msSince(t0);
        return ok;
    }

    // Take frames like the Qt side does, for `seconds`. Returns frames rendered.
    long consume(double seconds) {
        long start = frames.load();
        auto end   = Clock::now() + std::chrono::duration<double>(seconds);
        std::unique_lock<std::mutex> lock(mtx);
        while (Clock::now() < end) {
            cv.wait_until(lock, end);
            if (auto* sc = sw->exSwapchain(); sc != nullptr) (void)sc->eatFrame();
        }
        return frames.load() - start;
    }

    double destroy() {
        auto t0 = Clock::now();
        sw.reset();
        return msSince(t0);
    }
};

double median(std::vector<double> v) {
    if (v.empty()) return -1;
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

std::string jnum(double v) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.3f", v);
    return buf;
}
std::string jstr(const std::string& s) {
    std::string o = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') o += '\\';
        o += c;
    }
    return o + "\"";
}

struct Steady {
    int    fps;
    double seconds;
    long   frames;
    double cpu_ms;
    double gpu_util { -1 };
};

Steady steady(const Options& opt, const std::string& scene, int fps, std::vector<double>& loads,
              std::vector<double>& teardowns) {
    Steady   res { fps, opt.seconds, 0, 0 };
    Instance inst;
    if (! inst.load(opt, scene, fps)) {
        inst.destroy();
        return res;
    }
    loads.push_back(inst.load_ms);
    inst.consume(1.0); // settle
    // nvidia-smi runs as a separate process, so it does not count in cpuMs()
    std::thread gpu([&]() {
        res.gpu_util = gpuUtil(std::max(1, (int)opt.seconds - 1));
    });
    double c0   = cpuMs();
    auto   t0   = Clock::now();
    res.frames  = inst.consume(opt.seconds);
    res.seconds = msSince(t0) / 1000.0;
    res.cpu_ms  = cpuMs() - c0;
    gpu.join();
    teardowns.push_back(inst.destroy());
    return res;
}

} // namespace

int main(int argc, char** argv) {
    Options opt;
    for (int i = 1; i < argc; i++) {
        std::string a    = argv[i];
        auto        next = [&]() -> std::string {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "missing value for %s\n", a.c_str());
                std::exit(2);
            }
            return argv[++i];
        };
        if (a == "--res") {
            int w = 0, h = 0;
            if (std::sscanf(next().c_str(), "%dx%d", &w, &h) != 2 || w <= 0 || h <= 0) return 2;
            opt.width  = (uint16_t)w;
            opt.height = (uint16_t)h;
        } else if (a == "--seconds") {
            opt.seconds = std::atof(next().c_str());
        } else if (a == "--warm") {
            opt.warm = std::atoi(next().c_str());
        } else if (a == "--cycles") {
            opt.cycles = std::atoi(next().c_str());
        } else if (a == "--cache") {
            opt.cache = next();
        } else if (a == "--out") {
            opt.out = next();
        } else if (a == "--load-only") {
            opt.load_only = true;
            opt.timeout   = 20;
        } else if (opt.assets.empty()) {
            opt.assets = a;
        } else {
            opt.scenes.push_back(a);
        }
    }
    if (opt.assets.empty() || opt.scenes.empty() || opt.cache.empty()) {
        std::fprintf(stderr,
                     "usage: scenebench --cache DIR [--res WxH] [--seconds S] [--warm N] "
                     "[--cycles N] [--out FILE] <assets> <scene>...\n");
        return 2;
    }

    if (opt.load_only) {
        std::ostringstream js;
        js << "[\n";
        for (std::size_t i = 0; i < opt.scenes.size(); i++) {
            const auto& scene = opt.scenes[i];
            std::fprintf(stderr, "== load %s\n", scene.c_str());
            Instance inst;
            bool     ok     = inst.load(opt, scene, 15);
            long     frames = ok ? inst.consume(0.5) : 0;
            inst.destroy();
            js << (i ? ",\n" : "") << "  {\"scene\": " << jstr(scene)
               << ", \"ok\": " << (ok ? "true" : "false") << ", \"load_ms\": "
               << jnum(inst.load_ms) << ", \"frames\": " << frames << "}";
        }
        js << "\n]\n";
        if (opt.out.empty()) {
            std::fputs(js.str().c_str(), stdout);
        } else {
            std::ofstream f(opt.out);
            f << js.str();
        }
        return 0;
    }

    std::ostringstream js;
    js << "{\n  \"width\": " << opt.width << ", \"height\": " << opt.height
       << ",\n  \"scenes\": [\n";

    bool first_scene = true;
    for (const auto& scene : opt.scenes) {
        std::fprintf(stderr, "== bench %s\n", scene.c_str());

        // cold: nothing for this scene in the shader cache yet
        double cold_ms = -1, cold_teardown = -1;
        {
            Instance inst;
            if (inst.load(opt, scene, 15)) cold_ms = inst.load_ms;
            cold_teardown = inst.destroy();
        }

        std::vector<double> loads, teardowns;
        for (int i = 0; cold_ms >= 0 && i < opt.warm; i++) {
            Instance inst;
            if (inst.load(opt, scene, 15)) {
                loads.push_back(inst.load_ms);
                inst.consume(0.3);
            }
            teardowns.push_back(inst.destroy());
        }

        std::vector<Steady> st;
        if (cold_ms >= 0) {
            for (int fps : { 15, 240 }) st.push_back(steady(opt, scene, fps, loads, teardowns));
        }

        js << (first_scene ? "" : ",\n") << "    {\"scene\": " << jstr(scene)
           << ", \"ok\": " << (cold_ms >= 0 ? "true" : "false")
           << ", \"load_cold_ms\": " << jnum(cold_ms) << ", \"load_warm_ms\": "
           << jnum(median(loads)) << ", \"teardown_ms\": " << jnum(median(teardowns))
           << ", \"teardown_cold_ms\": " << jnum(cold_teardown) << ", \"steady\": [";
        for (std::size_t i = 0; i < st.size(); i++) {
            const auto& s = st[i];
            js << (i ? ", " : "") << "{\"fps\": " << s.fps << ", \"seconds\": " << jnum(s.seconds)
               << ", \"frames\": " << s.frames << ", \"cpu_ms\": " << jnum(s.cpu_ms)
               << ", \"gpu_util\": " << jnum(s.gpu_util) << "}";
        }
        js << "]}";
        first_scene = false;
    }
    js << "\n  ],\n";

    // Leak check: create and destroy wallpapers the way a playlist does. The images
    // each renderer exports are never imported here, which is the case of a wallpaper
    // dropped before all of its frames were shown.
    {
        std::fprintf(stderr, "== cycles\n");
        auto one = [&](int i) {
            Instance inst;
            if (inst.load(opt, opt.scenes[(std::size_t)i % opt.scenes.size()], 15)) inst.consume(0.2);
            inst.destroy();
        };
        for (int i = 0; i < 3; i++) one(i); // warm-up, lets allocators settle
        long fd0 = openFds(), th0 = threads(), rss0 = rssKb();
        auto t0  = Clock::now();
        for (int i = 0; i < opt.cycles; i++) one(i);
        double total_ms = msSince(t0);
        long   fd1 = openFds(), th1 = threads(), rss1 = rssKb();
        js << "  \"cycles\": {\"count\": " << opt.cycles << ", \"total_ms\": " << jnum(total_ms)
           << ", \"fds_before\": " << fd0 << ", \"fds_after\": " << fd1
           << ", \"threads_before\": " << th0 << ", \"threads_after\": " << th1
           << ", \"rss_kb_before\": " << rss0 << ", \"rss_kb_after\": " << rss1 << "}\n";
    }
    js << "}\n";

    if (opt.out.empty()) {
        std::fputs(js.str().c_str(), stdout);
    } else {
        std::ofstream f(opt.out);
        f << js.str();
    }
    return 0;
}
