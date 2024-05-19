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

#include "raft_functional_common.hxx"

#include "nuraft.hxx"

#include "async_worker.hxx"
#include "latency_collector.h"
#include "test_common.h"
#include <boost/filesystem.hpp>
#include "json.hpp"

using namespace nuraft;
using namespace raft_functional_common;

namespace fsys = boost::filesystem;
using json = nlohmann::json;

namespace raft_bench {

fsys::path global_workdir;
LatencyCollector global_lat;
size_t global_sid;

using raft_result = cmd_result<ptr<buffer>>;

std::map<ulong, std::string> commit_data;
class dummy_sm : public state_machine {
public:
    dummy_sm()
        : last_commit_idx_(0) {}
    ~dummy_sm() {}

    ptr<buffer> commit(const ulong log_idx, buffer& data) {
        buffer_serializer bs(data);
        commit_data.insert({log_idx, bs.get_cstr()});
        ptr<buffer> ret = buffer::alloc(sizeof(ulong));
        ret->put(log_idx);
        ret->pos(0);
        last_commit_idx_ = log_idx;
        return ret;
    }

    ptr<buffer> pre_commit(const ulong log_idx, buffer& data) { return nullptr; }

    void rollback(const ulong log_idx, buffer& data) {}
    void save_snapshot_data(snapshot& s, const ulong offset, buffer& data) {}
    void save_logical_snp_obj(
        snapshot& s, ulong& obj_id, buffer& data, bool is_first_obj, bool is_last_obj) {
        obj_id++;
    }
    bool apply_snapshot(snapshot& s) { return true; }
    int read_snapshot_data(snapshot& s, const ulong offset, buffer& data) { return 0; }

    int read_logical_snp_obj(snapshot& s,
                             void*& user_snp_ctx,
                             ulong obj_id,
                             ptr<buffer>& data_out,
                             bool& is_last_obj) {
        is_last_obj = true;
        data_out = buffer::alloc(sizeof(ulong));
        data_out->put(obj_id);
        data_out->pos(0);
        return 0;
    }

    void free_user_snp_ctx(void*& user_snp_ctx) {}

    ptr<snapshot> last_snapshot() {
        std::lock_guard<std::mutex> ll(last_snapshot_lock_);
        return last_snapshot_;
    }

    ulong last_commit_index() { return last_commit_idx_; }

    void create_snapshot(snapshot& s, async_result<bool>::handler_type& when_done) {
        {
            std::lock_guard<std::mutex> ll(last_snapshot_lock_);
            // NOTE: We only handle logical snapshot.
            ptr<buffer> snp_buf = s.serialize();
            last_snapshot_ = snapshot::deserialize(*snp_buf);
        }
        ptr<std::exception> except(nullptr);
        bool ret = true;
        when_done(ret, except);
    }

private:
    ptr<snapshot> last_snapshot_;
    std::mutex last_snapshot_lock_;
    uint64_t last_commit_idx_;
};

struct bench_config {
    bench_config(size_t _srv_id = 1,
                 const std::string& _my_endpoint = "tcp://localhost:25000",
                 size_t _duration = 30,
                 int _complexity = 3,
                 std::string fault = "none",
                 size_t cluster_size = 2,
                 int _parallel = 1,
                 size_t _iops = 5,
                 size_t _num_threads = 1,
                 size_t _payload_size = 128,
                 std::string _workdir = "")
        : srv_id_(_srv_id)
        , my_endpoint_(_my_endpoint)
        , duration_(_duration)
        , complexity_(_complexity)
        , fault_(fault)
        , cluster_size_(cluster_size)
        , parallel_(_parallel)
        , iops_(_iops)
        , num_threads_(_num_threads)
        , payload_size_(_payload_size)
        , workdir(_workdir) {}

    size_t srv_id_;
    std::string my_endpoint_;
    size_t duration_;
    int complexity_;
    std::string fault_;
    size_t cluster_size_;
    int parallel_;
    size_t iops_;
    size_t num_threads_;
    size_t payload_size_;
    std::vector<std::string> endpoints_;
    std::string workdir;
};

struct server_stuff {
    server_stuff()
        : server_id_(1)
        , addr_("localhost")
        , port_(25000) {}

    dummy_sm* get_sm() { return static_cast<dummy_sm*>(sm_.get()); }

    int server_id_;
    std::string addr_;
    int port_;

    // Endpoint: `tcp://<addr>:<port>`.
    std::string endpoint_;

    // Logger.
    ptr<logger_wrapper> log_wrap_;
    ptr<logger> raft_logger_;

    // State machine.
    ptr<state_machine> sm_;
    // State manager.
    ptr<state_mgr> smgr_;

    // ASIO things.
    ptr<asio_service> asio_svc_;
    ptr<rpc_listener> asio_listener_;

    // Raft server instance.
    ptr<raft_server> raft_instance_;
    
    ptr<fault_controller> fault_controller_;
};

struct response {
    response()
        : req_id_(-1)
        , return_code_(cmd_result_code::FAILED)
        , result_string_("")
        , is_accepted_(false)
        , response_time_("") {}

    int req_id_;
    cmd_result_code return_code_;
    std::string result_string_;
    bool is_accepted_;
    std::string response_time_;
};
std::vector<response> response_list;

int init_raft(server_stuff& stuff, int complexity, std::string fault) {
    // Create logger for this server.
    std::string log_file_name = global_workdir.string() + "/srv" + std::to_string(stuff.server_id_) + ".log";
    // std::string log_file_name = "./srv" + std::to_string(stuff.server_id_) + ".log";

    _msg("Writing log to %s\n", log_file_name.c_str());
    stuff.log_wrap_ = cs_new<logger_wrapper>(log_file_name, -1);
    stuff.raft_logger_ = stuff.log_wrap_;

    // Create state manager and state machine.
    stuff.smgr_ = cs_new<TestMgr>(stuff.server_id_, stuff.endpoint_);
    stuff.sm_ = cs_new<dummy_sm>();

    // Start ASIO service.
    asio_service::options asio_opt;
    asio_opt.thread_pool_size_ = 32;
    stuff.asio_svc_ = cs_new<asio_service>(asio_opt, stuff.raft_logger_);

    stuff.asio_listener_ =
        stuff.asio_svc_->create_rpc_listener(stuff.port_, stuff.raft_logger_);
    if (stuff.asio_listener_ == nullptr) {
        stuff.raft_logger_->err("asio listener failed to init\n");
    }

    ptr<delayed_task_scheduler> scheduler = stuff.asio_svc_;
    ptr<rpc_client_factory> rpc_cli_factory = stuff.asio_svc_;

    // Set parameters and start Raft server.
    raft_params params;
    params.heart_beat_interval_ = 500;
    params.election_timeout_lower_bound_ = 1000;
    params.election_timeout_upper_bound_ = 2000;
    params.reserved_log_items_ = 10000000;
    params.snapshot_distance_ = 100000;
    params.client_req_timeout_ = 4000;
    params.return_method_ = raft_params::blocking;
    // params.max_append_size_ = 100000; // FMARK: RN: using a very large batch size

    if (complexity < 3) {
        params.use_commitment_cert_ = false;
    }
    if (complexity < 2) {
        params.use_leader_sig_ = false;
    }
    if (complexity < 1) {
        params.use_chain_ptr_ = false;
    }

    context* ctx = new context(stuff.smgr_,
                               stuff.sm_,
                               stuff.asio_listener_,
                               stuff.raft_logger_,
                               rpc_cli_factory,
                               scheduler,
                               params);
    stuff.raft_instance_ = cs_new<raft_server>(ctx);

    // Listen.
    stuff.asio_listener_->listen(stuff.raft_instance_);

    stuff.fault_controller_ = cs_new<fault_controller>(fault, stuff.raft_instance_);

    // Wait until Raft server is ready (upto 10 seconds).
    const size_t MAX_TRY = 40;
    _msg("init Raft instance ");
    for (size_t ii = 0; ii < MAX_TRY; ++ii) {
        if (stuff.raft_instance_->is_initialized()) {
            _msg(" done\n");
            return 0;
        }
        _msg(".");
        fflush(stdout);
        TestSuite::sleep_ms(250);
    }
    std::cout << " FAILED" << std::endl;
    return -1;
}

int add_servers(server_stuff& stuff, const bench_config& config) {
    size_t num_srvs = config.endpoints_.size();
    for (size_t ii = 0; ii < num_srvs; ++ii) {
        int server_id_to_add = ii + 2;
        _msg("add server %d ", server_id_to_add);

        srv_config srv_conf_to_add(
            server_id_to_add, 1, config.endpoints_[ii], std::string(), false, 50);
        ptr<raft_result> ret = stuff.raft_instance_->add_srv(srv_conf_to_add);
        if (!ret->get_accepted()) {
            _msg(" .. failed");
            return -1;
        }

        // Wait until it appears in server list.
        const size_t MAX_TRY = 40;
        for (size_t jj = 0; jj < MAX_TRY; ++jj) {
            fflush(stdout);
            _msg(".");
            TestSuite::sleep_ms(250);
            ptr<srv_config> conf = stuff.raft_instance_->get_srv_config(server_id_to_add);
            if (conf) {
                _msg(" done\n");
                break;
            }
        }
    }

    return 0;
}

struct worker_params : public TestSuite::ThreadArgs {
    worker_params(const bench_config& _config, server_stuff& _stuff)
        : config_(_config)
        , stuff_(_stuff)
        , stop_signal_(false)
        , num_ops_done_(0)
        , wg_(config_.iops_) {}
    const bench_config& config_;
    server_stuff& stuff_;
    std::atomic<bool> stop_signal_;
    std::atomic<uint64_t> num_ops_done_;
    TestSuite::WorkloadGenerator wg_;
    std::mutex wg_lock_;
};

int submit_request(worker_params* args, ptr<buffer> msg) {
    TestSuite::Timer timer;
    msg->pos(0);
    ptr<raft_result> ret = args->stuff_.raft_instance_->append_entries({msg});
    global_lat.addLatency("rep", timer.getTimeUs());

    response res;
    res.response_time_ = TestSuite::getTimeString();
    res.req_id_ = args->num_ops_done_.load();
    res.is_accepted_ = ret->get_accepted();
    res.return_code_ = ret->get_result_code();
    res.result_string_ = ret->get_result_str();
    response_list.emplace_back(res);

    // CHK_TRUE(ret->get_accepted());
    // CHK_NONNULL(ret->get());


    {
        std::lock_guard<std::mutex> l(args->wg_lock_);
        args->wg_.addNumOpsDone(1);
    }
    args->num_ops_done_.fetch_add(1);
    return 0;
}

// typedef int (*job_t)(worker_params*, ptr<buffer>);

int worker_func(TestSuite::ThreadArgs* _args) {
    worker_params* args = static_cast<worker_params*>(_args);

    ptr<buffer> msg = buffer::alloc(args->config_.payload_size_);
    msg->put((byte)0x0);

    async_worker worker(args->config_.parallel_);

    while (!args->stop_signal_) {
        size_t num_ops = 0;
        {
            std::lock_guard<std::mutex> l(args->wg_lock_);
            num_ops = args->wg_.getNumOpsToDo();
        }
        if (!num_ops) {
            TestSuite::sleep_ms(1);
            continue;
        }

        // std::thread thr(submit_request, args, msg);
        // thr.join();
        if (args->config_.parallel_ == 1) {
            submit_request(args, msg);
        } else {
            worker.add_job(submit_request, args, msg);
        }
    }
    return 0;
}

void worker_killer_func(TestSuite::ThreadArgs* _args) {
    worker_params* args = static_cast<worker_params*>(_args);
    args->stop_signal_ = true;
}

void print_config(const bench_config& config) {
    _msg("-----\n");
    _msg("server id: %zu\n", config.srv_id_);
    _msg("run duration: %zu seconds\n", config.duration_);
    if (config.srv_id_ == 1) {
        _msg("traffic: %zu ops/sec\n", config.iops_);
        _msg("%zu threads\n", config.num_threads_);
        _msg("payload size: %zu bytes\n", config.payload_size_);
    }
    _msg("-----\n");
}

void ensure_dir(std::string directory) {
    if (!fsys::exists(directory)) {
        fsys::create_directories(fsys::absolute(directory));
    } else if (!fsys::is_directory(directory)) {
        fprintf(
            stderr, "Cannot create directory %s\n", fsys::absolute(directory).c_str());
        exit(1);
    }
}

void write_iops(uint64_t total_ops, uint64_t total_us) {
    fsys::path filename =
        global_workdir / fsys::path("iops_" + std::to_string(global_sid) + ".json");
    std::ofstream fs;
    fs.open(filename.string());
    if (!fs.good()) return;
    fs << "{\n";
    fs << "    \"ops\": " << total_ops << ",\n";
    fs << "    \"us\": " << total_us << ",\n";
    fs << "    \"iops\": " << total_ops * 1000000.0 / total_us << "\n";
    fs << "}\n";
    fs.close();
}

void write_latency_distribution() {
    fsys::path filename =
        global_workdir
        / fsys::path("raft_latency_" + std::to_string(global_sid) + ".json");
    std::ofstream fs;
    fs.open(filename.string());
    if (!fs.good()) return;

    fs << "{\n";
    fs << "    \"mean\": " << global_lat.getAvgLatency("rep") << "," << std::endl;

    for (size_t ii = 0; ii <= 99; ++ii) {
        fs << "    \"" << ii << "\": " << global_lat.getPercentile("rep", ii) << ","
           << std::endl;
    }
    fs << "    \"99.9\": " << global_lat.getPercentile("rep", 99.9) << ",\n";
    fs << "    \"99.99\": " << global_lat.getPercentile("rep", 99.99) << ",\n";
    fs << "    \"99.999\": " << global_lat.getPercentile("rep", 99.999) << "\n";
    fs << "}" << std::endl;
    fs.close();
}

void create_faults(server_stuff& stuff) {
    bool is_server_faulty = stuff.fault_controller_->get_server_fault_status();
    if(is_server_faulty) {
        _msg("Server %d is faulty\n", stuff.server_id_);
        stuff.fault_controller_->set_is_server_under_attack(true);
        stuff.fault_controller_->inject_fault();
    }
}

void write_response_result(server_stuff& stuff) {
    json result_data;

    int i = 0;
    for(auto& it:response_list) {
        result_data["responses"][i] = {
                                        {"req_id", it.req_id_},
                                        {"ret_code", int(it.return_code_)},
                                        {"result_string", it.result_string_},
                                        {"is_accepted", it.is_accepted_},
                                        {"response_time", it.response_time_}
                                    };
        ++i;
    }

    int j = 0;
    for(auto& it:commit_data) {
        result_data["commit_data"][j] = {
                                            {"commit_idx", it.first},
                                            {"commit_data", it.second}
                                        };
        ++j;
    }

    result_data["log_height"] = stuff.raft_instance_->get_last_log_idx();
    result_data["log_height_committed"] = stuff.raft_instance_->get_committed_log_idx();
    result_data["log_term"] = stuff.raft_instance_->get_last_log_term();
    result_data["term"] = stuff.raft_instance_->get_term();

    try {
        std::ofstream file((global_workdir / (std::string("server_response_") + std::to_string(stuff.server_id_) + ".json")).c_str());
        file << std::setw(4) << result_data;
        file.close();
    } catch (std::exception& ec) {
        _msg("cannot write result: %s\n", ec.what());
    }
}

int bench_main(const bench_config& config) {
    server_stuff stuff;
    stuff.server_id_ = config.srv_id_;
    stuff.endpoint_ = config.my_endpoint_;

    size_t pos = config.my_endpoint_.rfind(":");
    if (pos == std::string::npos) {
        std::cerr << "wrong endpoint: " << config.my_endpoint_ << std::endl;
        return -1;
    }
    stuff.port_ = atoi(config.my_endpoint_.substr(pos + 1).c_str());
    if (stuff.port_ < 1000) {
        std::cerr << "wrong port (should be >= 1000): " << stuff.port_ << std::endl;
        return -1;
    }

    print_config(config);

    CHK_Z(init_raft(stuff, config.complexity_, config.fault_));
    _msg("-----\n");

    stuff.fault_controller_->set_cluster_size(config.cluster_size_);
    
    if (stuff.server_id_ > 1) {
        create_faults(stuff);
        // Follower, just sleep
        TestSuite::sleep_sec(config.duration_, "ready");
    }

    // Leader.
    CHK_Z(add_servers(stuff, config));
    _msg("-----\n");

    if(stuff.server_id_ == 1) {
        create_faults(stuff);
    }

    worker_params param(config, stuff);
    std::vector<TestSuite::ThreadHolder> h_workers(config.num_threads_);
    for (size_t ii = 0; ii < h_workers.size(); ++ii) {
        TestSuite::ThreadHolder& h_worker = h_workers[ii];
        h_worker.spawn(&param, worker_func, worker_killer_func);
    }

    TestSuite::Displayer dd(1, 3);
    dd.init();
    std::vector<size_t> col_width(3, 15);
    dd.setWidth(col_width);
    TestSuite::Timer duration_timer(config.duration_ * 1000);

    uint64_t final_us = 1, final_ops = 0;
    while (!duration_timer.timeout()) {
        TestSuite::sleep_ms(80);
        uint64_t cur_us = duration_timer.getTimeUs();
        if (!cur_us) continue;

        uint64_t cur_ops = param.num_ops_done_;

        final_ops = cur_ops;
        final_us = cur_us;
        dd.set(0, 0, "%zu/%zu", cur_us / 1000000, config.duration_);
        dd.set(0, 1, "%zu", cur_ops);
        dd.set(0, 2, "%s ops/s", TestSuite::throughputStr(cur_ops, cur_us).c_str());
        dd.print();
    }
    param.stop_signal_ = true;

    for (size_t ii = 0; ii < h_workers.size(); ++ii) {
        TestSuite::ThreadHolder& tt = h_workers[ii];
        tt.join();
    }

    _msg("-----\n");
    TestSuite::_msg(
        "%15s%10s%10s%10s%10s%10s\n", "OP", "p50", "p95", "p99", "p99.9", "p99.99");

    TestSuite::_msg(
        "%15s%10s%10s%10s%10s%10s\n",
        "replication",
        TestSuite::usToString(global_lat.getPercentile("rep", 50)).c_str(),
        TestSuite::usToString(global_lat.getPercentile("rep", 95)).c_str(),
        TestSuite::usToString(global_lat.getPercentile("rep", 99)).c_str(),
        TestSuite::usToString(global_lat.getPercentile("rep", 99.9)).c_str(),
        TestSuite::usToString(global_lat.getPercentile("rep", 99.99)).c_str());
    _msg("-----\n");

    write_iops(final_ops, final_us);
    write_latency_distribution();
    write_response_result(stuff);

    _msg("Actual state machine commit index: %lu\n", stuff.sm_->last_commit_index());
    _msg("log_height: %lu\n", stuff.raft_instance_->get_last_log_idx());
    _msg("log_height_committed: %lu\n", stuff.raft_instance_->get_committed_log_idx());
    _msg("log_term: %lu\n", stuff.raft_instance_->get_last_log_term());
    _msg("term: %lu\n", stuff.raft_instance_->get_term());


    stuff.fault_controller_->set_is_server_under_attack(false);
    stuff.raft_instance_->shutdown();

    return 0;
}

void usage(int argc, char** argv) {
    std::stringstream ss;
    ss << "Usage: \n"
       << "    - Leader:\n"
       << "    raft_bench 1 <address:port>\n"
          "               <duration> <IOPS> <# threads> <payload size>\n"
          "               <server 2 addr:port> <server 3 addr:port> ...\n"
       << std::endl
       << "    - Follower:\n"
       << "    raft_bench <server ID> <address:port>\n"
       << std::endl;

    std::cout << ss.str();
    exit(0);
}

bench_config parse_config(int argc, char** argv) {
    // 0      1           2          3          4               5
    // <exec> <server ID> <endpoint> <duration> <complexity>    <workdir>
    // 6      7             8              9         10
    // <IOPS> <# pipelines> <payload size> <S2 addr> <S3 addr> ...
    if (argc < 6) usage(argc, argv);

    int iarg = 1;
    size_t srv_id = atoi(argv[iarg]);
    global_sid = srv_id;
    if (srv_id < 1) {
        std::cout << "server ID should be greater than zero." << std::endl;
        exit(0);
    }

    iarg++;
    std::string my_endpoint = argv[iarg];
    if (my_endpoint.find("tcp://") == std::string::npos) {
        my_endpoint = "tcp://" + my_endpoint;
    }

    iarg++;
    size_t duration = atoi(argv[iarg]);
    if (duration < 1) {
        std::cout << "duration should be greater than zero." << std::endl;
        exit(0);
    }

    iarg++;
    int complexity = atoi(argv[iarg]);
    if (complexity < 0 || complexity > 3) {
        std::cout << "complexity should be between 0 and 3." << std::endl;
        exit(0);
    }

    iarg++;
    std::string fault = argv[iarg];

    iarg++;
    size_t cluster_size = atoi(argv[iarg]);
    if (cluster_size < 1) {
        std::cout << "cluster size should be greater than zero." << std::endl;
        exit(0);
    }


    iarg++;
    std::string workdir(argv[iarg]);
    global_workdir = workdir;


    if (srv_id > 1) {
        // Follower.
        bench_config cfg = bench_config(srv_id, my_endpoint, duration, complexity);
        cfg.workdir = workdir;
        return cfg;
    }

    ensure_dir(workdir);

    if (argc < 10) usage(argc, argv);

    iarg++;
    size_t iops = atoi(argv[iarg]);
    if (iops < 1 || iops > 1000000) {
        std::cout << "valid IOPS range: 1 - 1M." << std::endl;
        exit(0);
    }

    iarg++;
    size_t num_threads = atoi(argv[iarg]);
    if (num_threads < 1 || num_threads > 128) {
        std::cout << "valid thread number range: 1 - 128." << std::endl;
        exit(0);
    }

    iarg++;
    int parallel = atoi(argv[iarg]);
    if (parallel < 1 || parallel > 10000) {
        std::cout << "valid parallel range: 1 - 10000." << std::endl;
        exit(0);
    }

    iarg++;
    size_t payload_size = atoi(argv[iarg]);
    if (payload_size < 1 || payload_size > 16 * 1024 * 1024) {
        std::cout << "valid payload size range: 1 byte - 16 MB." << std::endl;
        exit(0);
    }

    bench_config ret(srv_id,
                     my_endpoint,
                     duration,
                     complexity,
                     fault,
                     cluster_size,
                     parallel,
                     iops,
                     num_threads,
                     payload_size,
                     workdir);

    iarg++;
    for (int ii = iarg; ii < argc; ++ii) {
        std::string cur_endpoint = argv[ii];
        if (cur_endpoint.find("tcp://") == std::string::npos) {
            cur_endpoint = "tcp://" + cur_endpoint;
        }
        ret.endpoints_.push_back(cur_endpoint);
    }

    return ret;
}

}; // namespace raft_bench
using namespace raft_bench;

int main(int argc, char** argv) {
    TestSuite ts(argc, argv);

    bench_config config = parse_config(argc, argv);

    ts.options.printTestMessage = true;

    ts.doTest("bench main", bench_main, config);

    return 0;
}
