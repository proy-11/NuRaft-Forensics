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
#include "test_common.h"

#include <atomic>
#include <boost/filesystem.hpp>
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

#define BUF_SIZE 65536

#define _ISSUBSTR_(s1, s2) ((s1).find(s2) != std::string::npos)
#define _ISNPOS_(p) ((p) == std::string::npos)

using namespace nuraft;

namespace po = boost::program_options;

using json = nlohmann::json;

namespace d_raft_server {

static const raft_params::return_method_type CALL_TYPE = raft_params::blocking;
//  = raft_params::async_handler;

using raft_result = cmd_result<ptr<buffer>>;

std::mutex service_mutex;
std::mutex addpeer_mutex;
std::unordered_map<int, std::shared_ptr<std::mutex>> write_mutex;
std::unordered_set<int> committed_reqs;

boost::filesystem::path datadir;

std::atomic<bool> exit_signal(false);

struct simple_job {
    simple_job(int sock_, std::string request_)
        : sock(sock_)
        , request(request_) {}
    int sock;
    std::string request;
};

void replicate_request(simple_job job);
void handle_message(int sock, std::string request);
void handle_session(int sock);

struct cmargs {
    cmargs(int id_, std::string ipaddr_, int port_, int cport_, std::string byzantine_, int workers_, int qlen_)
        : id(id_)
        , ipaddr(ipaddr_)
        , port(port_)
        , cport(cport_)
        , byzantine(byzantine_)
        , workers(workers_)
        , qlen(qlen_) {}

    int id;
    std::string ipaddr;
    int port;
    int cport;
    std::string byzantine;
    int workers;
    int qlen;
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

std::shared_ptr<job_queue<simple_job>> jobq;

inline bool is_empty(std::string str) {
    return std::string(str.c_str()) == "" || str.find_first_not_of(" \0\t\n\v\f\r") == str.npos;
}

std::string escape_quote(std::string str) {
    char* buf = new char[2 * str.length() + 1];
    if (buf == NULL) {
        return "";
    }

    size_t p = 0;
    for (const char c: str) {
        if (c == '"') {
            buf[p] = '\\';
            buf[p + 1] = '"';
            p += 2;
        } else {
            buf[p] = c;
            p += 1;
        }
    }
    buf[p] = '\0';
    std::string res(buf);
    delete[] buf;
    return res;
}

json write_result() {
    int log_height = stuff.raft_instance_->get_last_log_idx();
    int log_term = stuff.raft_instance_->get_last_log_term();
    int term = stuff.raft_instance_->get_term();
    int clog_height = stuff.raft_instance_->get_committed_log_idx();
    json obj = {{"id", stuff.server_id_},
                {"success", true},
                {"log_height", log_height},
                {"log_height_committed", clog_height},
                {"log_term", log_term},
                {"term", term}};

    try {
        std::ofstream file((datadir / (std::string("server_") + std::to_string(stuff.server_id_) + ".json")).c_str());
        file << std::setw(4) << obj;
        file.close();
    } catch (std::exception& ec) {
        std::fprintf(stderr, "cannot write result: %s\n", ec.what());
    }
    return obj;
}

void exit_signal_handler(int signal) {
    std::fprintf(stderr, "got signal %s (%d), exiting\n", strsignal(signal), signal);
    write_result();
    exit(0);
    // exit_signal = true;
}

ssize_t sync_write(int sock, std::string msg) {
    ssize_t sent;
    ssize_t p = 0, total = msg.length();
    const char* cmsg = msg.c_str();

    std::unique_lock<std::mutex> lock(*write_mutex[sock]);
    while (p < total) {
        sent = send(sock, cmsg + p, total - p, MSG_NOSIGNAL);
        if (errno || sent < 0) {
            std::fprintf(
                stderr, "Warning: <send> encountered errno %d (%s):\n%s\n", errno, std::strerror(errno), msg.c_str());
            return sent;
        }
        p += sent;
    }
    return p;
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

void loop() {
    int server_fd;
    int new_sock;
    int opt = 1;
    int addrlen = sizeof(sockaddr_in);
    sockaddr_in address;

    if (stuff.cport_ < 1000) {
        while (!exit_signal) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        return;
    }

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    // Forcefully attaching socket to the port 8080
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(stuff.cport_);

    if (bind(server_fd, (sockaddr*)&address, sizeof(address)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 3) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    while (exit_signal) {
        if ((new_sock = accept(server_fd, (sockaddr*)&address, (socklen_t*)&addrlen)) < 0) {
            perror("accept");
            exit(errno);
        }
        write_mutex[new_sock] = std::shared_ptr<std::mutex>(new std::mutex());
        std::thread thr(handle_session, new_sock);
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
        exit(1);
    }
    if (stuff.cport_ < 1000) {
        std::cerr << "warning: inactive cport (should be >= 1000): " << stuff.cport_ << std::endl;
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

void reply_check_init(int sock) { sync_write(sock, "init\n"); }

void add_peer(int sock, std::string& request) {
    int id;
    char ep[50] = {0};
    int scanned = std::sscanf(request.c_str(), "addpeer id=%d ep=%s", &id, ep);

    if (scanned != 2) {
        std::cerr << "request format wrong: " << request << std::endl;
        exit(1);
    }

    if (add_server(id, ep)) {
        sync_write(sock, std::string("added ") + std::to_string(id) + "\n");
    } else {
        sync_write(sock, std::string("cannot add ") + std::to_string(id) + "\n");
    }
}

void replicate_request(simple_job job) {
    int rid;

    debug_print("Replicating req %s\n", job.request.c_str());

    try {
        json req_obj = json::parse(job.request);
        rid = req_obj["index"];
    } catch (json::exception& ec) {
        std::string errmsg = "{\"success\": false, \"error\": \"";
        errmsg += escape_quote(job.request);
        errmsg += "\"}\n";
        sync_write(job.sock, errmsg);
        debug_print("req %s format error\n", job.request.c_str());
        return;
    }

    debug_print("req %s in service\n", job.request.c_str());
    service_mutex.lock();

    if (committed_reqs.find(rid) != committed_reqs.end()) {
        service_mutex.unlock();
        json reply = {{"rid", rid}, {"success", false}, {"error", "request already committed"}};
        sync_write(job.sock, reply.dump() + "\n");
        debug_print("req %s already committed\n", job.request.c_str());
        return;
    }

    debug_print("req %s appending\n", job.request.c_str());

    ptr<TestSuite::Timer> timer = cs_new<TestSuite::Timer>();

    // std::this_thread::sleep_for(std::chrono::milliseconds(100));
    ptr<buffer> new_log = buffer::alloc(sizeof(int));
    int payload = 24;
    buffer_serializer bs(new_log);
    bs.put_raw(&payload, sizeof(int));

    ptr<raft_result> ret = stuff.raft_instance_->append_entries({new_log});

    debug_print("req %s finished appending\n", job.request.c_str());

    if (!ret->get_accepted()) {
        cmd_result_code rc = ret->get_result_code();
        std::cout << "failed to replicate: " << rc << ", " << TestSuite::usToString(timer->getTimeUs()) << std::endl;
        service_mutex.unlock();
        json obj = {{"rid", rid}, {"success", false}, {"ec", rc}, {"error", ret->get_result_str()}};
        if (rc == cmd_result_code::NOT_LEADER) {
            obj["leader"] = stuff.raft_instance_->get_leader();
        }
        sync_write(job.sock, obj.dump() + "\n");
        debug_print("req %s not accepted, reason: %s\n", job.request.c_str(), ret->get_result_str().c_str());
        return;
    }

    debug_print("req %s accepted\n", job.request.c_str());

    ptr<std::exception> err(nullptr);
    handle_result(timer, *ret, err);
    int top_index = stuff.raft_instance_->get_last_log_idx();
    int top_term = stuff.raft_instance_->get_last_log_term();
    service_mutex.unlock();

    json obj = {{"rid", rid}, {"success", true}, {"index", top_index}, {"term", top_term}};
    committed_reqs.insert(rid);
    sync_write(job.sock, obj.dump() + "\n");

    debug_print("req %s committed\n", job.request.c_str());
    return;
}

void handle_message(int sock, std::string request) {
    if (_ISSUBSTR_(request, "check")) {
        reply_check_init(sock);
    } else if (_ISSUBSTR_(request, "addpeer")) {
        add_peer(sock, request);
    } else if (_ISSUBSTR_(request, "exit")) {
        json obj = write_result();
        sync_write(sock, obj.dump() + "\n");
        std::cout << "terminating -- info:\n" << obj.dump() << std::endl;
        exit(0);
    } else {
        if (!jobq->enque(simple_job(sock, request))) {
            int rid;
            try {
                json req_obj = json::parse(request);
                rid = req_obj["index"];
            } catch (json::exception& ec) {
                std::string errmsg = "{\"success\": false, \"error\": \"";
                errmsg += escape_quote(request);
                errmsg += "\"}\n";
                sync_write(sock, errmsg);
                return;
            }
            json reply = {{"rid", rid}, {"success", false}, {"error", "queue is full"}};
            sync_write(sock, reply.dump() + "\n");
        }
    }
}

void handle_session(int sock) {
    std::string line;
    char* buffer = new char[BUF_SIZE];
    while (true) {
        ssize_t bytes_read = recv(sock, buffer, BUF_SIZE, 0);
        if (bytes_read < 0) {
            std::cerr << "recv failed with error " << std::strerror(errno) << std::endl;
            delete[] buffer;
            return;
        }

        int start = 0;
        for (int i = 0; i < bytes_read; i++) {
            if (buffer[i] == '\n') {
                line += std::string(buffer + start, i - start);
                if (!is_empty(line)) {
                    std::cout << "Got message ~~ " << line << " ~~" << std::endl;
                    handle_message(sock, line);
                }
                start = i + 1;
                line.clear();
            }
        }
        if (start != bytes_read) {
            line += std::string(buffer + start, bytes_read - start);
        }
    }
}

cmargs parse_args(int argc, char** argv) {
    po::options_description desc("Allowed options");
    desc.add_options()("help", "produce help message")("id", po::value<int>(), "server id")(
        "ip", po::value<std::string>(), "IP address")("port", po::value<int>(), "port number")(
        "cport", po::value<int>(), "Client port number")("byz", po::value<std::string>(), "Byzantine status")(
        "workers", po::value<int>(), "number of threads at max")("qlen", po::value<int>(), "max queue length")(
        "datadir",
        po::value<std::string>()->default_value(boost::filesystem::current_path().string()),
        "data directory");

    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);

    if (vm.count("help")) {
        std::cout << desc << "\n";
        exit(0);
    }

    int id, port, cport, workers, qlen;
    std::string ipaddr, byzantine;

    if (vm.count("id")) {
        id = vm["id"].as<int>();
    } else {
        std::cerr << "Server ID was not set.\n";
        exit(1);
    }
    if (vm.count("port")) {
        port = vm["port"].as<int>();
    } else {
        std::cerr << "Port was not set.\n";
        exit(1);
    }
    if (vm.count("cport")) {
        cport = vm["cport"].as<int>();
    } else {
        std::cerr << "Client port was not set.\n";
        exit(1);
    }
    if (vm.count("ip")) {
        ipaddr = vm["ip"].as<std::string>();
    } else {
        std::cerr << "IP address not set.\n";
        exit(1);
    }
    if (vm.count("byz")) {
        byzantine = vm["byz"].as<std::string>();
    } else {
        std::cerr << "Byzantine status not set.\n";
        exit(1);
    }
    if (vm.count("workers")) {
        workers = vm["workers"].as<int>();
    } else {
        std::cerr << "max thread count was not set.\n";
        exit(1);
    }
    if (vm.count("qlen")) {
        qlen = vm["qlen"].as<int>();
    } else {
        std::cerr << "max queue lenght was not set.\n";
        exit(1);
    }

    if (vm.count("datadir")) {
        datadir = vm["datadir"].as<std::string>();
    }
    std::cerr << "datadir set to " << boost::filesystem::absolute(datadir) << "\n";

    return cmargs(id, ipaddr, port, cport, byzantine, workers, qlen);
}
}; // namespace d_raft_server
using namespace d_raft_server;

int main(int argc, char** argv) {
    cmargs args = parse_args(argc, argv);

    set_server_info(args);
    signal(SIGINT, exit_signal_handler);

    std::cout << "    -- Replicated Calculator with Raft --" << std::endl;
    std::cout << "                         Version 0.1.0" << std::endl;
    std::cout << "    Server ID:    " << stuff.server_id_ << std::endl;
    std::cout << "    Endpoint:     " << stuff.endpoint_ << std::endl;
    init_raft(cs_new<d_raft_state_machine>());

    jobq =
        std::shared_ptr<job_queue<simple_job>>(new job_queue<simple_job>(replicate_request, args.workers, args.qlen));
    jobq->process_jobs();
    loop();

    return 0;
}
