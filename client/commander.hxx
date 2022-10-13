#pragma once

#include "libnuraft/json.hpp"
#include <boost/asio.hpp>
#include <latch>
#include <mutex>
#include <string>
#include <vector>

using boost::asio::ip::tcp;
using nlohmann::json;

namespace asio = boost::asio;

class server_data_mgr;

class commander {
public:
    commander(json data, server_data_mgr* mgr);
    ~commander();

    void deploy();
    void send_command(int index, std::string cmd, boost::system::error_code& ec);

    void start_experiment_timer();

    void terminate(int error = 0);

    void send_addpeer_command(int j);

    void maintain_connection();
    void process_reply(std::string reply);

    void show_exp_duration();

    char* status_table();

private:
    int ns;
    uint64_t time_start;
    json setting;
    json replica_status_dict;
    std::mutex mutex;
    std::recursive_mutex exit_mutex;
    std::latch *init_latch, *peer_latch;
    server_data_mgr* server_mgr;
    std::vector<tcp::socket*> sockets;
};