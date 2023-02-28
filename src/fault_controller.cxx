#include "fault_controller.hxx"

namespace nuraft {
    
fault_controller::fault_controller(std::string fault, ptr<raft_server>& server)
    : raft_server_(server)
    , cluster_size_(-1)
    , is_server_faulty_(false)
    , is_under_attack_(false)
    , fault_map_(nuraft::create_fault_map())
    , fault_(fault_type::none)
    , eng_(std::random_device{}()) {

    fault_type server_fault = get_fault_type_from_map(fault);
    if(server_fault != fault_type::none) {
        is_server_faulty_ = true;
        set_fault(server_fault);
    }
}

fault_controller::~fault_controller() {}

fault_type fault_controller::get_fault_type_from_map(std::string fault) {
    auto it = fault_map_.find(fault);
    if(it != fault_map_.end()) {
        return it->second;
    } else {
        return fault_type::none;
    }
}

void fault_controller::set_fault(fault_type &fault) {
    fault_ = fault;
}

fault_type fault_controller::get_fault() {
    return fault_;
}

bool fault_controller::get_server_fault_status() {
    return is_server_faulty_;
}

void fault_controller::set_is_server_under_attack(bool attack) {
    is_under_attack_ = attack;
}

bool fault_controller::get_is_server_under_attack() {
    return is_under_attack_;
}

void fault_controller::set_cluster_size(int size) {
    cluster_size_ = size;
}

void fault_controller::perform_sleep() {
    std::cout << "-----\n" << "Sleeping...\n";
    std::uniform_int_distribution<> dist{10, 100};
    std::this_thread::sleep_for(std::chrono::milliseconds{dist(eng_)});
}

void fault_controller::perform_kill_self() {
    pid_t pid = getpid();
    std::cout << "-----\n" << "Killing...\n";
    if(kill(pid, SIGKILL) < 0) {
        std::cerr << "Failed to kill byzantine server :" << std::endl;
    }
}

void fault_controller::initiate_vote_monopoly_attack() {
    std::cout << "-----\n" << "Initiating vote monopoly attack...\n";
    raft_server_->set_fault_type(fault_type::vote_monopoly);
    raft_server_->initiate_attack();
    while(true) {
        int leader = raft_server_->get_leader();
        std::cout << "Current Leader ID: " << leader << std::endl;
        sleep(4);
        if(leader == -1) {
            break;
        }
    }
}

void fault_controller::initiate_false_commitments() {
    std::cout << "-----\n" << "Initiating false commitments...\n";
    raft_server_->set_fault_type(fault_type::signal_false_commitments);
    raft_server_->initiate_attack();
}

void fault_controller::initiate_ret_invalid_resp_to_client() {
    std::cout << "-----\n" << "Initiating return invalid response to client...\n";
    raft_server_->set_fault_type(fault_type::ret_invalid_resp_to_client);
    raft_server_->initiate_attack();
}

void fault_controller::initiate_drop_random_incoming_messages() {
    std::cout << "-----\n" << "Initiating drop random incoming messages...\n";
    raft_server_->set_fault_type(fault_type::drop_random_incoming_messages);
    raft_server_->initiate_attack();
}

void fault_controller::initiate_corrupt_random_incoming_messages() {
    std::cout << "-----\n" << "Initiating corrupt random incoming messages...\n";
    raft_server_->set_fault_type(fault_type::corrupt_random_incoming_messages);
    raft_server_->initiate_attack();
}

void fault_controller::initiate_delay_processing_of_incoming_messages() {
    std::cout << "-----\n" << "Initiating delay processing of incoming messages...\n";
    raft_server_->set_fault_type(fault_type::delay_processing_of_incoming_messages);
    raft_server_->initiate_attack();
}

void fault_controller::initiate_send_delayed_logs_to_followers() {
    std::cout << "-----\n" << "Initiating send delayed logs to followers...\n";
    raft_server_->set_fault_type(fault_type::send_delayed_logs_to_followers);
    raft_server_->initiate_attack();
}

void fault_controller::initiate_send_corrupt_logs_to_followers() {
    std::cout << "-----\n" << "Initiating send corrupt logs to followers...\n";
    raft_server_->set_fault_type(fault_type::send_corrupt_logs_to_followers);
    raft_server_->initiate_attack();
}

void fault_controller::initiate_send_invalid_request_to_followers() {
    std::cout << "-----\n" << "Initiating send invalid requests to followers...\n";
    raft_server_->set_fault_type(fault_type::send_invalid_request_to_followers);
    raft_server_->initiate_attack();
}

void fault_controller::initiate_send_diff_requests_to_diff_followers() {
    std::cout << "-----\n" << "Initiating send diff requests to diff followers...\n";
    raft_server_->set_fault_type(fault_type::send_diff_requests_to_diff_followers);
    raft_server_->initiate_attack();
}

void fault_controller::initiate_follower_returns_invalid_response() {
    std::cout << "-----\n" << "Initiating follower sends invalid response...\n";
    raft_server_->set_fault_type(fault_type::follower_returns_invalid_response);
    raft_server_->initiate_attack();
}

bool fault_controller::check_if_all_servers_are_added() {
    bool ret_val = false;
    std::vector<ptr<srv_config>> configs;
    raft_server_->get_srv_config_all(configs);
    if(cluster_size_ == configs.size()) {
        ret_val = true;
    }
    return ret_val;
}

void fault_controller::inject_fault() {
    if(!get_server_fault_status()) {
        return;
    }
    
    if(!get_is_server_under_attack()) {
        return;
    }

    while(true) {
        if(check_if_all_servers_are_added()) {
            break;
        }
    }

    fault_type fault = get_fault();
    switch(fault) {
        case fault_type::sleep:
            perform_sleep();
            break;
        case fault_type::kill_self:
            perform_kill_self();
            break;
        case fault_type::vote_monopoly:
            initiate_vote_monopoly_attack();
            break;
        case fault_type::signal_false_commitments:
            initiate_false_commitments();
            break;
        case fault_type::ret_invalid_resp_to_client:
            initiate_ret_invalid_resp_to_client();
            break;
        case fault_type::drop_random_incoming_messages:
            initiate_drop_random_incoming_messages();
            break;
        case fault_type::corrupt_random_incoming_messages:
            initiate_corrupt_random_incoming_messages();
            break;
        case fault_type::delay_processing_of_incoming_messages:
            initiate_delay_processing_of_incoming_messages();
            break;
        case fault_type::send_delayed_logs_to_followers:
            initiate_send_delayed_logs_to_followers();
            break;
        case fault_type::send_corrupt_logs_to_followers:
            initiate_send_corrupt_logs_to_followers();
            break;
        case fault_type::send_invalid_request_to_followers:
            initiate_send_invalid_request_to_followers();
            break;
        case fault_type::send_diff_requests_to_diff_followers:
            initiate_send_diff_requests_to_diff_followers();
            break;
        case fault_type::follower_returns_invalid_response:
            initiate_follower_returns_invalid_response();
            break;
        default:
            break;
    }
}

} // namesprace nuraft
