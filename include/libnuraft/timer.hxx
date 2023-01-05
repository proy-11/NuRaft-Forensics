#ifndef _INCLUDE_TIMER
#define _INCLUDE_TIMER

#include "ptr.hxx"
#include <chrono>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

using std::chrono::duration;
using std::chrono::duration_cast;
using std::chrono::high_resolution_clock;
using std::chrono::microseconds;
using std::chrono::steady_clock;

namespace nuraft {
class timer_t {
public:
    timer_t()
        : timing(false) {}
    ~timer_t() {}

    bool is_empty() { return records.empty(); }

    void start_timer() {
        starter = high_resolution_clock::now();
        timing = true;
    }

    void add_record(std::string key) {
        if (!timing) return;
        auto ender = high_resolution_clock::now();
        auto duration = duration_cast<microseconds>(ender - starter);
        records[key] = (int64_t)duration.count();
        timing = false;
    }

    std::string stringify() {
        std::stringstream ss;
        ss << "{";
        size_t rem = records.size();
        if (rem > 0) ss << "\n";
        for (auto& ent: records) {
            rem--;
            ss << "    \"" << ent.first << "\": " << ent.second << (rem > 0 ? ",\n" : "\n");
        }
        ss << "}";
        return ss.str();
    }

private:
    bool timing;
    steady_clock::time_point starter;
    std::unordered_map<std::string, int64_t> records;
};

class timing_mgr {
public:
    timing_mgr() {}
    ~timing_mgr() {}

    void add_sess(ptr<timer_t> sess) {
        if (sess != nullptr && !sess->is_empty()) sessions.emplace_back(sess);
    }

    void dump(std::string path) {
        std::ofstream file;
        file.open(path, std::ofstream::out);
        file << "[\n";
        size_t rem = sessions.size();
        for (auto& ent: sessions) {
            rem--;
            file << ent->stringify() << (rem > 0 ? ",\n" : "\n");
        }
        file << "]";
        file.close();
    }

private:
    std::vector<ptr<timer_t>> sessions;
};
} // namespace nuraft

#endif // !_INCLUDE_TIMER