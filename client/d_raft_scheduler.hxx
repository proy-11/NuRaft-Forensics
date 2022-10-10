#include "workload.hxx"
#include <iostream>
#include <memory>
#include <chrono>
#include <thread>
#include <condition_variable>
#include <queue>

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

        void add_task_to_queue(nuraft::request req);

        void schedule(Tasks f);

        void wait();
    
        virtual ~Scheduler() = default;
    
    private:
        std::condition_variable condition;
        std::mutex mutex;
        size_t size;
        const Error error;
        size_t count{};

        struct Lesser_Index {
            bool operator()(const nuraft::request& lhs, const nuraft::request& rhs) const
            {
                return lhs.index < rhs.index;
            }
        };

        std::priority_queue<nuraft::request , std::vector<nuraft::request >, Lesser_Index> task_queue;
};
}; // namespace d_raft_scheduler

#endif  // D_RAFT_SCHEDULER
