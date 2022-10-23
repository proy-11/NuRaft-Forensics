#include "job_queue.hxx"
#include <iostream>

job_queue::job_queue(job_func jfunc_)
    : jfunc(jfunc_) {
    sem_threads = std::shared_ptr<semaphore<MAX_THREADS>>(new semaphore<MAX_THREADS>(MAX_THREADS));
    sem_jobs = std::shared_ptr<semaphore<MAX_QUEUE_LEN>>(new semaphore<MAX_QUEUE_LEN>(0));
}

job_queue::~job_queue() {}

bool job_queue::enque(int sock, std::string request) {
    mutex.lock();
    if (jobs.size() < MAX_QUEUE_LEN) {
        jobs.push(std::make_pair(sock, request));
        // std::fprintf(stderr, "enque \n");
        mutex.unlock();
        sem_jobs->release();
        return true;
    } else {
        mutex.unlock();
        return false;
    }
}

std::pair<int, std::string> job_queue::deque() {
    sem_threads->acquire();
    mutex.lock();
    auto result = jobs.front();
    jobs.pop();
    mutex.unlock();
    return result;
}

void job_queue::process_jobs() {
    std::thread separated_thr([this]() -> void {
        while (true) {
            sem_jobs->acquire();
            auto pair = deque();

            std::thread thr(
                [this](std::pair<int, std::string> pair) -> void {
                    jfunc(pair.first, pair.second);
                    sem_threads->release();
                },
                pair);
            thr.detach();
        }
    });
    separated_thr.detach();
}