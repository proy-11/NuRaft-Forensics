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
#include "libnuraft/json.hpp"
#include "logger_wrapper.hxx"

#include "nuraft.hxx"
#include "test_common.h"
#include "utils.hxx"

#include <boost/asio.hpp>
#include <boost/program_options.hpp>

#include <atomic>
#include <chrono>
#include <iostream>
#include <mutex>
#include <sstream>
#include <sys/wait.h>
#include <thread>

#define _ISSUBSTR_(s1, s2) ((s1).find(s2) != std::string::npos)
#define _ISNPOS_(p) ((p) == std::string::npos)

using namespace nuraft;

namespace po = boost::program_options;
namespace asio = boost::asio;

using json = nlohmann::json;
using boost::asio::ip::tcp;

int _PROG_LEVEL_ = _LINFO_;

namespace d_raft_server {

static const raft_params::return_method_type CALL_TYPE = raft_params::blocking;
//  = raft_params::async_handler;

using raft_result = cmd_result<ptr<buffer>>;

std::mutex service_mutex;
std::mutex addpeer_mutex;
std::mutex write_mutex;

// std::atomic_int committed_req_index(-1);

std::unordered_set<int> committed_reqs;

struct cmargs {
    cmargs(int id_, std::string ipaddr_, int port_, int cport_, std::string byzantine_) {
        this->id = id_;
        this->ipaddr = ipaddr_;
        this->port = port_;
        this->cport = cport_;
        this->byzantine = byzantine_;
    }

    int id;
    std::string ipaddr;
    int port;
    int cport;
    std::string byzantine;
};

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

size_t sync_write(tcp::socket* psock, const asio::const_buffers_1& buf) {
    size_t res = 0;
    write_mutex.lock();
    try {
        res = asio::write(*psock, buf);
    } catch (boost::system::system_error& error) {
        std::cerr << error.what();
    }
    write_mutex.unlock();
    return res;
}

std::string readline(tcp::socket* psock) {
    std::string message = "";
    char buf[1] = {'k'};
    for (; buf[0] != '\n';) {
        asio::read(*psock, asio::buffer(buf, 1));
        message += buf[0];
    }
    return message;
}

bool add_server(int peer_id, std::string endpoint_to_add) {
    if (!peer_id || peer_id == stuff.server_id_) {
        std::cout << "wrong server id: " << peer_id << std::endl;
        return false;
    }

    addpeer_mutex.lock();
    std::cout << "adding server " << peer_id << "...\n";
    srv_config srv_conf_to_add(peer_id, endpoint_to_add);
    ptr<raft_result> ret = stuff.raft_instance_->add_srv(srv_conf_to_add);
    addpeer_mutex.unlock();

    if (!ret->get_accepted()) {
        std::cout << "failed to add server: " << ret->get_result_code() << std::endl;
        return false;
    }
    std::cout << "async request is in progress (check with `list` command)" << std::endl;
    return true;
}

void server_list() {
    std::vector<ptr<srv_config>> configs;
    stuff.raft_instance_->get_srv_config_all(configs);

    int leader_id = stuff.raft_instance_->get_leader();

    for (auto& entry: configs) {
        ptr<srv_config>& srv = entry;
        std::cout << "server id " << srv->get_id() << ": " << srv->get_endpoint();
        if (srv->get_id() == leader_id) {
            std::cout << " (LEADER)";
        }
        std::cout << std::endl;
    }
}

// bool do_cmd(const std::vector<std::string>& tokens);
void handle_message(tcp::socket* psock, std::string request);
void handle_session(tcp::socket* psock);

void loop() {
    // char cmd[1000];
    // std::string prompt = "calc " + std::to_string(stuff.server_id_) + "> ";

    asio::io_service io_service;
    tcp::acceptor acceptor_(io_service, tcp::endpoint(tcp::v4(), stuff.cport_));
    while (true) {
        tcp::socket* psocket_ = new tcp::socket(io_service);
        acceptor_.accept(*psocket_);
        std::thread thr(handle_session, psocket_);
        thr.detach();
    }
}

void init_raft(ptr<state_machine> sm_instance) {
    // Logger.
    std::string log_file_name = "./srv" + std::to_string(stuff.server_id_) + ".log";
    ptr<logger_wrapper> log_wrap = cs_new<logger_wrapper>(log_file_name, 4);
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

void set_server_info(cmargs& args) {
    // Get server ID.
    stuff.server_id_ = args.id;
    if (stuff.server_id_ < 1) {
        std::cerr << "wrong server id (should be >= 1): " << stuff.server_id_ << std::endl;
    }

    // Get server address and port.
    stuff.port_ = args.port;
    stuff.cport_ = args.cport;
    if (stuff.port_ < 1000) {
        std::cerr << "wrong port (should be >= 1000): " << stuff.port_ << std::endl;
    }
    if (stuff.cport_ < 1000) {
        std::cerr << "wrong cport (should be >= 1000): " << stuff.cport_ << std::endl;
    }

    stuff.addr_ = args.ipaddr;
    stuff.endpoint_ = stuff.addr_ + ":" + std::to_string(stuff.port_);
}

d_raft_state_machine* get_sm() { return static_cast<d_raft_state_machine*>(stuff.sm_.get()); }

void handle_result(ptr<TestSuite::Timer> timer, raft_result& result, ptr<std::exception>& err) {
    if (result.get_result_code() != cmd_result_code::OK) {
        // Something went wrong.
        // This means committing this log failed,
        // but the log itself is still in the log store.
        std::cout << "failed: " << result.get_result_code() << ", " << TestSuite::usToString(timer->getTimeUs())
                  << std::endl;
        return;
    }
    ptr<buffer> buf = result.get();
    uint64_t ret_value = buf->get_ulong();
    std::cout << "succeeded, " << TestSuite::usToString(timer->getTimeUs()) << ", return value: " << ret_value
              << ", state machine value: " << get_sm()->get_current_value() << std::endl;
}

void reply_check_init(tcp::socket* psock, std::string& request) { sync_write(psock, asio::buffer("init\n")); }

void add_peer(tcp::socket* psock, std::string& request) {
    const char *ID_PREFIX = "id=", *EP_PREFIX = "ep=";
    size_t idpos = request.find(ID_PREFIX);
    size_t eppos = request.find(EP_PREFIX);

    if (_ISNPOS_(idpos) || _ISNPOS_(eppos)) {
        std::cerr << "cannot find keywords" << std::endl;
        exit(1);
    }

    idpos += std::strlen(ID_PREFIX);
    eppos += std::strlen(EP_PREFIX);

    size_t delim;
    for (delim = idpos; delim < request.length() && request[delim] != ' ' && request[delim] != '\n'; delim++) {
    }
    if (delim >= request.length()) {
        std::cerr << "request format wrong: " << request << std::endl;
        exit(1);
    }
    int id = std::stoi(request.substr(idpos, delim));

    for (delim = eppos; delim < request.length() && request[delim] != ' ' && request[delim] != '\n'; delim++) {
    }
    if (delim >= request.length()) {
        std::cerr << "request format wrong: " << request << std::endl;
        exit(1);
    }
    std::string endpoint = request.substr(eppos, delim);

    // std::cout << "got id = " << id << ", endpoint = " << endpoint << "\n";
    bool add_result = add_server(id, endpoint);
    if (add_result) {
        sync_write(psock, asio::buffer(std::string("added ") + std::to_string(id) + "\n"));
    } else {
        sync_write(psock, asio::buffer(std::string("cannot add ") + std::to_string(id) + "\n"));
    }
}

void replicate_request(tcp::socket* psock, std::string request) {
    int rid;
    try {
        json req_obj = json::parse(request);
        rid = req_obj["index"];
    } catch (json::exception& ec) {
        json reply = {{"success", false}, {"error", ec.what()}};
        sync_write(psock, asio::buffer(reply.dump() + "\n"));
        return;
    }

    service_mutex.lock();

    if (committed_reqs.find(rid) != committed_reqs.end()) {
        service_mutex.unlock();
        json reply = {{"success", false}, {"error", "request already committed"}};
        sync_write(psock, asio::buffer(reply.dump() + "\n"));
        return;
    }

    ptr<TestSuite::Timer> timer = cs_new<TestSuite::Timer>();

    // std::this_thread::sleep_for(std::chrono::milliseconds(100));
    ptr<buffer> new_log = buffer::alloc(sizeof(int));
    int payload = 24;
    buffer_serializer bs(new_log);
    bs.put_raw(&payload, sizeof(int));

    ptr<raft_result> ret = stuff.raft_instance_->append_entries({new_log});

    if (!ret->get_accepted()) {
        cmd_result_code rc = ret->get_result_code();
        std::cout << "failed to replicate: " << rc << ", " << TestSuite::usToString(timer->getTimeUs()) << std::endl;
        service_mutex.unlock();
        json obj = {{"req", rid}, {"success", false}, {"ec", rc}, {"error", ret->get_result_str()}};
        if (rc == cmd_result_code::NOT_LEADER) {
            obj["leader"] = stuff.raft_instance_->get_leader();
        }
        sync_write(psock, asio::buffer(obj.dump() + "\n"));
        return;
    }

    ptr<std::exception> err(nullptr);
    handle_result(timer, *ret, err);
    int top_index = stuff.raft_instance_->get_last_log_idx();
    int top_term = stuff.raft_instance_->get_last_log_term();
    service_mutex.unlock();

    json obj = {{"req", rid}, {"success", true}, {"index", top_index}, {"term", top_term}};
    committed_reqs.insert(rid);
    sync_write(psock, asio::buffer(obj.dump() + "\n"));
    return;
}

void handle_message(tcp::socket* psock, std::string request) {
    if (_ISSUBSTR_(request, "check")) {
        reply_check_init(psock, request);
    } else if (_ISSUBSTR_(request, "addpeer")) {
        add_peer(psock, request);
    } else if (_ISSUBSTR_(request, "exit")) {
        int log_height = stuff.raft_instance_->get_last_log_idx();
        int log_term = stuff.raft_instance_->get_last_log_term();
        int term = stuff.raft_instance_->get_term();
        int clog_height = stuff.raft_instance_->get_committed_log_idx();
        json obj = {{"success", true},
                    {"log_height", log_height},
                    {"log_height_committed", clog_height},
                    {"log_term", log_term},
                    {"term", term}};
        sync_write(psock, asio::buffer(obj.dump() + "\n"));
        std::cout << "terminating -- info:\n" << obj.dump() << std::endl;
        exit(0);
    } else {
        replicate_request(psock, request);
    }
}

void handle_session(tcp::socket* psock) {
    try {
        for (;;) {
            std::string message = readline(psock);
            std::printf("Got message %s\n", strip_endl(message).c_str());

            std::thread thr(handle_message, psock, message);
            thr.detach();
        }
    } catch (boost::wrapexcept<boost::system::system_error>&) {
        std::cerr << "client disconnected!" << std::endl;
        delete psock;
        return;
    }
}

cmargs parse_args(int argc, char** argv) {
    po::options_description desc("Allowed options");
    desc.add_options()("help", "produce help message")("id", po::value<int>(), "server id")(
        "ip", po::value<std::string>(), "IP address")("port", po::value<int>(), "port number")(
        "cport", po::value<int>(), "Client port number")("byz", po::value<std::string>(), "Byzantine status");

    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);

    if (vm.count("help")) {
        std::cout << desc << "\n";
        exit(0);
    }

    int id, port, cport;
    std::string ipaddr, byzantine;

    if (vm.count("id")) {
        id = vm["id"].as<int>();
    } else {
        std::cout << "Server ID was not set.\n";
        exit(1);
    }
    if (vm.count("port")) {
        port = vm["port"].as<int>();
    } else {
        std::cout << "Port was not set.\n";
        exit(1);
    }
    if (vm.count("cport")) {
        cport = vm["cport"].as<int>();
    } else {
        std::cout << "Client port was not set.\n";
        exit(1);
    }
    if (vm.count("ip")) {
        ipaddr = vm["ip"].as<std::string>();
    } else {
        std::cout << "IP address not set.\n";
        exit(1);
    }
    if (vm.count("byz")) {
        byzantine = vm["byz"].as<std::string>();
    } else {
        std::cout << "Byzantine status not set.\n";
        exit(1);
    }

    return cmargs(id, ipaddr, port, cport, byzantine);
}

}; // namespace d_raft_server
using namespace d_raft_server;

int main(int argc, char** argv) {
    // TODO - Read config file path from cmd line
    cmargs args = parse_args(argc, argv);
    // service_mutex.unlock();

    // if (argc < 3) usage(argc, argv);

    set_server_info(args);

    std::cout << "    -- Replicated Calculator with Raft --" << std::endl;
    std::cout << "                         Version 0.1.0" << std::endl;
    std::cout << "    Server ID:    " << stuff.server_id_ << std::endl;
    std::cout << "    Endpoint:     " << stuff.endpoint_ << std::endl;
    init_raft(cs_new<d_raft_state_machine>());
    loop();

    return 0;
}
