#pragma once

#include <atomic>
#include <mutex>
#include <queue>
#include <semaphore>
#include <string>
#include <thread>

#define MAX_THREADS 1024
#define MAX_QUEUE_LEN 300000

template <ptrdiff_t diff> using semaphore = std::counting_semaphore<diff>;
using job_func = void (*)(int, std::string);

class job_queue {
public:
    job_queue(job_func jfunc_);
    ~job_queue();

    bool enque(int sock, std::string request);
    std::pair<int, std::string> deque();

    void process_jobs();

private:
    job_func jfunc;
    std::mutex mutex;
    std::shared_ptr<semaphore<MAX_THREADS>> sem_threads;
    std::shared_ptr<semaphore<MAX_QUEUE_LEN>> sem_jobs;
    std::queue<std::pair<int, std::string>> jobs;
};
