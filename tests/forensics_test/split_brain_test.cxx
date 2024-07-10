#include <cstdlib>  // for getenv

#include "debugging_options.hxx"
#include "fake_network.hxx"
#include "raft_package_fake.hxx"

#include "event_awaiter.h"
#include "test_common.h"

#include <stdio.h>

using namespace nuraft;
using namespace raft_functional_common;

namespace split_brain_test {

std::string get_env_var(const std::string& var_name) {
    const char* env_var = std::getenv(var_name.c_str());
    if (env_var == nullptr) {
        throw std::runtime_error("Environment variable not found: " + var_name);
    }
    return std::string(env_var);
}

void split_brain_exchange_all_in_partitions(std::vector<RaftPkg*> pkgs) {
    auto s1 = *pkgs[0];
    auto s2 = *pkgs[1];
    auto s3 = *pkgs[2];
    auto s4 = *pkgs[3];
    auto s5 = *pkgs[4];

    bool msg_to_exchange = true;
    while (msg_to_exchange) {
        msg_to_exchange = false;
        // partition 1 sends requests
        msg_to_exchange |= s1.fNet->delieverReqTo("S2");
        msg_to_exchange |= s1.fNet->delieverReqTo("S3");
        msg_to_exchange |= s2.fNet->delieverReqTo("S1");
        msg_to_exchange |= s2.fNet->delieverReqTo("S3");

        // partition 2 sends requests
        msg_to_exchange |= s4.fNet->delieverReqTo("S3");
        msg_to_exchange |= s4.fNet->delieverReqTo("S5");
        msg_to_exchange |= s5.fNet->delieverReqTo("S3");
        msg_to_exchange |= s5.fNet->delieverReqTo("S4");

        // partition 1 handles responses
        msg_to_exchange |= s1.fNet->handleRespFrom("S2");
        msg_to_exchange |= s1.fNet->handleRespFrom("S3");
        msg_to_exchange |= s2.fNet->handleRespFrom("S1");
        msg_to_exchange |= s2.fNet->handleRespFrom("S3");

        // partition 2 handles responses

        msg_to_exchange |= s4.fNet->handleRespFrom("S3");
        msg_to_exchange |= s4.fNet->handleRespFrom("S5");
        msg_to_exchange |= s5.fNet->handleRespFrom("S3");
        msg_to_exchange |= s5.fNet->handleRespFrom("S4");

        // S3 sends requests and handle responses
        msg_to_exchange |= s3.fNet->execReqResp();
    }
}

int basic_split_brain_test() {
    // in this test, S3 is the malicious nodes after the network is paritioned into 2
    // parts. Partition 1: S1, S2, S3 Partition 2: S3, S4, S5

    reset_log_files();
    ptr<FakeNetworkBase> f_base = cs_new<FakeNetworkBase>();

    std::string s1_addr = "S1";
    std::string s2_addr = "S2";
    std::string s3_addr = "S3";
    std::string s4_addr = "S4";
    std::string s5_addr = "S5";

    RaftPkg s1(f_base, 1, s1_addr);
    RaftPkg s2(f_base, 2, s2_addr);
    RaftPkg s3(f_base, 3, s3_addr);
    RaftPkg s4(f_base, 4, s4_addr);
    RaftPkg s5(f_base, 5, s5_addr);

    std::vector<RaftPkg*> pkgs = {&s1, &s2, &s3, &s4, &s5};

    std::string home_dir = get_env_var("R_OUT_PATH");

    raft_params custom_params;
    custom_params.election_timeout_lower_bound_ = 0;
    custom_params.election_timeout_upper_bound_ = 1000;
    custom_params.heart_beat_interval_ = 500;
    custom_params.snapshot_distance_ = 100;
    CHK_Z(launch_servers_malicious(pkgs, &custom_params, 2, home_dir));
    CHK_Z(make_group(pkgs));

    for (auto& entry: pkgs) {
        RaftPkg* pp = entry;
        raft_params param = pp->raftServer->get_current_params();
        param.return_method_ = raft_params::async_handler;
        pp->raftServer->update_params(param);
    }

    const size_t NUM = 1000;

    // Append messages asynchronously.
    for (size_t ii = 0; ii < NUM; ++ii) {
        int test_msg = ii * 2;
        ptr<buffer> msg = buffer::alloc(sizeof(int));
        msg->put(test_msg);
        s1.raftServer->append_entries({msg});
    }

    // exchange messages across all server
    while (s1.fNet->execReqResp())
        ;

    // Wait for bg commit.
    CHK_Z(wait_for_sm_exec(pkgs, COMMIT_TIMEOUT_SEC));

    // Check if all messages are committed.
    for (size_t ii = 0; ii < NUM; ++ii) {
        int test_msg = ii * 2;
        uint64_t idx = s1.getTestSm()->isCommitted(test_msg);
        CHK_GT(idx, 0);
    }

    /** NETWORK SPLIT, malicious node: S3 */
    // initiate leader election
    s3.fTimer->invoke(timer_task_type::election_timer);
    s1.fTimer->invoke(timer_task_type::election_timer);
    s2.fTimer->invoke(timer_task_type::election_timer);
    s4.fTimer->invoke(timer_task_type::election_timer);
    s5.fTimer->invoke(timer_task_type::election_timer);

    s3.fNet->execReqResp();

    s3.dbgLog(" --- Now S3 is leader ---");

    // Allow ReqResp within each partition
    while (s3.fNet->execReqResp())
        ;

    /** START ATTACK */
    auto mal_raft_ptr = std::dynamic_pointer_cast<mal_raft_server>(s3.raftServer);
    auto mal_config = new SplitBrainConfig({{1, 2}, {4, 5}});
    mal_raft_ptr->start_attack(SplitBrain, mal_config);

    // Append messages asynchronously.
    // S3 will send benign messages to S1 and S2, and modified malicious messages
    // (incremented by 1) to S4 and S5
    for (size_t ii = 0; ii < NUM; ++ii) {
        int test_msg = (NUM + ii) * 2;
        ptr<buffer> msg = buffer::alloc(sizeof(int));
        msg->put(test_msg);
        s3.raftServer->append_entries({msg});
    }
    
    while (s3.fNet->execReqResp())
        ;

    CHK_Z(wait_for_sm_exec(pkgs, COMMIT_TIMEOUT_SEC));

    // Check if all messages are committed correctly including malicious messages.
    for (size_t ii = 0; ii < NUM; ++ii) {
        int test_msg = (NUM + ii) * 2;
        uint64_t idx = s1.getTestSm()->isCommitted(test_msg);
        CHK_GT(idx, 0);
        idx = s2.getTestSm()->isCommitted(test_msg);
        CHK_GT(idx, 0);
        idx = s4.getTestSm()->isCommitted(test_msg + 1);
        CHK_GT(idx, 0);
        idx = s5.getTestSm()->isCommitted(test_msg + 1);
        CHK_GT(idx, 0);
    }

    print_stats(pkgs);

    s1.raftServer->shutdown();
    s2.raftServer->shutdown();
    s3.raftServer->shutdown();

    f_base->destroy();

    return 0;
}

} // namespace split_brain_test
using namespace split_brain_test;


int main(int argc, char** argv) {
    TestSuite ts(argc, argv);

    ts.options.printTestMessage = true;

    // Disable reconnection timer for deterministic test.
    debugging_options::get_instance().disable_reconn_backoff_ = true;

    ts.doTest("basic split brain test", basic_split_brain_test);

    return 0;
}
