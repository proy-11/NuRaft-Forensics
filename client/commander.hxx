#pragma once

#include "libnuraft/json.hpp"
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

using nlohmann::json;

class server_data_mgr;

class commander {
public:
    commander(json data, std::shared_ptr<server_data_mgr> mgr);
    ~commander();

    void deploy();
    ssize_t send_command(int index, std::string cmd);

    void start_experiment_timer();

    void terminate(int error = 0);

    void send_addpeer_command(int j);

    void maintain_connection();
    bool process_reply(std::string reply);

    void show_exp_duration();

    char* status_table();

private:
    int ns;
    uint64_t time_start;
    json setting;
    json replica_status_dict;
    std::mutex mutex;
    std::recursive_mutex exit_mutex;
    std::atomic<int> server_waited;
    std::atomic<bool> terminated;
    std::condition_variable cv_server;
    std::shared_ptr<server_data_mgr> server_mgr;
    std::vector<int> sockets;
    std::vector<std::shared_ptr<std::thread>> connections;
};