#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdarg>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <queue>
#include <sstream>
#include <string>
#include <thread>

#ifdef DEBUG
void debug_print(const char* fmt, ...) {
    std::ostringstream oss;
    oss << "[T " << std::setw(4) << std::this_thread::get_id() << "] ";

    va_list args;
    va_start(args, fmt);
    std::vfprintf(stderr, (oss.str() + fmt).c_str(), args);
    va_end(args);
}
#else
void debug_print(const char* fmt, ...) {}
#endif // DEBUG

template <class T> using job_func = void (*)(T);

template <class T> class job_queue {
public:
    job_queue(job_func<T> jfunc_, int max_threads, int max_qlen)
        : jfunc(jfunc_)
        , nthreads(max_threads)
        , target_nthreads(max_threads)
        , max_queue_length(max_qlen)
        , ended(false)
        , looper(nullptr) {}
    ~job_queue() {
        ended = true;
        cv_jobs.notify_all();
        if (looper) looper->join();
        std::unique_lock<std::mutex> lock(thread_allocation_lock);
        cv_threads.wait(lock, [this]() -> bool { return nthreads >= target_nthreads; });
        std::fprintf(stderr, "destroyed job queue\n");
    }

    bool enque(T element) {
        if (ended) return false;
        bool success = false;
        {
            std::unique_lock<std::mutex> lock(job_allocation_lock);
            if ((success = jobs.size() < max_queue_length)) {
                jobs.push(element);
            }
        }
        if (success) {
            cv_jobs.notify_one();
        }
        return success;
    }

    std::shared_ptr<T> deque() {
        std::unique_lock<std::mutex> lock(job_allocation_lock);
        cv_jobs.wait(lock, [this]() -> bool { return ended || !jobs.empty(); });
        if (ended) return nullptr;

        std::shared_ptr<T> result = std::make_shared<T>(jobs.front());
        jobs.pop();
        return result;
    }

    void process_jobs() {
        looper = std::shared_ptr<std::thread>(new std::thread([this]() -> void {
            while (!ended) {
                std::shared_ptr<T> pair = deque();

                if (ended) return;

                std::unique_lock<std::mutex> lock(thread_allocation_lock);
                cv_threads.wait(lock, [this]() -> bool { return nthreads > 0; });
                nthreads--;

                std::thread(
                    [this](T element) -> void {
                        jfunc(element);
                        std::unique_lock<std::mutex> lock(thread_allocation_lock);
                        nthreads++;
                        cv_threads.notify_one();
                    },
                    *pair)
                    .detach();
            }
        }));
    }

private:
    job_func<T> jfunc;
    std::atomic<int> nthreads;
    int target_nthreads;
    int max_queue_length;
    std::atomic<bool> ended;
    std::mutex job_allocation_lock;
    std::mutex thread_allocation_lock;
    std::condition_variable cv_jobs;
    std::condition_variable cv_threads;
    std::queue<T> jobs;
    std::shared_ptr<std::thread> looper;
};
