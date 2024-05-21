/************************************************************************
Copyright 2017-2019 eBay Inc.
Author/Developer(s): Jung-Sang Ahn

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    https://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
**************************************************************************/

#include "echo_state_machine.hxx"
#include "in_memory_state_mgr.hxx"

#include "libnuraft/nuraft.hxx"

#include <chrono>
#include <string>
#include <thread>

using namespace nuraft;

class raft_client {
public:
    raft_client()
        : logger_(nullptr)
        , state_machine_(cs_new<echo_state_machine>())
        , state_manager_(cs_new<inmem_state_mgr>(1, "localhost:12345"))
        , asio_opt_()
        , params_()
        , launcher_()
        , server_() {}

    ~raft_client() {}

    bool initialise_raft_server() {
        int port_number = 12345;
        server_ = launcher_.init(
            state_machine_, state_manager_, logger_, port_number, asio_opt_, params_);

        while (!server_->is_initialized()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        return true;
    }

    void append_log_entries() {
        std::string msg = "hello world";
        ptr<buffer> log = buffer::alloc(sizeof(int) + msg.size());
        buffer_serializer bs_log(log);
        bs_log.put_str(msg);
        server_->append_entries({log});
    }

    void shutdown() { launcher_.shutdown(); }

private:
    ptr<logger> logger_;

    ptr<state_machine> state_machine_;

    ptr<state_mgr> state_manager_;

    asio_service::options asio_opt_;

    raft_params params_;

    raft_launcher launcher_;

    ptr<raft_server> server_;
};

int main(int argc, char** argv) {
    raft_client client;
    if (client.initialise_raft_server()) {
        client.append_log_entries();
    }
    client.shutdown();
    return 0;
}
