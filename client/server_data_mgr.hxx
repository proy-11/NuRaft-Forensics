#include "libnuraft/json.hpp"
#include "socket_mgr.hxx"
#include <boost/asio.hpp>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#ifndef F_SERVER_DATA_MGR
#define F_SERVER_DATA_MGR

namespace asio = boost::asio;
using boost::asio::ip::tcp;
using nlohmann::json;

inline std::string endpoint_wrapper(std::string ip, int port) { return ip + ":" + std::to_string(port); }

class server_data_mgr {
public:
    server_data_mgr(nlohmann::json data) {
        for (size_t i = 0; i < data["server"].size(); i++) {
            int id = data["server"][i]["id"];
            if (indices.find(id) != indices.end()) {
                std::string error_message = "ID conflict: " + std::to_string(id);
                throw std::logic_error(error_message.c_str());
            }
            ids.emplace_back(id);
            indices[id] = i;
            endpoints.emplace_back(tcp::endpoint(asio::ip::address::from_string(data["server"][i]["ip"]), data["server"][i]["cport"]));
            endpoints_str.emplace_back(endpoint_wrapper(data["server"][i]["ip"], data["server"][i]["port"]));
        }
        leader_index = 0; //data["server"].size() - 1;
        cout << "server data mgr ctor leader_index: " << leader_index << endl;
        manager_index = 0;
    }
    ~server_data_mgr() {}

    int get_index(int id) {
        auto itr = std::find(ids.begin(), ids.end(), id);
        if (itr == ids.cend()) {
            std::string error_message = "ID not found: " + std::to_string(id);
            throw std::logic_error(error_message.c_str());
        }
        return std::distance(ids.begin(), itr);
    }

    inline int get_id(int index) { return ids[index]; }

    inline tcp::endpoint get_endpoint(int index) { return endpoints[index]; }

    inline std::string get_endpoint_str(int index) { return endpoints_str[index]; }

    void set_leader(int new_index) {
        mutex.lock();
        leader_index = new_index;
        cout << "set_leader " << leader_index << endl;
        mutex.unlock();

        for (auto pair: socket_managers) {
            pair.second->notify();
        }
    }

    int get_leader() {
        int result;
        mutex.lock();
        result = leader_index;
        cout << "get_leader leader_index : " << leader_index << "\n";
        mutex.unlock();
        return result;
    }

    inline tcp::endpoint get_leader_endpoint() { return endpoints[get_leader()]; }

    inline int get_leader_id() { return get_id(get_leader()); }

    inline int register_sock_mgr(socket_mgr* mgr) {
        mutex.lock();
        socket_managers[manager_index] = mgr;
        manager_index++;
        mutex.unlock();
        return 1;
    }

    inline void unregister_sock_mgr(int index) {
        mutex.lock();
        socket_managers.erase(index);
        mutex.unlock();
    }

private:
    std::mutex mutex;
    std::unordered_map<int, int> indices;
    std::vector<int> ids;
    std::vector<tcp::endpoint> endpoints;
    std::vector<std::string> endpoints_str;
    std::unordered_map<int, socket_mgr*> socket_managers;
    int leader_index;
    int manager_index;
};

#endif