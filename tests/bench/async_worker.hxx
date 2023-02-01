#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdarg>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

class async_worker {
public:
    async_worker(int parallel)
        : active_(true)
        , max_workers_(parallel)
        , available_(parallel)
        , tid_(0UL) {}
    ~async_worker() { join(); }

    template <class T, class... TArgs> void add_job(T&& job, TArgs&&... args) {
        if (!active_) return;

        std::unique_lock<std::mutex> lock(mutex_);
        cv_worker_.wait(lock, [this]() -> bool { return available_ > 0; });
        available_--;
        std::thread thr([this, job, args...]() {
            job(args...);
            std::unique_lock<std::mutex> lock(mutex_);
            available_++;
            cv_worker_.notify_one();
        });
        thr.detach();
        // job(args...);
    }

    void join() {
        active_ = false;
        std::unique_lock<std::mutex> lock(mutex_);
        cv_worker_.wait(lock, [this]() -> bool { return available_ >= max_workers_; });
    }

private:
    std::atomic_bool active_;
    int max_workers_;
    std::condition_variable cv_worker_;
    std::atomic_int available_;
    std::mutex mutex_;
    std::atomic_int64_t tid_;
    std::unordered_map<uint64_t, std::thread> jobs_;
};