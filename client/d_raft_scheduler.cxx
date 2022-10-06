
#include "d_raft_scheduler.hxx"
using namespace d_raft_scheduler;

Scheduler::Scheduler(size_t size, const Error error) :   
    size(size),
    error(error)
{
    if (error == nullptr) {
        throw std::runtime_error("Invalid callback specified");
    }
}
                                    

static std::chrono::time_point execution_time = std::chrono::system_clock::now();
void Scheduler::schedule(const Tasks t, long n, nuraft::request req) {
    std::unique_lock<std::mutex> lock(this->mutex);
    condition.wait(lock, [this]{ return this->count < this->size; });
    count++;
    
    execution_time += std::chrono::microseconds(n);

    auto task = std::make_shared<Tasks>(t);
    std::thread thread{
        [n, task, req, this] {
            std::this_thread::sleep_until(execution_time);

            try {
                (*task)(req);
            } catch (const std::exception &e) {
                this->error(e);
            } catch (...) {
                this->error(std::runtime_error("Unknown error"));
            }

            condition.notify_one();
            count--;
        }
    };
    thread.detach();
}
 
void Scheduler::wait() {
    std::unique_lock<std::mutex> lock(this->mutex);
    condition.wait(lock, [this] { return this->count == 0; });
}
