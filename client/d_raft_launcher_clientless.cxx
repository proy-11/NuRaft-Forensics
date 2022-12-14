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

#include "d_raft_state_machine.hxx"
#include "in_memory_state_mgr.hxx"
#include "job_queue.hxx"
#include "libnuraft/json.hpp"
#include "logger_wrapper.hxx"
#include "nuraft.hxx"
#include "sync_file_obj.hxx"
#include "test_common.h"
#include "utils.hxx"
#include "workload.hxx"

#include <atomic>
#include <boost/program_options.hpp>
#include <chrono>
#include <csignal>
#include <iostream>
#include <map>
#include <mutex>
#include <netinet/in.h>
#include <sstream>
#include <sys/socket.h>
#include <sys/wait.h>
#include <thread>

#define _ISSUBSTR_(s1, s2) ((s1).find(s2) != std::string::npos)
#define _ISNPOS_(p) ((p) == std::string::npos)

using namespace nuraft;

namespace po = boost::program_options;
namespace fsys = boost::filesystem;

using json = nlohmann::json;

json meta_setting, workload_setting;

std::shared_ptr<sync_file_obj> arrive = nullptr;
std::shared_ptr<sync_file_obj> depart = nullptr;
std::vector<pid_t> server_pids;

pid_t monitor;
fsys::path datadir;
fsys::path keypath;

int _PROG_LEVEL_ = _LINFO_;
int _SRV_LOG_LEVEL_ = 4;
int total_rejected = 0;
int total_enqueued = 0;

std::atomic<bool> ended(false);

namespace d_raft_server {

static const raft_params::return_method_type CALL_TYPE = raft_params::blocking;
//  = raft_params::async_handler;

using raft_result = cmd_result<ptr<buffer>>;

std::mutex service_mutex;
std::mutex addpeer_mutex;
std::mutex exit_mutex;

void replicate_request(request req);
void server_list();

inline std::string endpoint_wrapper(std::string ip, int port) { return ip + ":" + std::to_string(port); }

struct server_stuff {
    server_stuff()
        : server_id_(1)
        , addr_("localhost")
        , port_(25000)
        , cport_(23333)
        , raft_logger_(nullptr)
        , sm_(nullptr)
        , smgr_(nullptr)
        , raft_instance_(nullptr) {}

    void reset() {
        raft_logger_.reset();
        sm_.reset();
        smgr_.reset();
        raft_instance_.reset();
    }

    // Server ID.
    int server_id_;

    // Server address.
    std::string addr_;

    // Server port.
    int port_;

    int cport_;

    // Endpoint: `<addr>:<port>`.
    std::string endpoint_;

    // Logger.
    ptr<logger> raft_logger_;

    // State machine.
    ptr<state_machine> sm_;

    // State manager.
    ptr<state_mgr> smgr_;

    // Raft launcher.
    raft_launcher launcher_;

    // Raft server instance.
    ptr<raft_server> raft_instance_;
};
static server_stuff stuff;

std::shared_ptr<job_queue<request>> jobq;

json write_result() {
    int log_height = stuff.raft_instance_->get_last_log_idx();
    int log_term = stuff.raft_instance_->get_last_log_term();
    int term = stuff.raft_instance_->get_term();
    int clog_height = stuff.raft_instance_->get_committed_log_idx();
    json obj = {{"id", stuff.server_id_},
                {"log_height", log_height},
                {"log_height_committed", clog_height},
                {"log_term", log_term},
                {"term", term}};

    level_output(_LINFO_,
                 "Enqueued %d, Rejected %d, J = %d, J(C'ed) = %d, T = %d, term = %d\n",
                 total_enqueued,
                 total_rejected,
                 log_height,
                 clog_height,
                 log_term,
                 term);

    try {
        std::ofstream file((datadir / (std::string("server_") + std::to_string(stuff.server_id_) + ".json")).c_str());
        file << std::setw(4) << obj;
        file.close();
    } catch (std::exception& ec) {
        level_output(_LERROR_, "cannot write result: %s\n", ec.what());
    }
    return obj;
}

void exit_handler(int sig) {
    // std::unique_lock<std::mutex> lock(exit_mutex);
    exit_mutex.lock();
    ended = true;
    server_list();

    for (auto& pid: server_pids) {
        kill(pid, SIGINT);
        level_output(_LINFO_, "interrupted process %d\n", pid);
    }

    kill(monitor, SIGINT);
    write_result();

    stuff.raft_instance_->shutdown();
    stuff.launcher_.shutdown();

    level_output(_LINFO_, "exiting\n");
    exit(0);
}

bool add_server(int peer_id, std::string endpoint_to_add) {
    if (!peer_id || peer_id == stuff.server_id_) {
        level_output(_LERROR_, "wrong server id: %d\n", peer_id);
        return false;
    }

    std::unique_lock<std::mutex> lock(addpeer_mutex);
    srv_config srv_conf_to_add(peer_id, endpoint_to_add);
    ptr<raft_result> ret = stuff.raft_instance_->add_srv(srv_conf_to_add);

    if (!ret->get_accepted()) {
        level_output(_LERROR_, "failed to add server %d: %s\n", peer_id, ret->get_result_str().c_str());
        return false;
    }
    return true;
}

void server_list() {
    std::vector<ptr<srv_config>> configs;
    stuff.raft_instance_->get_srv_config_all(configs);

    int leader_id = stuff.raft_instance_->get_leader();
    level_output(_LINFO_, "%4s %24s\n", "ID", "endpoint");

    for (auto& entry: configs) {
        ptr<srv_config>& srv = entry;
        std::string fmt("%4d %24s");
        fmt += srv->get_id() == leader_id ? " (LEADER)\n" : "\n";
        level_output(_LINFO_, fmt.c_str(), srv->get_id(), srv->get_endpoint().c_str());
    }
}

void loop() {
    nuraft::workload load(workload_setting);
    int total_batches = 0;
    auto time_start = now_();

    while (true) {
        int delay = load.get_next_batch_delay_us();
        auto requests = load.get_next_batch();

        if (!load.proceed_batch()) {
            break;
        }

        for (auto& req: requests) {
            if (!jobq->enque(req)) {
                level_output(_LWARNING_, "req %d rejected by full queue\n", req.index);
                total_rejected++;
            } else {
                total_enqueued++;
            }
        }

        total_batches++;
        if (total_batches % 10 == 0) {
            uint64_t duration_total = now_() - time_start;
            uint64_t duration_min = duration_total / 60000000000;
            duration_total -= duration_min * (60000000000);
            uint64_t duration_s = duration_total / 1000000000;
            duration_total -= duration_s * 1000000000;
            uint64_t duration_ms = duration_total / 1000000;

            level_output(_LINFO_,
                         "%02llu:%02llu.%03llu submitted batch %d\n",
                         duration_min,
                         duration_s,
                         duration_ms,
                         total_batches);
        }

        std::this_thread::sleep_for(std::chrono::microseconds(delay));
    }

    exit_handler(0);
}

void init_raft(ptr<state_machine> sm_instance) {
    // Logger.

    std::string log_file_name = "srv" + std::to_string(stuff.server_id_) + ".log";
    std::string log_file_path = (datadir / log_file_name).string();
    ptr<logger_wrapper> log_wrap = cs_new<logger_wrapper>(log_file_path, _SRV_LOG_LEVEL_);
    stuff.raft_logger_ = log_wrap;

    // State machine.
    stuff.smgr_ = cs_new<inmem_state_mgr>(stuff.server_id_, stuff.endpoint_);
    // State manager.
    stuff.sm_ = sm_instance;

    // ASIO options.
    asio_service::options asio_opt;
    asio_opt.thread_pool_size_ = 4;

    // Raft parameters.
    raft_params params;
#if defined(WIN32) || defined(_WIN32)
    // heartbeat: 1 sec, election timeout: 2 - 4 sec.
    params.heart_beat_interval_ = 1000;
    params.election_timeout_lower_bound_ = 2000;
    params.election_timeout_upper_bound_ = 4000;
#else
    // heartbeat: 100 ms, election timeout: 200 - 400 ms.
    params.heart_beat_interval_ = 100;
    params.election_timeout_lower_bound_ = 200;
    params.election_timeout_upper_bound_ = 400;
#endif
    // Upto 5 logs will be preserved ahead the last snapshot.
    params.reserved_log_items_ = 5;
    // Snapshot will be created for every 5 log appends.
    params.snapshot_distance_ = 5;
    // Client timeout: 3000 ms.
    params.client_req_timeout_ = 3000;
    // According to this method, `append_log` function
    // should be handled differently.
    params.return_method_ = CALL_TYPE;

    params.private_key_path = keypath.string();

    // Initialize Raft server.
    stuff.raft_instance_ =
        stuff.launcher_.init(stuff.sm_, stuff.smgr_, stuff.raft_logger_, stuff.port_, asio_opt, params);
    if (!stuff.raft_instance_) {
        std::cerr << "Failed to initialize launcher (see the message "
                     "in the log file)."
                  << std::endl;
        log_wrap.reset();
        exit(-1);
    }

    // Wait until Raft server is ready (upto 5 seconds).
    const size_t MAX_TRY = 20;
    std::cout << "init Raft instance ";
    for (size_t ii = 0; ii < MAX_TRY; ++ii) {
        if (stuff.raft_instance_->is_initialized()) {
            std::cout << " done" << std::endl;
            return;
        }
        std::cout << ".";
        fflush(stdout);
        TestSuite::sleep_ms(250);
    }
    std::cout << " FAILED" << std::endl;
    log_wrap.reset();
    exit(-1);
}

void set_server_info(json server_setting) {
    // Get server ID.
    stuff.server_id_ = server_setting["id"];
    if (stuff.server_id_ < 1) {
        std::cerr << "wrong server id (should be >= 1): " << stuff.server_id_ << std::endl;
    }

    // Get server address and port.
    stuff.port_ = server_setting["port"];
    stuff.cport_ = -1;
    if (stuff.port_ < 1000) {
        std::cerr << "wrong port (should be >= 1000): " << stuff.port_ << std::endl;
        exit(1);
    }

    stuff.addr_ = server_setting["ip"];
    stuff.endpoint_ = stuff.addr_ + ":" + std::to_string(stuff.port_);
}

d_raft_state_machine* get_sm() { return static_cast<d_raft_state_machine*>(stuff.sm_.get()); }

void handle_result(ptr<TestSuite::Timer> timer, raft_result& result, ptr<std::exception>& err) {
    if (result.get_result_code() != cmd_result_code::OK) {
        // Something went wrong.
        // This means committing this log failed,
        // but the log itself is still in the log store.
        level_output(_LDEBUG_,
                     "failed: %s, %s\n",
                     result.get_result_str().c_str(),
                     TestSuite::usToString(timer->getTimeUs()).c_str());
        return;
    }
    ptr<buffer> buf = result.get();
    uint64_t ret_value = buf->get_ulong();
    level_output(
        _LDEBUG_, "succeeded, %s, return value: %llu\n", TestSuite::usToString(timer->getTimeUs()).c_str(), ret_value);
}

void replicate_request(request req) {
    if (ended) return;

    std::string req_json_str = req.to_json_str();
    const char* req_desc = req_json_str.c_str();

    std::unique_lock<std::mutex> lock(service_mutex);

    ptr<TestSuite::Timer> timer = cs_new<TestSuite::Timer>();
    ptr<buffer> new_log = buffer::alloc(sizeof(int) + req.payload.size() + 1);
    buffer_serializer bs(new_log);
    bs.put_raw(&req.index, sizeof(int));
    bs.put_cstr(req.payload.c_str());

    ptr<raft_result> ret = stuff.raft_instance_->append_entries({new_log});

    if (ended) return;

    if (!ret->get_accepted()) {
        level_output(_LWARNING_,
                     "failed to replicate (%s), %s",
                     ret->get_result_str().c_str(),
                     TestSuite::usToString(timer->getTimeUs()).c_str());
        debug_print("req %s not accepted, reason: %s\n", req_desc, ret->get_result_str().c_str());
        return;
    }

    debug_print("req %s accepted\n", req_desc);

    ptr<std::exception> err(nullptr);
    handle_result(timer, *ret, err);
    return;
}

void parse_args(int argc, char** argv) {
    std::string config_file("");

    po::options_description desc("Allowed options");
    desc.add_options()("help",
                       "produce help message")("config-file", po::value<std::string>()->required(), "config file path")(
        "size", po::value<int>(), "total size")("freq", po::value<float>(), "frequency")(
        "batch-size", po::value<int>(), "batch size")("loglv", po::value<int>()->default_value(4), "server log level")(
        "log-level", po::value<int>()->default_value(2), "print log level")(
        "datadir", po::value<std::string>(), "directory to save results")(
        "keypath", po::value<std::string>(), "path to private key");

    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);

    if (vm.count("help")) {
        std::cout << _C_BOLDYELLOW_ << desc << "\n";
        exit(0);
    }

    if (vm.count("config-file")) {
        config_file = vm["config-file"].as<std::string>();
    } else {
        level_output(_LERROR_, "config file not set, aborting\n");
        exit(1);
    }

    if (vm.count("log-level")) {
        _PROG_LEVEL_ = vm["log-level"].as<int>();
    }

    try {
        std::ifstream f(config_file);
        meta_setting = json::parse(f);
        datadir = std::string(meta_setting["working_dir"]);
    } catch (json::exception& je) {
        level_output(_LERROR_, "Error reading meta settings: %s\n", je.what());
        exit(1);
    }

    try {
        json client_setting = meta_setting["client"];
        std::ifstream file(std::string(client_setting.at("path")).c_str());
        workload_setting = json::parse(file);
    } catch (json::exception& je) {
        level_output(_LERROR_, "Error reading workload settings: %s\n", je.what());
        exit(1);
    }

    if (vm.count("size")) {
        workload_setting["size"] = vm["size"].as<int>();
    }

    if (vm.count("freq")) {
        float frequency = vm["freq"].as<float>();
        std::stringstream ss;
        ss << std::setprecision(4) << frequency;
        workload_setting["freq"] = std::stof(ss.str());
    }

    if (vm.count("batch-size")) {
        workload_setting["batch_size"] = vm["batch-size"].as<int>();
    }

    if (vm.count("datadir")) {
        datadir = vm["datadir"].as<std::string>();
    }

    if (vm.count("keypath")) {
        keypath = vm["keypath"].as<std::string>();
    }

    _SRV_LOG_LEVEL_ = vm["loglv"].as<int>();

    if (fsys::exists(datadir) && !fsys::is_directory(datadir)) {
        level_output(_LERROR_, "Cannot create directory %s\n", fsys::absolute(datadir).c_str());
        exit(1);
    }

    if (fsys::exists(datadir)) {
        fsys::remove_all(fsys::absolute(datadir));
    }
    fsys::create_directories(fsys::absolute(datadir));

    level_output(_LINFO_,
                 "Using workload settings\nsize      = %10d,\nfreq      = "
                 "%10.4f,\nbatchsize = %10d\n",
                 int(workload_setting.at("size")),
                 float(workload_setting.at("freq")),
                 int(workload_setting.at("batch_size")));
}

}; // namespace d_raft_server
using namespace d_raft_server;

int main(int argc, char** argv) {
    signal(SIGINT, exit_handler);
    signal(SIGSTOP, exit_handler);

    parse_args(argc, argv);

    json server_settings = meta_setting["server"];
    json this_setting = server_settings[0];
    int ns = server_settings.size();

    set_server_info(this_setting);

    for (int i = 1; i < ns; i++) {
        server_settings[i]["cport"] = -1;
        server_settings[i]["datadir"] = datadir.string();
        server_settings[i]["loglv"] = _SRV_LOG_LEVEL_;
        server_pids.emplace_back(create_server(server_settings[i], datadir));
    }

    monitor = create_cpu_monitor(datadir);

    level_output(_LINFO_, "    -- Clientless Benchmarker for Raft --\n");
    level_output(_LINFO_, "    Server ID:    %d\n", stuff.server_id_);
    level_output(_LINFO_, "    Endpoint:     %s\n", stuff.endpoint_.c_str());

    init_raft(cs_new<d_raft_state_machine>());

    std::this_thread::sleep_for(std::chrono::milliseconds(meta_setting["connection_wait_ms"]));

    for (int i = 1; i < ns; i++) {
        if (!add_server(int(server_settings[i]["id"]),
                        endpoint_wrapper(server_settings[i]["ip"], int(server_settings[i]["port"])))) {
            exit(1);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(meta_setting["add_server_gap_ms"]));
    }

    server_list();

    std::thread([] {
        std::this_thread::sleep_for(std::chrono::milliseconds(meta_setting["exp_duration_ms"]));
        if (!ended) {
            level_output(_LINFO_, "experiment terminated due to expiration\n");
            exit_handler(0);
        }
    }).detach();

    jobq = std::shared_ptr<job_queue<request>>(
        new job_queue<request>(replicate_request, this_setting["workers"], this_setting["qlen"]));
    jobq->process_jobs();

    loop();

    return 0;
}
