#include "server_data_mgr.hxx"
#include "req_socket_mgr.hxx"
#include "utils.hxx"
#include <iostream>

server_data_mgr::server_data_mgr(json data)
    : terminated(false) {
    ns = data.size();
    leader_index = 0;
    manager_index = 0;

    if (ns < 1) {
        std::cerr << "Error: number of servers must be at least 1!" << std::endl;
        exit(1);
    }

    for (int i = 0; i < ns; i++) {
        int id = data[i]["id"];
        if (indices.find(id) != indices.end()) {
            std::string error_message = "ID conflict: " + std::to_string(id);
            throw std::logic_error(error_message.c_str());
        }

        ids.emplace_back(id);
        indices[id] = i;
        auto serv_addr = std::shared_ptr<sockaddr_in>(new sockaddr_in());
        serv_addr->sin_family = AF_INET;
        serv_addr->sin_port = htons(data[i]["cport"]);
        if (inet_pton(AF_INET, std::string(data[i]["ip"]).c_str(), &serv_addr->sin_addr) <= 0) {
            level_output(_LERROR_, "Invalid address: \"%s\"\n", std::string(data[i]["ip"]).c_str());
            exit(-1);
        }
        endpoints.emplace_back(serv_addr);
        endpoints_str.emplace_back(endpoint_wrapper(data[i]["ip"], data[i]["port"]));
    }
}

server_data_mgr::~server_data_mgr() {}

int server_data_mgr::get_index(int id) {
    auto itr = std::find(ids.begin(), ids.end(), id);
    if (itr == ids.cend()) {
        std::string error_message = "ID not found: " + std::to_string(id);
        throw std::logic_error(error_message.c_str());
    }
    return std::distance(ids.begin(), itr);
}

int server_data_mgr::get_id(int index) { return ids[index]; }

std::shared_ptr<sockaddr_in> server_data_mgr::get_endpoint(int index) { return endpoints[index]; }

std::string server_data_mgr::get_endpoint_str(int index) { return endpoints_str[index]; }

void server_data_mgr::set_leader(int new_index) {
    mutex.lock();
    level_output(_LWARNING_, "leader %d -> %d\n", leader_index, new_index);
    leader_index = new_index;
    mutex.unlock();

    for (auto& pair: socket_managers) {
        pair.second->notify();
    }
}

void server_data_mgr::terminate_all_req_mgrs() {
    terminated = true;
    for (auto& pair: socket_managers) {
        pair.second->terminate();
    }
}

int server_data_mgr::get_leader() {
    int result;
    mutex.lock();
    result = leader_index;
    mutex.unlock();
    return result;
}

std::shared_ptr<sockaddr_in> server_data_mgr::get_leader_endpoint() { return endpoints[get_leader()]; }

int server_data_mgr::get_leader_id() { return get_id(get_leader()); }

int server_data_mgr::register_sock_mgr(std::shared_ptr<req_socket_manager> mgr) {
    mutex.lock();
    int seqno = manager_index;
    socket_managers[manager_index] = mgr;
    manager_index++;
    empty_req_mutex.try_lock();
    mutex.unlock();
    return seqno;
}

void server_data_mgr::unregister_sock_mgr(int index) {
    mutex.lock();
    socket_managers.erase(index);
    level_output(_LWARNING_, "Unregistered mgr #%d\n", index);
    if (socket_managers.empty()) empty_req_mutex.unlock();
    mutex.unlock();
}

void server_data_mgr::wait() { empty_req_mutex.lock(); }