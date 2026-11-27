#include "ThreadTimer.hpp"

#include <cassert>

using namespace wallpaper;
using micros = std::chrono::microseconds;

ThreadTimer::ThreadTimer(std::function<void()> cb): m_callback(cb) {}
ThreadTimer::~ThreadTimer() { Stop(); }

bool ThreadTimer::Running() const { return m_running; }

void ThreadTimer::SetInterval(micros v) { m_interval = v; }

void ThreadTimer::Start() {
    std::unique_lock<std::mutex> lock(m_op_mutex);

    if (Running()) return;
    // set before the thread starts, or its first check can see false and exit
    m_running      = true;
    m_timer_thread = std::thread([this]() {
        while (true) {
            {
                std::unique_lock<std::mutex> lock(m_cond_mutex);
                // returns early only when stopped
                if (m_condition.wait_for(lock, m_interval.load(), [this]() {
                        return ! Running();
                    }))
                    break;
            }
            if (m_callback) m_callback();
        }
    });
}

void ThreadTimer::Stop() {
    std::unique_lock<std::mutex> lock(m_op_mutex);
    assert(std::this_thread::get_id() != m_timer_thread.get_id());

    if (! Running()) return;
    {
        // under the mutex, so the thread can't miss it between its check and its wait
        std::unique_lock<std::mutex> lock(m_cond_mutex);
        m_running = false;
        m_condition.notify_all();
    }

    if (m_timer_thread.joinable()) {
        m_timer_thread.join();
    }
}
