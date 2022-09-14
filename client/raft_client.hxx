#include "echo_state_machine.hxx"
#include "in_memory_state_mgr.hxx"

#include "libnuraft/nuraft.hxx"

#include <chrono>
#include <string>
#include <thread>

using namespace nuraft;

class raft_client {
    public:
        raft_client(); 
        
        ~raft_client(); 

        bool initialise_raft_server();

        void append_log_entries();

        void shutdown();

    private:
        ptr<logger> logger_;

        ptr<state_machine> state_machine_;

        ptr<state_mgr> state_manager_;

        asio_service::options asio_opt_; 

        raft_params params_;

        raft_launcher launcher_;

        ptr<raft_server> server_;
};
