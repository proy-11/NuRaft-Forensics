#define CONSOLE

#include "d_raft_scheduler.hxx"
#include "libnuraft/json.hpp"
#include "nuraft.hxx"
#include "utils.hxx"
#include "workload.hxx"
#include <atomic>
#include <boost/asio.hpp>
#include <boost/program_options.hpp>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <thread>

namespace po = boost::program_options;
namespace asio = boost::asio;
namespace fsys = std::filesystem;

using boost::asio::ip::tcp;
using nlohmann::json;
using std::string;

std::mutex table_mutex;
std::mutex timeout_mutex;
std::mutex log_mutex;
asio::io_service io_service;

const string INIT_ASK = "check\n";
const string NEW_SERVER = "addpeer id=%d ep=%s\n";
const string EXIT_COMMAND = "exit\n";
const string ERROR_CONN = "error_conn\n";
const int MAX_NUMBER_OF_JOBS = 1000;

vector<int> ids(0);
vector<std::thread*> request_submissions(0);
vector<tcp::socket*> sockets_ctrl(0);
vector<std::mutex> mutex_ctrl(0);
vector<tcp::endpoint> endpoints(0);
vector<string> endpoints_str(0);
std::atomic_int current_raft_leader(4);
std::atomic_bool exp_ended(false);
json replica_status_dict;
json meta_setting;

FILE* depart = nullptr;
FILE* arrive = nullptr;

uint64_t time_start;

int _PROG_LEVEL_ = _LINFO_;

void end_srv(int i, bool recv);

int get_leader_index(int id) {
    for (size_t i = 0; i < ids.size(); i++) {
        if (ids[i] == id) return i;
    }
    return -1;
}

void sync_writeline(FILE* fp, string line) {
    log_mutex.lock();
    std::fprintf(fp, "%s\n", line.c_str());
    log_mutex.unlock();
}

string readline(tcp::socket* psock, boost::system::error_code& error) {
    string message = "";
    char buf[1] = {'k'};
    for (; buf[0] != '\n';) {
        psock->read_some(asio::buffer(buf), error);
        if (buf[0] != '\0') message += buf[0];
    }
    return message;
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

void create_server(int id, string ip, int server_port, int client_port, string byz) {
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
        if (status < 0) {
            level_output(_LERROR_, "%s\n", strerror(errno));
            exit(errno);
        } else {
            if (WIFEXITED(status)) {
                level_output(
                    _LINFO_, "<Server %2d> Program returned normally, exit code %d\n", id, WEXITSTATUS(status));
            } else {
                level_output(_LERROR_, "<Server %2d> Program returned abnormally\n", id);
            }
            exit(status);
        }
    } else {
        return;
    }
}

string send_(string msg, int i, bool recv, bool timeout) {
    boost::system::error_code error;
    asio::streambuf receive_buffer;
    std::stringstream ostream;

    tcp::socket sock(io_service);
    string buf_str;

    try {
        sock.connect(endpoints[i]);
        // asio::write(*psockets[i], asio::buffer(msg), error);
        asio::write(sock, asio::buffer(msg), error);
        if (error) {
            level_output(_LERROR_, "<Server %2d> send failed (%s)", ids[i], error.message().c_str());
            return ERROR_CONN;
        }

        if (!recv) {
            // sock.close();
            return string("\n");
        }

        buf_str = readline(&sock, error);
        sock.close();
    } catch (boost::system::system_error& error) {
        level_output(_LWARNING_, "<Server %2d> %s\n", ids[i], error.what());
        sock.close();
        return ERROR_CONN;
    }

    if (error && error != asio::error::eof) {
        level_output(_LERROR_, "<Server %2d> receive failed (%s)\n", ids[i], error.message().c_str());
        return ERROR_CONN;
    } else {
        level_output(_LDEBUG_, "<Server %2d> %s\n", ids[i], strip_endl(buf_str).c_str());
        return buf_str;
    }
}

string send_(string msg, int i, bool recv) { return send_(msg, i, recv, false); }

void send_(string msg, int i) { send_(msg, i, false); }

void ask_init(int i) { send_(INIT_ASK, i, true); }

bool try_add_server(int i, int ir) {
    char c_msg[1024];
    std::snprintf(c_msg, sizeof(c_msg), NEW_SERVER.c_str(), ids[ir], endpoints_str[ir].c_str());
    string result = send_(c_msg, i, true);

    if (result == ERROR_CONN) {
        level_output(_LERROR_, "<Server %2d> add %d failed (send/recv). Terminating raft...\n", ids[i], ids[ir]);

        for (size_t i = 0; i < ids.size(); i++) {
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
        level_output(_LERROR_, "<Server %2d> add %d failed, trying again...\n", ids[i], ids[ir]);
        std::this_thread::sleep_for(std::chrono::milliseconds(meta_setting["add_server_retry_ms"]));
    }
}

void end_srv(int i, bool recv) {
    string status = send_(EXIT_COMMAND, i, recv);

    if (recv) {
        table_mutex.lock();
        try {
            json obj = json::parse(status);
            if (obj["success"]) replica_status_dict[std::to_string(ids[i])] = obj;
        } catch (json::parse_error& pe) {
            level_output(_LERROR_, "<Server %2d> got invalid response \"%s\"\n", ids[i], status.c_str());
        }
        table_mutex.unlock();
    }
}

void submit_request(nuraft::request req) {
    json obj = {{"index", req.index}, {"payload", req.payload}};
    while (!exp_ended) {
        uint64_t now_ns = now_();
        int lid = current_raft_leader;

        sync_writeline(depart, json({{"index", req.index}, {"time", now_ns}}).dump());

        string result = send_(obj.dump() + "\n", lid, true);
        if (result == ERROR_CONN) {
            level_output(_LERROR_, "<Server %2d> request #%d: Connection error, retrying\n", ids[lid], req.index);
            std::this_thread::sleep_for(std::chrono::milliseconds(meta_setting["missing_leader_retry_ms"]));
            continue;
        } else if (exp_ended) {
            return;
        }

        json result_obj;
        try {
            result_obj = json::parse(result);
        } catch (json::parse_error& error) {
            level_output(_LERROR_,
                         "<Server %2d> request #%d: got invalid response \"%s\", retrying\n",
                         ids[lid],
                         req.index,
                         result.c_str());
            continue;
        }

        if (result_obj["success"]) {
            sync_writeline(arrive, json({{"index", req.index}, {"time", now_()}}).dump());
            return;
        }

        level_output(_LERROR_,
                     "<Server %2d> request #%d failed (%s)\n",
                     ids[lid],
                     req.index,
                     result_obj["error"].dump().c_str());

        if (!result_obj.contains("ec")) {
            return;
        }

        // Handle error
        nuraft::cmd_result_code ec = static_cast<nuraft::cmd_result_code>(result_obj["ec"]);
        if (ec == nuraft::cmd_result_code::NOT_LEADER) {
            if (!result_obj.contains("leader")) {
                level_output(_LERROR_,
                             "<Server %2d> request #%d: Got a NOT_LEADER error but no leader is reported. Given up.\n",
                             ids[lid],
                             req.index);
                return;
            } else {
                int leader_id = result_obj["leader"];
                if (leader_id > 0) {
                    auto itr = std::find(ids.begin(), ids.end(), leader_id);
                    if (itr == ids.cend()) {
                        level_output(
                            _LERROR_,
                            "<Server %2d> request #%d: Got a NOT_LEADER error but leader id not found. Given up.\n",
                            ids[lid],
                            req.index);
                        return;
                    } else {
                        int leader_index = std::distance(ids.begin(), itr);
                        if (current_raft_leader != leader_index) {
                            level_output(_LWARNING_, "leader %d -> %d\n", ids[current_raft_leader], ids[leader_index]);
                            current_raft_leader = leader_index;
                        }
                    }
                } else {
                    std::this_thread::sleep_for(std::chrono::milliseconds(meta_setting["missing_leader_retry_ms"]));
                }
                continue;
            }
        } else {
            return;
        }
    }
}

std::mutex submit_req_mutex;
void submit_batched_request(vector<nuraft::request> reqs) {
    submit_req_mutex.lock();
    // level_output(_LDEBUG_, "Sending req #%d, = %llu ns\n", reqs.index, now_());
    uint64_t now_ns = now_();
    // string serialized_reqs = nuraft::to_jsonl(reqs);
    bool first = true;

    while (!exp_ended) {
        // int lid = current_raft_leader;

        if (!first) now_ns = now_();
        sync_writeline(
            depart, json({{"index_start", reqs[0].index}, {"index_end", reqs.back().index}, {"time", now_ns}}).dump());

        string result = ERROR_CONN;//send_(serialized_reqs, lid, true);
        if (result == ERROR_CONN) {
            // level_output(_LERROR_, "<Server %2d> request #%d: Connection error, retrying\n", ids[lid], req.index);
            std::this_thread::sleep_for(std::chrono::milliseconds(meta_setting["missing_leader_retry_ms"]));
            continue;
        } else if (exp_ended) {
            return;
        }

        json result_obj;
        try {
            result_obj = json::parse(result);
        } catch (json::parse_error& error) {
            // level_output(_LERROR_,
            //              "<Server %2d> request #%d: got invalid response \"%s\", retrying\n",
            //              ids[lid],
            //              req.index,
            //              result.c_str());
            continue;
        }

        if (result_obj["success"]) {
            // sync_writeline(arrive, json({{"index", req.index}, {"time", now_()}}).dump());
            return;
        }

        // level_output(_LERROR_,
        //              "<Server %2d> request #%d failed (%s)\n",
        //              ids[lid],
        //              req.index,
        //              result_obj["error"].dump().c_str());

        if (!result_obj.contains("ec")) {
            return;
        }

        // Handle error
        nuraft::cmd_result_code ec = static_cast<nuraft::cmd_result_code>(result_obj["ec"]);
        if (ec == nuraft::cmd_result_code::NOT_LEADER) {
            if (!result_obj.contains("leader")) {
                // level_output(_LERROR_,
                //              "<Server %2d> request #%d: Got a NOT_LEADER error but no leader is reported. Given up.\n",
                //              ids[lid],
                //              req.index);
                return;
            } else {
                int leader_id = result_obj["leader"];
                if (leader_id > 0) {
                    auto itr = std::find(ids.begin(), ids.end(), leader_id);
                    if (itr == ids.cend()) {
                        // level_output(
                        //     _LERROR_,
                        //     "<Server %2d> request #%d: Got a NOT_LEADER error but leader id not found. Given up.\n",
                        //     ids[lid],
                        //     req.index);
                        return;
                    } else {
                        int leader_index = std::distance(ids.begin(), itr);
                        if (current_raft_leader != leader_index) {
                            level_output(_LWARNING_, "leader %d -> %d\n", ids[current_raft_leader], ids[leader_index]);
                            current_raft_leader = leader_index;
                        }
                    }
                } else {
                    std::this_thread::sleep_for(std::chrono::milliseconds(meta_setting["missing_leader_retry_ms"]));
                }
                continue;
            }
        } else {
            return;
        }
    }
    submit_req_mutex.unlock();
}
void experiment_timeout() {
    std::this_thread::sleep_for(std::chrono::milliseconds(meta_setting["exp_duration_ms"]));
    if (!exp_ended) {
        level_output(_LWARNING_, "experiment terminated due to expiration\n");
        std::raise(SIGUSR1);
    }
}

void send_timeout(int i, std::string message) {
    std::this_thread::sleep_for(std::chrono::milliseconds(meta_setting["send_timeout"]));
    if (!exp_ended) {
        level_output(_LWARNING_, "<Server %2d> message send timeout: %s\n", ids[i], strip_endl(message).c_str());
    }
}

void wait_for_threads(vector<std::thread*> threads) {
    for (size_t i = 0; i < threads.size(); i++) {
        threads[i]->join();
    }
    std::raise(SIGUSR1);
}

void experiment(string path) {
    nuraft::workload load(path);

    timeout_mutex.lock();

    std::thread timeout_thread(experiment_timeout);
    timeout_thread.detach();

    // d_raft_scheduler::Scheduler scheduler(
    //     MAX_NUMBER_OF_JOBS, [](const std::exception& e) { level_output(_LERROR_, "Error: %s", e.what()); });

    while (!exp_ended) {
        vector<nuraft::request> batch = load.get_next_batch();
        int delay = load.get_next_batch_delay_us();

        if (batch.size() == 0) {
            level_output(_LINFO_, "All requests sent\n");
            break;
        } else {
            level_output(
                _LDEBUG_, "Sending req #%d - #%d, next delay = %d us\n", batch[0].index, batch.back().index, delay);
        }
        std::thread* pthread = new std::thread(submit_batched_request, batch);
        request_submissions.emplace_back(pthread);
        std::this_thread::sleep_for(std::chrono::microseconds(delay));
        
        // scheduler.add_task_to_queue(req);
        // scheduler.schedule(submit_request);
        // auto interval = std::chrono::system_clock::now() + std::chrono::microseconds(delay);
        // std::this_thread::sleep_until(interval);


    for (std::thread* pthread: request_submissions) {
        if (pthread != nullptr) {
            pthread->join();
            delete pthread;
        }
    }

    std::raise(SIGUSR1);
    // std::thread thread_(wait_for_threads, request_submissions);
    // thread_.detach();

    // for (std::thread* pthread: request_submissions) {
    //     delete pthread;
    // }
    }
}

void show_exp_duration() {
    uint64_t duration_total = (now_()) - (time_start);
    uint64_t duration_min = duration_total / 60000000000;
    duration_total -= duration_min * (60000000000);
    uint64_t duration_s = duration_total / 1000000000;
    duration_total -= duration_s * 1000000000;
    uint64_t duration_ms = duration_total / 1000000;

    level_output(_LINFO_, "experiment lasted %02llu:%02llu.%03llu\n", duration_min, duration_s, duration_ms);
}

void create_client(int port, string ip, string path) {
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
    level_output(_LWARNING_, "got signal %d, terminating all servers...\n", signal);

    for (size_t i = 0; i < ids.size(); i++) {
        end_srv(i, false);
    }

    fflush(stdout);
    std::fclose(arrive);
    std::fclose(depart);
    exit(signal);
}

void end_properly(int signal) {
    if (exp_ended) {
        return;
    }
    exp_ended = true;
    show_exp_duration();

    level_output(_LINFO_, "Closing servers...\n");

    vector<std::thread> server_ends(0);
    for (size_t i = 0; i < ids.size(); i++) {
        server_ends.emplace_back(end_srv, i, true);
    }

    for (size_t i = 0; i < ids.size(); i++) {
        server_ends[i].join();
    }

    char* table = status_table();

    if (table != NULL) {
        level_output(_LINFO_, "Status report:\n%s", table);
        delete[] table;
    }

    std::fclose(arrive);
    std::fclose(depart);
    exit(0);
}

int main(int argc, const char** argv) {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGABRT, signal_handler);
    std::signal(SIGUSR1, end_properly);

    string config_file = "";
    if (argc == 2) {
        config_file = argv[1];
    } else if (argc == 3) {
        config_file = argv[1];
        _PROG_LEVEL_ = std::atoi(argv[2]);
    } else {
        std::cout << _C_BOLDYELLOW_ << "Usage: ./d_raft_launcher config_file <PRINT_LEVEL>\n";
        return 1;
    }

    std::ifstream f(config_file);
    meta_setting = json::parse(f);

    int number_of_servers = meta_setting["server"].size();
    // int number_of_clients = data["client"].size();

    if (number_of_servers < 1) {
        std::cerr << _C_BOLDRED_ << "Error: number of servers must be at least 1!" << std::endl;
        exit(1);
    }

    fsys::path working_dir = meta_setting["working_dir"];
    if (!fsys::exists(working_dir)) {
        fsys::create_directories(fsys::absolute(working_dir));
    } else if (!fsys::is_directory(working_dir)) {
        level_output(_LERROR_, "Cannot create directory %s\n", fsys::absolute(working_dir).c_str());
        exit(1);
    }

    depart = std::fopen((working_dir / "depart.jsonl").c_str(), "w");
    arrive = std::fopen((working_dir / "arrive.jsonl").c_str(), "w");

    vector<std::thread> server_creators(0);
    vector<std::thread> server_inits(0);
    vector<std::thread> server_adds(0);
    // std::vector<std::thread> clients(number_of_clients);

    level_output(_LINFO_, "Launching servers...\n");

    for (int i = 0; i < number_of_servers; i++) {
        server_creators.emplace_back(create_server,
                                     meta_setting["server"][i]["id"],
                                     meta_setting["server"][i]["ip"],
                                     meta_setting["server"][i]["port"],
                                     meta_setting["server"][i]["cport"],
                                     meta_setting["server"][i]["byzantine"]);
        ids.emplace_back(meta_setting["server"][i]["id"]);
        endpoints_str.emplace_back(
            endpoint_wrapper(meta_setting["server"][i]["ip"], meta_setting["server"][i]["port"]));
    }

    for (int i = 0; i < number_of_servers; i++) {
        server_creators[i].join();
    }

    level_output(_LINFO_, "Waiting %d ms for connection...\n", int(meta_setting["connection_wait_ms"]));

    std::this_thread::sleep_for(std::chrono::milliseconds(meta_setting["connection_wait_ms"]));

    for (int i = 0; i < number_of_servers; i++) {
        endpoints.emplace_back(tcp::endpoint(asio::ip::address::from_string(meta_setting["server"][i]["ip"]),
                                             meta_setting["server"][i]["cport"]));
        sockets_ctrl.emplace_back(new tcp::socket(io_service));
        sockets_ctrl[i]->connect(endpoints[i]);
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
        std::this_thread::sleep_for(std::chrono::milliseconds(meta_setting["add_server_gap_ms"]));

        // server_adds.emplace_back(add_server_as_peer, 0, i);
    }

    // for (int i = 0; i < number_of_servers - 1; i++) {
    //     server_adds[i].join();
    // }
    level_output(_LINFO_, "Waiting %d ms for launching client...\n", int(meta_setting["request_wait_ms"]));
    std::this_thread::sleep_for(std::chrono::milliseconds(meta_setting["request_wait_ms"]));
    level_output(_LINFO_, "Launching client...\n");

    time_start = now_();
    experiment(meta_setting["client"]["path"]);

    return 0;
}
