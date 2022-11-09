#include "d_raft_byzantine_controller.hxx"
using namespace d_raft_byz_controller;

byzantine_controller::byzantine_controller(std::string byz_fault):
    m_is_byzantine(false),
    m_byzantine_fault_map(d_raft_byz_controller::create_byz_fault_map()),
    m_byzantine_fault(byzantine_fault_t::none),
    m_eng(std::random_device{}())
{

    byzantine_fault_t fault = get_byzantine_fault_from_map(byz_fault);
    if(fault != byzantine_fault_t::none) {
        m_is_byzantine = true;
        set_byzantine_fault(fault);
    }
}

byzantine_controller::~byzantine_controller() {}

byzantine_fault_t byzantine_controller::get_byzantine_fault_from_map(std::string fault) {
    auto it = m_byzantine_fault_map.find(fault);
    if(it != m_byzantine_fault_map.end()) {
        return it->second;
    } else {
        return byzantine_fault_t::none;
    }
}

void byzantine_controller::set_byzantine_fault(byzantine_fault_t &fault) {
    m_byzantine_fault = fault;
}

byzantine_fault_t byzantine_controller::get_byzantine_fault() {
    return m_byzantine_fault;
}

bool byzantine_controller::get_byzantine_status() {
    return m_is_byzantine;
}

void byzantine_controller::perform_sleep() {
    std::uniform_int_distribution<> dist{10, 100};
    std::this_thread::sleep_for(std::chrono::milliseconds{dist(m_eng)});
}

void byzantine_controller::perform_kill_self() {
    pid_t pid = getpid();
    if(kill(pid, SIGKILL) < 0) {
        std::cerr << " Failed to kill byzantine server :" << strerror(errno) << std::endl;
    }
}

void byzantine_controller::perform_restart() {
    
}

void byzantine_controller::create_byzantine_fault() {
    if(!get_byzantine_status()) {
        return;
    }
    byzantine_fault_t fault = get_byzantine_fault();
    switch (fault)
    {
        case byzantine_fault_t::sleep:
            perform_sleep();
            break;
        case byzantine_fault_t::kill_self:
            perform_kill_self();
            break;
        case byzantine_fault_t::restart:
            perform_restart();
            break;
        case byzantine_fault_t::incorrect_append:
            break;
        case byzantine_fault_t::return_incorrect_result:
            break;
        default:
            break;
    }
}

