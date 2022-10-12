#include "libnuraft/json.hpp"
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#define _ISSUBSTR_(s1, s2) ((s1).find(s2) != std::string::npos)
#define _C_CYAN_ "\033[36m"              /* Cyan */
#define _C_BOLDRED_ "\033[1m\033[31m"    /* Bold Red */
#define _C_BOLDYELLOW_ "\033[1m\033[33m" /* Bold Yellow */
#define _C_RESET_ "\033[0m"

using nlohmann::json;

enum _levels_ { _LERROR_ = 0, _LWARNING_ = 1, _LINFO_ = 2, _LDEBUG_ = 3 };

extern int _PROG_LEVEL_;

inline std::string endpoint_wrapper(std::string ip, int port);

class sync_file_obj {
public:
    sync_file_obj(std::string path) {
        fp = std::fopen(path.c_str(), "w");
        if (errno != 0) {
            std::fprintf(stderr, "Error: Cannot open file %s\n", path.c_str());
            exit(1);
        }
    };
    ~sync_file_obj() { std::fclose(fp); };

    void writeline(std::string line) {
        if (line.empty()) {
            return;
        }
        if (line.back() != '\n') {
            line += "\n";
        }
        mutex.lock();
        std::fprintf(fp, "%s", line.c_str());
        mutex.unlock();
    }

private:
    FILE* fp;
    std::mutex mutex;
};

class server_data_mgr {
public:
    server_data_mgr(json data) {
        for (int i = 0; i < data.size(); i++) {
            int id = data[i]["id"];
            if (indices.find(id) != indices.end()) {
                std::string error_message = "ID conflict: " + std::to_string(id);
                throw std::logic_error(error_message.c_str());
            }

            ids.emplace_back(id);
            indices[id] = i;
            endpoints.emplace_back(tcp::endpoint(asio::ip::address::from_string(data[i]["ip"]), data[i]["cport"]));
            endpoints_str.emplace_back(endpoint_wrapper(data[i]["ip"], data[i]["port"]));
        }
        leader_index = data.size() - 1;
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
        mutex.unlock();
    }

    int get_leader() {
        int result;
        mutex.lock();
        result = leader_index;
        mutex.unlock();
        return result;
    }

    inline int get_leader_id() { return get_id(get_leader()); }

private:
    std::mutex mutex;
    std::unordered_map<int, int> indices;
    std::vector<int> ids;
    std::vector<tcp::endpoint> endpoints;
    std::vector<std::string> endpoints_str;
    int leader_index;
};

void level_output(_levels_ level, const char* fmt, ...) {
    if (level > _PROG_LEVEL_) return;

    va_list args;
    va_start(args, fmt);
    switch (level) {
    case _levels_::_LERROR_:
        std::vfprintf(stderr, (std::string(_C_BOLDRED_ "[ERROR] ") + fmt + _C_RESET_).c_str(), args);
        break;
    case _levels_::_LWARNING_:
        std::vfprintf(stderr, (std::string(_C_BOLDYELLOW_ "[WARN ] ") + fmt + _C_RESET_).c_str(), args);
        break;
    case _levels_::_LINFO_:
        std::vfprintf(stderr, (std::string(_C_CYAN_ "[INFO ] ") + fmt + _C_RESET_).c_str(), args);
        break;
    case _levels_::_LDEBUG_:
        std::vfprintf(stderr, (std::string(_C_RESET_ "[DEBUG] ") + fmt).c_str(), args);
        break;
    default:
        break;
    }
    va_end(args);
}

inline std::string endpoint_wrapper(std::string ip, int port) { return ip + ":" + std::to_string(port); }

inline std::string strip_endl(std::string str) {
    size_t len = str.length();
    if (str[len - 1] == '\n') {
        return str.substr(0, len - 1);
    } else {
        return str;
    }
}

inline uint64_t now_() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}