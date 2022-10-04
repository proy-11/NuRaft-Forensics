#include <chrono>
#include <cstdarg>
#include <string>
#include <vector>

#define _ISSUBSTR_(s1, s2) ((s1).find(s2) != std::string::npos)
#define _C_CYAN_ "\033[36m"              /* Cyan */
#define _C_BOLDRED_ "\033[1m\033[31m"    /* Bold Red */
#define _C_BOLDYELLOW_ "\033[1m\033[33m" /* Bold Yellow */
#define _C_RESET_ "\033[0m"

using std::vector;

enum _levels_ { _LERROR_ = 0, _LWARNING_ = 1, _LINFO_ = 2, _LDEBUG_ = 3 };

extern int _PROG_LEVEL_;

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

std::string endpoint_wrapper(std::string ip, int port) { return ip + ":" + std::to_string(port); }

std::string strip_endl(std::string str) {
    size_t len = str.length();
    if (str[len - 1] == '\n') {
        return str.substr(0, len - 1);
    } else {
        return str;
    }
}

uint64_t now_() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}