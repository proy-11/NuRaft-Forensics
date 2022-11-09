#include <iostream>
#include <thread>
#include <random>
#include <chrono>
#include <unistd.h>
#include <unordered_map>
#include <sys/types.h>
#include <signal.h>
#include <string.h>

#ifndef D_RAFT_BYZ_CONTROLLER
#define D_RAFT_BYZ_CONTROLLER

namespace d_raft_byz_controller {

enum class byzantine_fault_t {
    sleep,
    kill_self,
    restart,
    incorrect_append,
    return_incorrect_result,
    none
};

inline std::unordered_map<std::string,byzantine_fault_t> create_byz_fault_map() {
    std::unordered_map<std::string,byzantine_fault_t> byz_map = {
        {"Sleep",byzantine_fault_t::sleep}, 
        {"Kill_self",byzantine_fault_t::kill_self},
        {"Restart",byzantine_fault_t::restart},
        {"Incorrect_append",byzantine_fault_t::incorrect_append},
        {"Return_incorrect_result",byzantine_fault_t::return_incorrect_result},
        {"None",byzantine_fault_t::none}
    };
    return byz_map;
}

class byzantine_controller {
public:
    byzantine_controller(std::string byz_fault);

    ~byzantine_controller();
    
    byzantine_fault_t get_byzantine_fault_from_map(std::string fault);
    
    byzantine_fault_t get_byzantine_fault();
    
    bool get_byzantine_status();

    void create_byzantine_fault();

private:
    void set_byzantine_fault(byzantine_fault_t &fault);
   
    void perform_sleep();
    
    void perform_kill_self();
    
    void perform_restart();
    
    bool m_is_byzantine;
    
    const std::unordered_map<std::string,byzantine_fault_t> m_byzantine_fault_map;
    
    byzantine_fault_t m_byzantine_fault;
    
    std::mt19937_64 m_eng;
};

}; // namespace d_raft_byz_controller
#endif
