#ifndef _FAULT_CONTROLLER_HXX_
#define _FAULT_CONTROLLER_HXX_

#include "ptr.hxx"
#include "raft_server.hxx"
#include "fault_types.hxx"

#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <unordered_map>
#include <signal.h>
#include <random>
#if defined(__linux__) || defined(__APPLE__)
    #include <unistd.h> 
#endif

namespace nuraft {

static inline std::unordered_map<std::string, fault_type> create_fault_map() {
    std::unordered_map<std::string, fault_type> fault_map = {
        {"none", fault_type::none},
        {"sleep", fault_type::sleep}, 
        {"kill_self", fault_type::kill_self},
        {"vote_monopoly", fault_type::vote_monopoly},
        {"signal_false_commitments", fault_type::signal_false_commitments},
        {"ret_invalid_resp_to_client", fault_type::ret_invalid_resp_to_client},
        {"drop_random_incoming_messages", fault_type::drop_random_incoming_messages},
        {"corrupt_random_incoming_messages", fault_type::corrupt_random_incoming_messages},
        {"delay_processing_of_incoming_messages", fault_type::delay_processing_of_incoming_messages},
        {"send_delayed_logs_to_followers", fault_type::send_delayed_logs_to_followers},
        {"send_corrupt_logs_to_followers", fault_type::send_corrupt_logs_to_followers},
        {"send_invalid_request_to_followers", fault_type::send_invalid_request_to_followers},
        {"send_diff_requests_to_diff_followers", fault_type::send_diff_requests_to_diff_followers},
        {"follower_returns_invalid_response", fault_type::follower_returns_invalid_response}
    };
    return fault_map;
}

class fault_controller {
public:
    fault_controller(std::string fault, ptr<raft_server>& server);
    
    ~fault_controller();

    fault_type get_fault_type_from_map(std::string fault);
    
    fault_type get_fault();
    
    bool get_server_fault_status();

    bool get_is_server_under_attack();

    void set_is_server_under_attack(bool attack);

    void set_cluster_size(int size);

    void inject_fault();

private:
    void set_fault(fault_type &fault);

    void perform_sleep();
    
    void perform_kill_self();

    void initiate_vote_monopoly_attack();

    void initiate_false_commitments();

    void initiate_ret_invalid_resp_to_client();
    
    void initiate_drop_random_incoming_messages();

    void initiate_corrupt_random_incoming_messages();

    void initiate_delay_processing_of_incoming_messages();

    void initiate_send_delayed_logs_to_followers();

    void initiate_send_corrupt_logs_to_followers();

    void initiate_send_invalid_request_to_followers();

    void initiate_send_diff_requests_to_diff_followers();

    void initiate_follower_returns_invalid_response();

    bool check_if_all_servers_are_added();

    ptr<raft_server> raft_server_;
    
    size_t cluster_size_;

    bool is_server_faulty_;
    
    bool is_under_attack_;

    const std::unordered_map<std::string, fault_type> fault_map_;
    
    fault_type fault_;
    
    std::mt19937_64 eng_;
};

} // namespace nuraft

#endif // _FAULT_CONTROLLER_HXX_
