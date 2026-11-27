#pragma once
#include <chrono>

namespace wallpaper
{

// Where the time of one scene load goes. Each loading thread has its own, the
// thread resets it when it starts a load and logs it when done.
struct LoadStats {
    double tex_header_ms { 0 };
    double shader_ms { 0 };
    double tex_decode_ms { 0 };
    double tex_upload_ms { 0 };
    double reflect_ms { 0 };
    double pipeline_ms { 0 };
    double rt_ms { 0 };
    double buf_ms { 0 };
    int    pipelines { 0 };
    int    tex_decoded { 0 };

    static LoadStats& get() {
        static thread_local LoadStats stats;
        return stats;
    }

    // adds the time it lives to one of the fields
    class Timer {
    public:
        explicit Timer(double& acc): m_acc(acc), m_start(std::chrono::steady_clock::now()) {}
        ~Timer() {
            m_acc += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() -
                                                               m_start)
                         .count();
        }
        Timer(const Timer&)            = delete;
        Timer& operator=(const Timer&) = delete;

    private:
        double&                               m_acc;
        std::chrono::steady_clock::time_point m_start;
    };
};

} // namespace wallpaper
