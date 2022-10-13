#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#ifndef D_RAFT_UTILS
#define D_RAFT_UTILS

#define _ISSUBSTR_(s1, s2) ((s1).find(s2) != std::string::npos)
#define _C_CYAN_ "\033[36m"              /* Cyan */
#define _C_BOLDRED_ "\033[1m\033[31m"    /* Bold Red */
#define _C_BOLDYELLOW_ "\033[1m\033[33m" /* Bold Yellow */
#define _C_RESET_ "\033[0m"

enum _levels_ { _LERROR_ = 0, _LWARNING_ = 1, _LINFO_ = 2, _LDEBUG_ = 3 };

extern int _PROG_LEVEL_;

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

// class semaphore {
// public:
//     semaphore(int count_ = 0)
//         : count(count_) {}

//     inline void notify() {
//         std::unique_lock<std::mutex> lock(mtx);
//         count++;
//         cv.notify_one();
//     }

//     inline void wait() {
//         std::unique_lock<std::mutex> lock(mtx);
//         cv.wait(lock, [this]() { return count > 0; });
//         count--;
//     }

// private:
//     std::mutex mtx;
//     std::condition_variable cv;
//     int count;
// };

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

#endif