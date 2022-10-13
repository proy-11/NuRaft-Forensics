#include "req_socket_mgr.hxx"
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <memory>
#include <queue>
#include <thread>
#include <vector>

#ifndef D_RAFT_SCHEDULER
#define D_RAFT_SCHEDULER

using namespace std;
using Tasks = void (*)(req_socket_manager* mgr);
using Error = void (*)(const std::exception&);

namespace d_raft_scheduler {
class Scheduler {
public:
    Scheduler(size_t size, Error error);

    Scheduler(size_t size, nullptr_t) = delete;

    Scheduler(const Scheduler&) = delete;

    void add_task_to_queue(req_socket_manager* mgr);

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
        bool operator()(req_socket_manager* lhs, req_socket_manager* rhs) const { return lhs->seqno() < rhs->seqno(); }
    };

    std::priority_queue<req_socket_manager*, std::vector<req_socket_manager*>, Lesser_Index> task_queue;
};
}; // namespace d_raft_scheduler

#endif // D_RAFT_SCHEDULER
