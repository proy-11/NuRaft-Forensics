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
        , max_queue_length(max_qlen) {}
    ~job_queue() {}

    bool enque(T element) {
        bool success = false;
        {
            std::unique_lock<std::mutex> lock(job_allocation_lock);
            if ((success = jobs.size() < max_queue_length)) {
                debug_print("Try IN\n");
                jobs.push(element);
                debug_print("IN , height = %6d\n", jobs.size());
            }
        }
        if (success) {
            cv_jobs.notify_one();
        }
        return success;
    }

    T deque() {
        debug_print("Try OUT, waiting for mutex\n");
        std::unique_lock<std::mutex> lock(job_allocation_lock);
        debug_print("Try OUT, waiting for jobs\n");
        cv_jobs.wait(lock, [this]() -> bool { return !jobs.empty(); });
        T result = jobs.front();
        jobs.pop();
        debug_print("OUT, height = %6d\n", jobs.size());
        return result;
    }

    void process_jobs() {
        std::thread looper([this]() -> void {
            while (true) {
                auto pair = deque();

                debug_print("Waiting for thread\n");
                std::unique_lock<std::mutex> lock(thread_allocation_lock);
                cv_threads.wait(lock, [this]() -> bool { return nthreads > 0; });
                nthreads--;

                debug_print("Got free thread, resource remaining = %d\n", int(nthreads));

                std::thread thr(
                    [this](T element) -> void {
                        jfunc(element);
                        std::unique_lock<std::mutex> lock(thread_allocation_lock);
                        nthreads++;
                        cv_threads.notify_one();
                    },
                    pair);
                thr.detach();
            }
        });
        looper.detach();
    }

private:
    job_func<T> jfunc;
    std::atomic<int> nthreads;
    int max_queue_length;
    std::mutex job_allocation_lock;
    std::mutex thread_allocation_lock;
    std::condition_variable cv_jobs;
    std::condition_variable cv_threads;
    std::queue<T> jobs;
};
