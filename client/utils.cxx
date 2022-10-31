#include "utils.hxx"

#if __APPLE__
#define TOP_CMD "/usr/bin/top -l 0 -n 1 -pid "
#else
#define TOP_CMD "/usr/bin/top -b -d 1 -n 1 -p "
#endif

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

bool is_empty(std::string str) {
    return std::string(str.c_str()) == "" || str.find_first_not_of(" \0\t\n\v\f\r") == str.npos;
}

pid_t create_server(nlohmann::json data, fsys::path rootpath) {
    int id = data["id"];
    pid_t pid = fork();

    if (pid == 0) {
        int fd_out, fd_err;
        fsys::path current = boost::filesystem::current_path();
        auto path_out = rootpath / (std::string("out_server_") + std::to_string(id) + ".log");
        auto path_err = rootpath / (std::string("err_server_") + std::to_string(id) + ".log");

        if ((fd_out = open(path_out.c_str(), O_CREAT | O_WRONLY, S_IRUSR | S_IWUSR)) < 0) {
            level_output(_LERROR_, "cannot create stdout file %s (%s)\n", path_out.c_str(), strerror(errno));
            exit(errno);
        }

        if ((fd_err = open(path_err.c_str(), O_CREAT | O_WRONLY, S_IRUSR | S_IWUSR)) < 0) {
            level_output(_LERROR_, "cannot create stdout file %s (%s)\n", path_err.c_str(), strerror(errno));
            exit(errno);
        }

        if (dup2(fd_out, 1) < 0) {
            level_output(_LERROR_, "cannot redirect stdout to file (%s)\n", strerror(errno));
            exit(errno);
        }

        if (dup2(fd_err, 2) < 0) {
            level_output(_LERROR_, "cannot redirect stdout to file (%s)\n", strerror(errno));
            exit(errno);
        }

        int status = execl((current / "client" / "d_raft").c_str(),
                           (current / "client" / "d_raft").c_str(),
                           "--id",
                           std::to_string(int(data["id"])).c_str(),
                           "--ip",
                           std::string(data["ip"]).c_str(),
                           "--port",
                           std::to_string(int(data["port"])).c_str(),
                           "--cport",
                           std::to_string(int(data["cport"])).c_str(),
                           "--byz",
                           std::string(data["byzantine"]).c_str(),
                           "--workers",
                           std::to_string(int(data["workers"])).c_str(),
                           "--qlen",
                           std::to_string(int(data["qlen"])).c_str(),
                           "--datadir",
                           std::string(data["datadir"]).c_str(),
                           (char*)NULL);

        // int status = std::system(cmd);

        close(fd_out);
        close(fd_err);
        if (status < 0) {
            level_output(_LERROR_, "%s (%d)\n", strerror(errno), errno);
            exit(errno);
        } else {
            if (WIFEXITED(status)) {
                level_output(
                    _LDEBUG_, "<Server %2d> Program returned normally, exit code %d\n", id, WEXITSTATUS(status));
            } else {
                level_output(_LERROR_, "<Server %2d> Program returned abnormally\n", id);
            }
            exit(status);
        }
    } else if (pid < 0) {
        level_output(_LERROR_, "cannot fork (%s)\n", strerror(errno));
        exit(errno);
    } else {
        return pid;
    }
}

pid_t create_cpu_monitor(fsys::path rootpath) {
    pid_t monitored = getpid();
    pid_t pid = fork();

    if (pid == 0) {
        int fd_out;
        auto path_out = rootpath / "cpu-usage.log";

        if ((fd_out = open(path_out.c_str(), O_CREAT | O_WRONLY, S_IRUSR | S_IWUSR)) < 0) {
            level_output(_LERROR_, "cannot create stdout file %s (%s)\n", path_out.c_str(), strerror(errno));
            exit(errno);
        }

        if (dup2(fd_out, 1) < 0) {
            level_output(_LERROR_, "cannot redirect stdout to file (%s)\n", strerror(errno));
            exit(errno);
        }
        if (dup2(fd_out, 2) < 0) {
            level_output(_LERROR_, "cannot redirect stderr to file (%s)\n", strerror(errno));
            exit(errno);
        }

        // int status = execl("/usr/bin/top", "/usr/bin/top", "-n", "1", "|", "grep", "CPU usage", (char*)NULL);
        std::stringstream ss;
        ss << TOP_CMD << monitored << " | grep --line-buffered " << monitored;
        int status = execl("/bin/sh", "/bin/sh", "-c", ss.str().c_str(), (char*)NULL);

        close(fd_out);

        if (status < 0) {
            level_output(_LERROR_, "CPU monitor exit with error: %s (%d)\n", strerror(errno), errno);
            exit(errno);
        } else {
            if (WIFEXITED(status)) {
                level_output(_LDEBUG_, "CPU monitor returned normally, exit code %d\n", WEXITSTATUS(status));
            } else {
                level_output(_LERROR_, "CPU monitor returned abnormally\n");
            }
            exit(status);
        }
    } else if (pid < 0) {
        level_output(_LERROR_, "cannot fork (%s)\n", strerror(errno));
        exit(errno);
    } else {
        return pid;
    }
}