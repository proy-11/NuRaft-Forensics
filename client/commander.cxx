#include "commander.hxx"
#include "server_data_mgr.hxx"
#include "utils.hxx"
#include <chrono>
#include <sstream>
#include <thread>

commander::commander(json data, server_data_mgr* mgr)
    : setting(data)
    , server_mgr(mgr) {
    ns = setting["server"].size();
    init_latch = new std::latch(ns);
    peer_latch = new std::latch(ns - 1);
    maintain_connection();
    std::this_thread::sleep_for(std::chrono::milliseconds(setting["connection_wait_ms"]));
}

commander::~commander() {
    delete init_latch;
    delete peer_latch;
    for (auto psock: sockets) {
        if (psock) delete psock;
    }
}

void commander::deploy() {
    level_output(_LINFO_, "Checking initialization...\n");
    for (int i = 0; i < ns; i++) {
        std::thread thr(
            [this](int j) -> void {
                boost::system::error_code ec;
                send_command(j, "check\n", ec);
                if (ec) {
                    level_output(_LERROR_, "Commander check init of #%d failed: %s\n", j, ec.message().c_str());
                    exit(1);
                } else
                    return;
            },
            i);
        thr.detach();
    }

    init_latch->wait();

    level_output(_LINFO_, "Adding servers...\n");
    for (int i = 1; i < ns; i++) {
        send_addpeer_command(i);
        std::this_thread::sleep_for(std::chrono::milliseconds(setting["add_server_gap_ms"]));
    }
    peer_latch->wait();
}

void commander::send_command(int index, std::string cmd, boost::system::error_code& ec) {
    sockets[index]->write_some(asio::buffer(cmd, cmd.length()), ec);
}

void commander::start_experiment_timer() {
    std::thread thr([this]() -> void {
        std::this_thread::sleep_for(std::chrono::milliseconds(setting["exp_duration_ms"]));
        if (exit_mutex.try_lock()) {
            level_output(_LWARNING_, "experiment terminated due to expiration\n");
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
                boost::system::error_code ec;
                for (int k = int(setting["exit_retries"]) - 1; k >= 0; k--) {
                    send_command(i, "exit\n", ec);
                    if (ec) {
                        level_output(
                            _LERROR_, "Commander terminate #%d failed (%d left): %s.  \n", i, k, ec.message().c_str());
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
    boost::system::error_code ec;
    std::ostringstream oss;
    oss << "addpeer id=" << server_mgr->get_id(j) << " ep=" << server_mgr->get_endpoint_str(j) << "\n";
    send_command(0, oss.str(), ec);
    if (ec) {
        level_output(_LERROR_, "Commander add peer #%d failed: %s\n", j, ec.message().c_str());
        exit(1);
    } else
        return;
}

void commander::maintain_connection() {
    asio::io_service ios;

    for (int i = 0; i < ns; i++) {
        sockets.emplace_back(new tcp::socket(ios));
        std::thread thr(
            [this](int i) -> void {
                while (true) {
                    boost::system::error_code ec;
                    sockets[i]->connect(server_mgr->get_endpoint(i), ec);
                    if (ec) {
                        level_output(_LERROR_, "Commander connection to #%d error: %s\n", i, ec.message().c_str());
                        std::this_thread::sleep_for(std::chrono::milliseconds(setting["reconnect_retry_ms"]));
                        continue;
                    }

                    level_output(_LINFO_, "connected server %d\n", server_mgr->get_id(i));
                    while (true) {
                        asio::streambuf sbuf;
                        asio::read_until(*sockets[i], sbuf, "\n", ec);
                        if (ec) {
                            level_output(_LERROR_,
                                         "<Server %2d> Cmd got error %s\n",
                                         server_mgr->get_id(i),
                                         ec.message().c_str());
                            break;
                        }
                        auto data = sbuf.data();
                        std::istringstream iss(
                            std::string(asio::buffers_begin(data), asio::buffers_begin(data) + data.size() - 1));
                        std::string line;
                        level_output(
                            _LDEBUG_, "commander got lines \"%s\" (%llu)\n", iss.str().c_str(), iss.str().length());
                        while (std::getline(iss, line)) {
                            if (is_empty(line)) {
                                continue;
                            }
                            process_reply(line);
                        }
                    }
                }
            },
            i);
        thr.detach();
    }
}

void commander::process_reply(std::string reply) {
    if (_ISSUBSTR_(reply, "init")) {
        init_latch->count_down();
    } else if (_ISSUBSTR_(reply, "added")) {
        peer_latch->count_down();
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
    } else {
        try {
            json obj = json::parse(reply);
            if (obj["success"]) {
                mutex.lock();
                replica_status_dict[std::to_string(int(obj["id"]))] = obj;
                mutex.unlock();
            }
        } catch (const json::parse_error& error) {
            level_output(_LERROR_,
                         "commander cannot parse \"%s\" (%x, %llu)\n",
                         reply.c_str(),
                         *((int*)(unsigned char*)&reply[0]),
                         reply.length());
            return;
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
