#pragma once

#include <cstdarg>
#include <string>

#ifndef D_RAFT_UTILS
#define D_RAFT_UTILS

#define BUF_SIZE 1024

#define _ISSUBSTR_(s1, s2) ((s1).find(s2) != std::string::npos)
#define _C_CYAN_ "\033[36m"              /* Cyan */
#define _C_BOLDRED_ "\033[1m\033[31m"    /* Bold Red */
#define _C_BOLDYELLOW_ "\033[1m\033[33m" /* Bold Yellow */
#define _C_RESET_ "\033[0m"

enum _levels_ { _LERROR_ = 0, _LWARNING_ = 1, _LINFO_ = 2, _LDEBUG_ = 3 };

extern int _PROG_LEVEL_;

void level_output(_levels_ level, const char* fmt, ...);

std::string strip_endl(std::string str);

uint64_t now_();

bool is_empty(std::string str);

#endif