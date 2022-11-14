#include<raft_client.hxx>

raft_client::raft_client() : 
        logger_(nullptr),
        state_machine_(cs_new<echo_state_machine>()),
        state_manager_(cs_new<inmem_state_mgr>(1, "localhost:12345")),
        asio_opt_(),
        params_(),
        launcher_(),
        server_()
    {};

raft_client::~raft_client() 
    {};

bool raft_client::initialise_raft_server() {
    int port_number = 12345;
    server_ = launcher_.init(state_machine_,
                             state_manager_,
                             logger_,
                             port_number,
                             asio_opt_,
                             params_);

    while (!server_->is_initialized()) {
        std::this_thread::sleep_for( std::chrono::milliseconds(100) );
    }

    return true;
}

void raft_client::append_log_entries() {
    std::string msg = "hello world";
    ptr<buffer> log = buffer::alloc(sizeof(int) + msg.size());
    buffer_serializer bs_log(log);
    bs_log.put_str(msg);
    server_->append_entries({log});
}

void raft_client::shutdown() {
    launcher_.shutdown();
}
