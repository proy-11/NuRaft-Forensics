#include "workload.hxx"
#include <iostream>
#include <memory>
#include <chrono>
#include <thread>
#include <condition_variable>

#ifndef D_RAFT_SCHEDULER
#define D_RAFT_SCHEDULER

using namespace std;
using Tasks = void (*)(nuraft::request req);
using Error = void (*)(const std::exception &);

namespace d_raft_scheduler { 
class Scheduler {
    public:
        Scheduler(size_t size, Error error);
    
        Scheduler(size_t size, nullptr_t) = delete;
    
        Scheduler(const Scheduler &) = delete;
    
        void schedule(Tasks f, std::chrono::time_point<std::chrono::system_clock>  n, nuraft::request req);
    
        void wait();
    
        virtual ~Scheduler() = default;
    
    private:
        std::condition_variable condition;
        std::mutex mutex;
        size_t size;
        const Error error;
        size_t count{};
};
}; // namespace d_raft_scheduler

#endif  // D_RAFT_SCHEDULER
