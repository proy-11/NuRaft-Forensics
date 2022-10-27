#include "commander.hxx"
#include "server_data_mgr.hxx"
#include "utils.hxx"
#include <boost/filesystem.hpp>
#include <chrono>
#include <csignal>
#include <sstream>

commander::commander(json data, std::shared_ptr<server_data_mgr> mgr)
    : setting(data)
    , server_mgr(mgr) {
    ns = setting["server"].size();
    server_waited = ns;
    maintain_connection();
    std::this_thread::sleep_for(std::chrono::milliseconds(setting["connection_wait_ms"]));
}

commander::~commander() {
    // raise(SIGUSR1);
    for (auto& conn: connections) {
        conn->join();
    }
    level_output(_LWARNING_, "destroyed commander cleanly\n");
}

void commander::deploy() {
    level_output(_LINFO_, "Checking initialization...\n");
    for (int i = 0; i < ns; i++) {
        std::thread thr(
            [this](int j) -> void {
                ssize_t sent = send_command(j, "check\n");
                if (sent < 0) {
                    level_output(_LERROR_, "Commander check init of #%d failed: %s\n", j, std::strerror(errno));
                    exit(errno);
                } else
                    return;
            },
            i);
        thr.detach();
    }
    {
        std::unique_lock<std::mutex> lock(mutex);
        cv_server.wait(lock, [this]() -> bool { return server_waited <= 0; });
        server_waited = ns - 1;
    }

    level_output(_LINFO_, "Adding servers...\n");
    for (int i = 1; i < ns; i++) {
        send_addpeer_command(i);
        std::this_thread::sleep_for(std::chrono::milliseconds(setting["add_server_gap_ms"]));
    }
    {
        std::unique_lock<std::mutex> lock(mutex);
        cv_server.wait(lock, [this]() -> bool { return server_waited <= 0; });
        server_waited = ns;
    }
}

ssize_t commander::send_command(int index, std::string cmd) {
    ssize_t sent;
    ssize_t p = 0, total = cmd.length();
    const char* msg = cmd.c_str();
    while (p < total) {
        sent = send(sockets[index], msg + p, total - p, MSG_NOSIGNAL);
        if (errno || sent < 0) return sent;
        p += sent;
    }
    return p;
}

void commander::start_experiment_timer() {
    std::thread thr([this]() -> void {
        std::this_thread::sleep_for(std::chrono::milliseconds(setting["exp_duration_ms"]));
        if (exit_mutex.try_lock()) {
            level_output(_LINFO_, "experiment terminated due to expiration\n");
            terminate(0);
        }
    });
    thr.detach();
}

void commander::terminate(int error) {
    exit_mutex.lock();
    server_mgr->terminate_all_req_mgrs();

    std::vector<std::thread> terminaters;
    for (int i = 0; i < ns; i++) {
        terminaters.emplace_back(
            [this](int i) -> void {
                for (int k = int(setting["exit_retries"]) - 1; k >= 0; k--) {
                    if (send_command(i, "exit\n") < 0) {
                        level_output(
                            _LERROR_, "Commander terminate #%d failed (%d left): %s. \n", i, k, std::strerror(errno));
                        std::this_thread::sleep_for(std::chrono::milliseconds(setting["exit_retry_ms"]));
                    } else {
                        return;
                    }
                }
            },
            i);
    }
    for (int i = 0; i < ns; i++) {
        terminaters[i].join();
    }

    if (error == 0 || error == SIGINT) {
        std::this_thread::sleep_for(std::chrono::milliseconds(setting["exit_cooldown_ms"]));

        char* table = status_table();
        if (table != NULL) {
            level_output(_LINFO_, "Status report:\n%s", table);
            delete[] table;
        }
    }
    exit(error);
}

void commander::send_addpeer_command(int j) {
    std::ostringstream oss;
    oss << "addpeer id=" << server_mgr->get_id(j) << " ep=" << server_mgr->get_endpoint_str(j) << "\n";
    if (send_command(0, oss.str()) < 0) {
        level_output(_LERROR_, "Commander add peer #%d failed: %s\n", j, std::strerror(errno));
        exit(errno);
    } else {
        return;
    }
}

void commander::maintain_connection() {
    for (int i = 0; i < ns; i++) {
        int sock;
        if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
            printf("\n Socket creation error \n");
            exit(1);
        }
        sockets.emplace_back(sock);
        client_fds.emplace_back(-1);
    }

    for (int i = 0; i < ns; i++) {
        std::shared_ptr<std::thread> thr = std::shared_ptr<std::thread>(new std::thread(
            [this](int i) -> void {
                bool final_result = false;
                while (!final_result) {
                    int cfd = connect(sockets[i], (sockaddr*)(server_mgr->get_endpoint(i).get()), sizeof(sockaddr));
                    if (cfd < 0) {
                        level_output(_LERROR_,
                                     "Commander connection to %d error: %s\n",
                                     server_mgr->get_id(i),
                                     std::strerror(errno));
                        std::this_thread::sleep_for(std::chrono::milliseconds(setting["reconnect_retry_ms"]));
                        continue;
                    }
                    client_fds[i] = cfd;

                    char buffer[BUF_SIZE] = {0};
                    std::string line;
                    while (true) {
                        ssize_t bytes_read = recv(sockets[i], buffer, BUF_SIZE, 0);
                        if (bytes_read < 0) {
                            if (!final_result)
                                level_output(_LERROR_,
                                             "<Server %2d> cmd recv got error %s\n",
                                             server_mgr->get_id(i),
                                             std::strerror(errno));
                            break;
                        }

                        int start = 0;
                        for (int i = 0; i < bytes_read; i++) {
                            if (buffer[i] == '\n') {
                                line += std::string(buffer + start, i - start);
                                if (!is_empty(line) && process_reply(line)) {
                                    final_result = true;
                                    break;
                                }
                                start = i + 1;
                                line.clear();
                            }
                        }

                        if (final_result) break;
                    }
                }
            },
            i));
        connections.emplace_back(thr);
    }
}

bool commander::process_reply(std::string reply) {
    level_output(_LDEBUG_, "cmd processing reply \"%s\"\n", reply.c_str());
    if (_ISSUBSTR_(reply, "init") || _ISSUBSTR_(reply, "added")) {
        std::unique_lock<std::mutex> lock(mutex);
        server_waited--;
        cv_server.notify_one();
        return false;
    } else if (_ISSUBSTR_(reply, "cannot add")) {
        int peer_id;
        int scanned = std::sscanf(reply.c_str(), "cannot add %d\n", &peer_id);
        if (scanned < 1) {
            level_output(_LERROR_, "commander cannot parse \"%s\"\n", reply.c_str());
            exit(1);
        }

        std::thread thr(
            [this](int peer_id) -> void {
                std::this_thread::sleep_for(std::chrono::milliseconds(setting["add_server_retry_ms"]));
                send_addpeer_command(server_mgr->get_index(peer_id));
            },
            peer_id);
        thr.detach();
        return false;
    } else {
        try {
            json obj = json::parse(reply);
            if (obj["success"]) {
                mutex.lock();
                replica_status_dict[std::to_string(int(obj["id"]))] = obj;
                mutex.unlock();
                return true;
            }
            return false;
        } catch (const json::parse_error& error) {
            level_output(_LERROR_,
                         "commander cannot parse \"%s\" (%x, %llu)\n",
                         reply.c_str(),
                         *((int*)(unsigned char*)&reply[0]),
                         reply.length());
            return false;
        }
    }
}

void commander::show_exp_duration() {
    uint64_t duration_total = (now_()) - (time_start);
    uint64_t duration_min = duration_total / 60000000000;
    duration_total -= duration_min * (60000000000);
    uint64_t duration_s = duration_total / 1000000000;
    duration_total -= duration_s * 1000000000;
    uint64_t duration_ms = duration_total / 1000000;

    level_output(_LINFO_, "experiment lasted %02llu:%02llu.%03llu\n", duration_min, duration_s, duration_ms);
}

char* commander::status_table() {
    if (replica_status_dict.empty()) {
        return NULL;
    }
    char* result = new char[10000];
    const char* fmt = "%6d %8d %8d %8d %14d %8d\n";
    const char* fmt_header = "%6s %8s %8s %8s %14s %8s\n";

    size_t pos = 0;
    pos += std::sprintf(result, fmt_header, "id", "term", "T", "J", "J(committed)", "n_req");
    for (auto& pair: replica_status_dict.items()) {
        int id = std::stoi(pair.key());
        json obj = pair.value();

        pos += std::sprintf(result + pos,
                            fmt,
                            id,
                            int(obj["term"]),
                            int(obj["log_term"]),
                            int(obj["log_height"]),
                            int(obj["log_height_committed"]),
                            int(obj["num_committed"]));
    }

    boost::filesystem::path report = setting["working_dir"];
    report /= "server_report.json";
    FILE* fp = std::fopen(report.c_str(), "w");
    if (errno != 0) {
        std::fprintf(stderr, "Error: Cannot open file %s\n", report.c_str());
        exit(1);
    }
    std::fprintf(fp, replica_status_dict.dump(4).c_str());
    std::fclose(fp);

    return result;
}
