#pragma once

#include "libnuraft/json.hpp"
#include <arpa/inet.h>
#include <map>
#include <mutex>
#include <string>
#include <sys/socket.h>
#include <vector>

#ifndef F_SERVER_DATA_MGR
#define F_SERVER_DATA_MGR

using nlohmann::json;

inline std::string endpoint_wrapper(std::string ip, int port) { return ip + ":" + std::to_string(port); }

class req_socket_manager;

class server_data_mgr {
public:
    server_data_mgr(json data);
    ~server_data_mgr();

    int get_index(int id);
    int get_id(int index);
    std::shared_ptr<sockaddr_in> get_endpoint(int index);
    std::string get_endpoint_str(int index);
    void set_leader(int new_index);
    void terminate_all_req_mgrs();
    int get_leader();
    std::shared_ptr<sockaddr_in> get_leader_endpoint();
    int get_leader_id();

    int register_sock_mgr(std::shared_ptr<req_socket_manager> mgr);
    void unregister_sock_mgr(int index);

    void wait();

    bool terminated;
    int ns;

private:
    std::mutex mutex;
    std::mutex empty_req_mutex;
    std::unordered_map<int, int> indices;
    std::vector<int> ids;
    std::vector<std::shared_ptr<sockaddr_in>> endpoints;
    std::vector<std::string> endpoints_str;
    std::unordered_map<int, std::shared_ptr<req_socket_manager>> socket_managers;
    int leader_index;
    int manager_index;
};

#endif