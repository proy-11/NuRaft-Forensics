#include "commander.hxx"
#include "d_raft_scheduler.hxx"
#include "nuraft.hxx"
#include "req_socket_mgr.hxx"
#include "server_data_mgr.hxx"
#include "utils.hxx"
#include "workload.hxx"
#include <boost/program_options.hpp>
#include <csignal>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace po = boost::program_options;
namespace fsys = std::filesystem;

using std::string;

const int MAX_NUMBER_OF_JOBS = 1000;

json meta_setting;

server_data_mgr* server_mgr = nullptr;
commander* captain = nullptr;
sync_file_obj *depart = nullptr, *arrive = nullptr;

int _PROG_LEVEL_ = _LINFO_;

void create_server(json data) {
    int id = data["id"];
    char cmd[1024];
    std::snprintf(cmd,
                  sizeof(cmd),
                  "client/d_raft --id %d --ip %s --port %d "
                  "--cport %d --byz %s 1> server_%d.log 2> err_server_%d.log",
                  id,
                  string(data["ip"]).c_str(),
                  int(data["port"]),
                  int(data["cport"]),
                  string(data["byzantine"]).c_str(),
                  id,
                  id);
    pid_t pid = fork();

    if (pid == 0) {
        int status = std::system(cmd);
        if (status < 0) {
            level_output(_LERROR_, "%s\n", strerror(errno));
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
    } else {
        return;
    }
}

std::mutex submit_req_mutex;
void submit_batch(req_socket_manager* req_mgr) { req_mgr->auto_submit(); }

// void submit_batch(std::vector<nuraft::request> requests) {
//     req_socket_manager* req_mgr = new req_socket_manager(requests, arrive, depart, server_mgr);
//     req_mgr->auto_submit();
// }

void experiment(string path) {
    captain->start_experiment_timer();
    nuraft::workload load(path);

    d_raft_scheduler::Scheduler scheduler(
        MAX_NUMBER_OF_JOBS, [](const std::exception& e) { level_output(_LERROR_, "Error: %s", e.what()); });

    while (true) {
        int delay = load.get_next_batch_delay_us();
        auto requests = load.get_next_batch();

        if (!load.proceed_batch()) {
            break;
        }

        req_socket_manager* mgr = new req_socket_manager(requests, arrive, depart, server_mgr);
        scheduler.add_task_to_queue(mgr);
        scheduler.schedule(submit_batch);
        auto interval = std::chrono::system_clock::now() + std::chrono::microseconds(delay);
        std::this_thread::sleep_until(interval);
    }
    scheduler.wait();
    captain->terminate();
}

void signal_handler(int signal) {
    level_output(_LWARNING_, "got signal %d, terminating all servers...\n", signal);

    if (captain != nullptr) {
        captain->terminate(signal);
        delete captain;
    }
    if (arrive != nullptr) delete arrive;
    if (depart != nullptr) delete depart;

    fflush(stdout);
    exit(signal);
}

int main(int argc, const char** argv) {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGABRT, signal_handler);

    string config_file = "";
    if (argc == 2) {
        config_file = argv[1];
    } else if (argc == 3) {
        config_file = argv[1];
        _PROG_LEVEL_ = std::atoi(argv[2]);
    } else {
        std::cout << _C_BOLDYELLOW_ << "Usage: ./d_raft_launcher config_file <PRINT_LEVEL>\n";
        return 1;
    }

    std::ifstream f(config_file);
    meta_setting = json::parse(f);

    server_data_mgr server_mgr(meta_setting["server"]);

    fsys::path working_dir = meta_setting["working_dir"];
    if (!fsys::exists(working_dir)) {
        fsys::create_directories(fsys::absolute(working_dir));
    } else if (!fsys::is_directory(working_dir)) {
        level_output(_LERROR_, "Cannot create directory %s\n", fsys::absolute(working_dir).c_str());
        exit(1);
    }

    depart = new sync_file_obj(working_dir / "depart.jsonl");
    arrive = new sync_file_obj(working_dir / "arrive.jsonl");

    vector<std::thread> server_creators(0);

    level_output(_LINFO_, "Launching servers...\n");

    for (int i = 0; i < server_mgr.ns; i++) {
        server_creators.emplace_back(create_server, meta_setting["server"][i]);
    }

    for (int i = 0; i < server_mgr.ns; i++) {
        server_creators[i].join();
    }

    level_output(_LINFO_, "Connecting...\n");
    std::this_thread::sleep_for(std::chrono::milliseconds(meta_setting["connection_wait_ms"]));

    captain = new commander(meta_setting, &server_mgr);
    captain->deploy();

    level_output(_LINFO_, "Launching client...\n");

    experiment(meta_setting["client"]["path"]);

    return 0;
}
