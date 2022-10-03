#include "libnuraft/json.hpp"
#include "nuraft.hxx"
#include "workload.hxx"
#include <atomic>
#include <boost/asio.hpp>
#include <boost/program_options.hpp>
#include <chrono>
#include <csignal>
#include <cstdarg>
#include <cstdio>
#include <format>
#include <fstream>
#include <iostream>
#include <mutex>
#include <signal.h>
#include <sstream>
#include <thread>
#include <unistd.h>

#define _ISSUBSTR_(s1, s2) ((s1).find(s2) != std::string::npos)

namespace po = boost::program_options;
namespace asio = boost::asio;
using boost::asio::ip::tcp;
using json = nlohmann::json;

std::mutex print_mutex;
std::mutex ending_mutex;

const std::string INIT_ASK = "check\n";
const std::string NEW_SERVER = "addpeer id=%d ep=%s\n";
const std::string EXIT_COMMAND = "exit\n";

std::vector<int> ids(0);
std::vector<std::thread> server_ends(0);
std::vector<tcp::socket*> psockets(0);
std::vector<std::string> endpoints(0);
std::atomic_int current_raft_leader(4);
json replica_status_dict;

enum _levels_ { _LERROR_ = 0, _LWARNING_ = 1, _LINFO_ = 2, _LDEBUG_ = 3 };

int _PROG_LEVEL_ = _LINFO_;

void end_srv(int i, bool recv);

void level_output(_levels_ level, const char* fmt, ...) {
    if (level > _PROG_LEVEL_) return;

    print_mutex.lock();
    va_list args;
    va_start(args, fmt);
    switch (level) {
    case _levels_::_LERROR_:
        std::vfprintf(stderr, (std::string("\033[1m\033[31m[ERROR] ") + fmt + "\033[0m").c_str(), args);
        break;
    case _levels_::_LWARNING_:
        std::vfprintf(stderr, (std::string("\033[1m\033[33m[WARN ] ") + fmt + "\033[0m").c_str(), args);
        break;
    case _levels_::_LINFO_:
        std::vfprintf(stderr, (std::string("\033[32m[INFO ] ") + fmt + "\033[0m").c_str(), args);
        break;
    case _levels_::_LDEBUG_:
        std::vfprintf(stderr, (std::string("[DEBUG] ") + fmt + "\033[0m").c_str(), args);
        break;
    default:
        break;
    }
    va_end(args);
    print_mutex.unlock();
}

int get_leader_index(int id) {
    for (int i = 0; i < ids.size(); i++) {
        if (ids[i] == id) return i;
    }
    return -1;
}

std::string strip_endl(std::string str) {
    size_t len = str.length();
    if (str[len - 1] == '\n') {
        return str.substr(0, len - 1);
    } else {
        return str;
    }
}

std::string endpoint_wrapper(std::string ip, int port) { return ip + ":" + std::to_string(port); }

std::string readline(tcp::socket* psock, boost::system::error_code& error) {
    std::string message = "";
    char buf[1] = {'k'};
    for (; buf[0] != '\n';) {
        psock->read_some(boost::asio::buffer(buf), error);
        if (buf[0] != '\0') message += buf[0];
    }
    return message;
}

void create_server(int id, std::string ip, int server_port, int client_port, std::string byz) {
    char cmd[1024];
    std::snprintf(cmd,
                  sizeof(cmd),
                  "client/d_raft --id %d --ip %s --port %d "
                  "--cport %d --byz %s 1> server_%d.log 2> err_server_%d.log",
                  id,
                  ip.c_str(),
                  server_port,
                  client_port,
                  byz.c_str(),
                  id,
                  id);
    pid_t pid = fork();

    if (pid == 0) {
        int status = std::system(cmd);
        std::stringstream msg;
        if (status < 0) {
            level_output(_LERROR_, "%s\n", strerror(errno));
            exit(errno);
        } else {
            if (WIFEXITED(status)) {
                level_output(
                    _LDEBUG_, "<Server %d> Program returned normally, exit code %d\n", id, WEXITSTATUS(status));
            } else {
                level_output(_LERROR_, "<Server %d> Program returned abnormally\n", id);
            }
            exit(status);
        }
    } else {
        return;
    }
}

std::string send_(std::string msg, int i, bool recv) {
    boost::system::error_code error;
    boost::asio::streambuf receive_buffer;
    std::stringstream ostream;

    boost::asio::write(*psockets[i], boost::asio::buffer(msg), error);
    if (error) {
        level_output(_LERROR_, "<Server %d> send failed (%s)", ids[i], error.message().c_str());
        return std::string("error_send\n");
    }

    if (!recv) {
        return std::string("\n");
    }

    std::string buf_str = readline(psockets[i], error);

    if (error && error != boost::asio::error::eof) {
        level_output(_LERROR_, "<Server %d> receive failed (%s)\n", ids[i], error.message().c_str());
        buf_str = std::string("error_recv\n");
    } else {
        level_output(_LDEBUG_, "<Server %d> %s\n", ids[i], strip_endl(buf_str).c_str());
    }

    return buf_str;
}

void send_(std::string msg, int i) { send_(msg, i, false); }

void ask_init(int i) { send_(INIT_ASK, i, true); }

bool try_add_server(int i, int ir) {
    char c_msg[1024];
    std::snprintf(c_msg, sizeof(c_msg), NEW_SERVER.c_str(), ids[ir], endpoints[ir].c_str());
    std::string result = send_(c_msg, i, true);

    if (result == "error_send\n" || result == "error_recv\n") {
        level_output(_LERROR_, "<Server %d> add %d failed (send/recv). Terminating raft...\n", ids[i], ids[ir]);

        for (int i = 0; i < psockets.size(); i++) {
            end_srv(i, false);
        }

        exit(1);
    } else if (_ISSUBSTR_(result, "added")) {
        return true;
    } else {
        return false;
    }
}

void add_server(int i, int ir) {
    while (!try_add_server(i, ir)) {
        level_output(_LERROR_, "<Server %d> add %d failed, trying again...\n", ids[i], ids[ir]);
        std::this_thread::sleep_for(std::chrono::microseconds(1000));
    }
}

void end_srv(int i, bool recv) {
    std::string status = send_(EXIT_COMMAND, i, recv);

    if (recv) {
        ending_mutex.lock();
        json obj = json::parse(status);
        if (obj["success"]) replica_status_dict[std::to_string(ids[i])] = obj;
        ending_mutex.unlock();
    }
}

char* status_table() {
    if (replica_status_dict.empty()) {
        return NULL;
    }
    char* result = new char[1000];
    const char* fmt = "%6d %8d %8d %8d %14d\n";
    const char* fmt_header = "%6s %8s %8s %8s %14s\n";

    size_t pos = 0;
    pos += std::sprintf(result, fmt_header, "id", "term", "T", "J", "J(committed)");
    for (auto& pair: replica_status_dict.items()) {
        int id = std::stoi(pair.key());
        json obj = pair.value();

        pos += std::sprintf(result + pos,
                            fmt,
                            id,
                            int(obj["term"]),
                            int(obj["log_term"]),
                            int(obj["log_height"]),
                            int(obj["log_height_committed"]));
    }

    return result;
}

void submit_request(nuraft::request& req) {
    json obj = {{"index", req.index}, {"payload", req.payload}};
    for (;;) {
        int lid = current_raft_leader;
        std::string result = send_(obj.dump() + "\n", lid, true);
        json result_obj = json::parse(result);
        if (result_obj["success"]) break;

        level_output(
            _LERROR_, "<Server %d> request #%d failed (%s)\n", ids[lid], req.index, result_obj["error"].dump().c_str());

        if (!result_obj.contains("ec")) {
            break;
        }

        // Handle error
        nuraft::cmd_result_code ec = static_cast<nuraft::cmd_result_code>(result_obj["ec"]);
        if (ec == nuraft::cmd_result_code::NOT_LEADER) {
            if (!result_obj.contains("leader")) {
                level_output(_LERROR_,
                             "<Server %d> request #%d: Got a NOT_LEADER error but no leader is reported. Given up.\n",
                             ids[lid],
                             req.index);
                break;
            } else {
                int leader_id = result_obj["leader"];
                if (leader_id > 0) {
                    int leader_index = get_leader_index(leader_id);
                    if (leader_index < 0) {
                        level_output(
                            _LERROR_,
                            "<Server %d> request #%d: Got a NOT_LEADER error but leader id not found. Given up.\n",
                            ids[lid],
                            req.index);
                        break;
                    }
                    current_raft_leader = leader_index;
                }
                continue;
            }
        } else {
            break;
        }
    }
}

void experiment(std::string path) {
    nuraft::workload load(path);

    while (true) {
        int delay;
        nuraft::request req(0);
        std::tie(req, delay) = load.get_next_req_us();

        level_output(_LDEBUG_, "Sending req #%d, next delay = %d us\n", req.index, delay);
        if (req.index < 0) {
            break;
        }

        std::thread thread_(submit_request, std::ref(req));
        thread_.detach();
        std::this_thread::sleep_for(std::chrono::microseconds(delay));
    }
}

void create_client(int port, std::string ip, std::string path) {
    std::ostringstream cbuf;
    cbuf << "client/d_raft_client --port " << port << " --ip " << ip << " --path " << path;
    int status = std::system(cbuf.str().c_str());
    if (status < 0)
        std::cout << "Error: " << strerror(errno) << '\n';
    else {
        if (WIFEXITED(status))
            level_output(_LDEBUG_, "Program returned normally, exit code %d\n", WEXITSTATUS(status));
        else
            std::cout << "Program exited abnormally\n";
    }
}

void signal_handler(int signal) {
    level_output(_LWARNING_, "Terminating all servers...\n");

    for (int i = 0; i < psockets.size(); i++) {
        end_srv(i, false);
    }

    fflush(stdout);
    exit(0);
}

int main(int argc, const char** argv) {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGABRT, signal_handler);

    std::string config_file = "";
    if (argc == 2) {
        config_file = argv[1];
    } else if (argc == 3) {
        config_file = argv[1];
        _PROG_LEVEL_ = std::atoi(argv[2]);
    } else {
        std::cout << "Usage: ./d_raft_launcher config_file <PRINT_LEVEL>\n";
        return 1;
    }

    std::ifstream f(config_file);
    json data = json::parse(f);

    int number_of_servers = data["server"].size();
    // int number_of_clients = data["client"].size();

    if (number_of_servers < 1) {
        std::cerr << "Number of servers must be at least 1!" << std::endl;
        exit(1);
    }

    std::vector<std::thread> server_creators(0);
    std::vector<std::thread> server_inits(0);
    std::vector<std::thread> server_adds(0);
    boost::asio::io_service io_service;
    // std::vector<std::thread> clients(number_of_clients);

    level_output(_LINFO_, "Launching servers...\n");

    for (int i = 0; i < number_of_servers; i++) {
        server_creators.emplace_back(create_server,
                                     data["server"][i]["id"],
                                     data["server"][i]["ip"],
                                     data["server"][i]["port"],
                                     data["server"][i]["cport"],
                                     data["server"][i]["byzantine"]);
        ids.emplace_back(data["server"][i]["id"]);
        endpoints.emplace_back(endpoint_wrapper(data["server"][i]["ip"], data["server"][i]["port"]));
    }

    for (int i = 0; i < number_of_servers; i++) {
        server_creators[i].join();
    }

    level_output(_LINFO_, "Connecting...\n");

    std::this_thread::sleep_for(std::chrono::milliseconds(2500));

    for (int i = 0; i < number_of_servers; i++) {
        psockets.emplace_back(new tcp::socket(io_service));
        // psockets[i] = new tcp::socket(io_service);
        psockets[i]->connect(
            tcp::endpoint(boost::asio::ip::address::from_string(data["server"][i]["ip"]), data["server"][i]["cport"]));
    }

    level_output(_LINFO_, "Checking initialization...\n");

    for (int i = 0; i < number_of_servers; i++) {
        server_inits.emplace_back(ask_init, i);
    }

    for (int i = 0; i < number_of_servers; i++) {
        server_inits[i].join();
    }

    level_output(_LINFO_, "Adding servers...\n");

    for (int i = 1; i < number_of_servers; i++) {
        add_server(0, i);
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        // server_adds.emplace_back(add_server_as_peer, 0, i);
    }

    // for (int i = 0; i < number_of_servers - 1; i++) {
    //     server_adds[i].join();
    // }

    level_output(_LINFO_, "Launching client...\n");

    experiment(data["client"]["path"]);

    level_output(_LINFO_, "Closing servers...\n");

    for (int i = 0; i < number_of_servers; i++) {
        server_ends.emplace_back(end_srv, i, true);
    }

    for (int i = 0; i < number_of_servers; i++) {
        server_ends[i].join();
    }

    char* table = status_table();

    if (table != NULL) {
        level_output(_LINFO_, "Status report:\n%s", table);
        delete[] table;
    }

    return 0;
}
