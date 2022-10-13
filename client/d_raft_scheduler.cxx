
#include "d_raft_scheduler.hxx"
using namespace d_raft_scheduler;

Scheduler::Scheduler(size_t size, const Error error)
    : size(size)
    , error(error) {
    if (error == nullptr) {
        throw std::runtime_error("Invalid callback specified");
    }
}

void Scheduler::add_task_to_queue(req_socket_manager* mgr) { task_queue.emplace(mgr); }

void Scheduler::schedule(const Tasks t) {
    std::unique_lock<std::mutex> lock(this->mutex);
    condition.wait(lock, [this] { return this->count < this->size; });
    count++;
    auto task = std::make_shared<Tasks>(t);
    std::thread thread{[task, this] {
        try {
            (*task)(task_queue.top());
            task_queue.pop();
        } catch (const std::exception& e) {
            this->error(e);
        } catch (...) {
            this->error(std::runtime_error("Unknown error"));
        }
        condition.notify_one();
        count--;
    }};
    thread.detach();
}

void Scheduler::wait() {
    std::unique_lock<std::mutex> lock(this->mutex);
    condition.wait(lock, [this] { return this->count == 0; });
}
