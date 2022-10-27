#include "commander.hxx"
#include "d_raft_scheduler.hxx"
#include "libnuraft/json.hpp"
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
#include <iomanip>
#include <iostream>

namespace po = boost::program_options;
namespace fsys = std::filesystem;

const int MAX_NUMBER_OF_JOBS = 1000;

json meta_setting, workload_setting;

std::shared_ptr<server_data_mgr> server_mgr = nullptr;
std::shared_ptr<commander> captain = nullptr;
std::shared_ptr<sync_file_obj> arrive = nullptr;
std::shared_ptr<sync_file_obj> depart = nullptr;

int _PROG_LEVEL_ = _LINFO_;

void create_server(json data) {
    int id = data["id"];
    char cmd[1024];
    std::string current_path = std::filesystem::current_path();
    std::snprintf(cmd,
                  sizeof(cmd),
                  "%s/client/d_raft --id %d --ip %s --port %d "
                  "--cport %d --byz %s --workers %d --qlen %d 1> server_%d.log 2> err_server_%d.log",
                  current_path.c_str(),
                  id,
                  string(data["ip"]).c_str(),
                  int(data["port"]),
                  int(data["cport"]),
                  string(data["byzantine"]).c_str(),
                  int(data["workers"]),
                  int(data["qlen"]),
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
void submit_batch(std::shared_ptr<req_socket_manager> req_mgr) { req_mgr->auto_submit(); }

void experiment(json workload_) {
    captain->start_experiment_timer();
    nuraft::workload load(workload_);

    // d_raft_scheduler::Scheduler scheduler(
    //     MAX_NUMBER_OF_JOBS, [](const std::exception& e) { level_output(_LERROR_, "Error: %s", e.what()); });

    while (true) {
        int delay = load.get_next_batch_delay_us();
        auto requests = load.get_next_batch();

        if (!load.proceed_batch()) {
            break;
        }
        level_output(_LDEBUG_, "sending batch #%d -- #%d\n", requests[0].index, requests.back().index);

        auto mgr = std::shared_ptr<req_socket_manager>(new req_socket_manager(requests, arrive, depart, server_mgr));
        mgr->self_register();
        // scheduler.add_task_to_queue(mgr);
        // scheduler.schedule(submit_batch);
        // auto interval = std::chrono::system_clock::now() + std::chrono::microseconds(delay);
        // std::this_thread::sleep_until(interval);
        std::thread thr(submit_batch, mgr);
        thr.detach();
        std::this_thread::sleep_for(std::chrono::microseconds(delay));
    }
    // scheduler.wait();
    server_mgr->wait();
    captain->terminate();
}

void signal_handler(int signal) {
    level_output(_LWARNING_, "got signal %d, terminating all servers...\n", signal);

    fflush(stdout);
    // if (arrive != nullptr) delete arrive;
    // if (depart != nullptr) delete depart;

    if (captain != nullptr) {
        captain->terminate(signal);
    } else {
        level_output(_LWARNING_, "failed to find commander, exit without proper termination\n");
        exit(signal);
    }
}

int main(int argc, const char** argv) {
    string config_file = "";

    po::options_description desc("Allowed options");
    desc.add_options()("help", "produce help message")("config-file",
                                                       po::value<std::string>()->required(),
                                                       "config file path")("size", po::value<int>(), "total size")(
        "freq", po::value<float>(), "frequency")("batch-size", po::value<int>(), "batch size")(
        "log-level", po::value<int>()->default_value(2), "print log level");

    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);

    if (vm.count("help")) {
        std::cout << _C_BOLDYELLOW_ << desc << "\n";
        exit(0);
    }

    if (vm.count("config-file")) {
        config_file = vm["config-file"].as<std::string>();
    } else {
        level_output(_LERROR_, "config file not set, aborting\n");
        exit(1);
    }

    if (vm.count("log-level")) {
        _PROG_LEVEL_ = vm["log-level"].as<int>();
    }

    try {
        std::ifstream f(config_file);
        meta_setting = json::parse(f);
    } catch (json::exception& je) {
        level_output(_LERROR_, "Error reading meta settings: %s\n", je.what());
        exit(1);
    }

    try {
        std::ifstream file(std::string(meta_setting["client"]["path"]).c_str());
        workload_setting = json::parse(file);
    } catch (json::exception& je) {
        level_output(_LERROR_, "Error reading workload settings: %s\n", je.what());
        exit(1);
    }

    if (vm.count("size")) {
        workload_setting["size"] = vm["size"].as<int>();
    }

    if (vm.count("freq")) {
        float frequency = vm["freq"].as<float>();
        std::stringstream ss;
        ss << std::setprecision(4) << frequency;
        workload_setting["freq"] = std::stof(ss.str());
    }

    if (vm.count("batch-size")) {
        workload_setting["batch_size"] = vm["batch-size"].as<int>();
    }

    level_output(_LINFO_,
                 "Using workload settings\nsize      = %10d,\nfreq      = %10.4f,\nbatchsize = %10d\n",
                 int(workload_setting.at("size")),
                 float(workload_setting.at("freq")),
                 int(workload_setting.at("batch_size")));

    server_mgr = std::shared_ptr<server_data_mgr>(new server_data_mgr(meta_setting["server"]));

    fsys::path working_dir = meta_setting["working_dir"];
    if (!fsys::exists(working_dir)) {
        fsys::create_directories(fsys::absolute(working_dir));
    } else if (!fsys::is_directory(working_dir)) {
        level_output(_LERROR_, "Cannot create directory %s\n", fsys::absolute(working_dir).c_str());
        exit(1);
    }

    std::signal(SIGINT, signal_handler);
    std::signal(SIGABRT, signal_handler);
    std::signal(SIGPIPE, signal_handler);

    depart = std::shared_ptr<sync_file_obj>(new sync_file_obj(working_dir / "depart.jsonl"));
    arrive = std::shared_ptr<sync_file_obj>(new sync_file_obj(working_dir / "arrive.jsonl"));

    vector<std::thread> server_creators(0);

    level_output(_LINFO_, "Launching servers...\n");

    for (int i = 0; i < server_mgr->ns; i++) {
        server_creators.emplace_back(create_server, meta_setting["server"][i]);
    }

    for (int i = 0; i < server_mgr->ns; i++) {
        server_creators[i].join();
    }

    level_output(_LINFO_, "Connecting...\n");
    std::this_thread::sleep_for(std::chrono::milliseconds(meta_setting["connection_wait_ms"]));

    captain = std::shared_ptr<commander>(new commander(meta_setting, server_mgr));
    captain->deploy();

    level_output(_LINFO_, "Launching client...\n");

    experiment(workload_setting);

    return 0;
}
