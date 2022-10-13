#include "utils.hxx"
#include <chrono>
#include <cstdio>

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