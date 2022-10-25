#include "commander.hxx"
#include "d_raft_scheduler.hxx"
#include "nuraft.hxx"
#include "req_socket_mgr.hxx"
#include "server_data_mgr.hxx"
#include "utils.hxx"
#include "workload.hxx"
#include "libnuraft/json.hpp"
#include <boost/program_options.hpp>
#include <csignal>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iomanip>

namespace po = boost::program_options;
namespace fsys = std::filesystem;

using std::string;

const int MAX_NUMBER_OF_JOBS = 1000;

json meta_setting;

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
                  "--cport %d --byz %s 1> server_%d.log 2> err_server_%d.log",
                  current_path.c_str(),
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
void submit_batch(std::shared_ptr<req_socket_manager> req_mgr) { req_mgr->auto_submit(); }

void experiment(string path) {
    captain->start_experiment_timer();
    nuraft::workload load(path);

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
    std::signal(SIGINT, signal_handler);
    std::signal(SIGABRT, signal_handler);
    std::signal(SIGPIPE, signal_handler);

    string config_file = "";
    int size = 0, batch_size = 0;
    float frequency = 0.0;

    po::options_description desc("Allowed options");
    desc.add_options()
        ("help", "produce help message")
        ("config_file", po::value<std::string>()->required(), "config file path")
        ("size", po::value<int>()->default_value(0), "total size")
        ("freq", po::value<float>()->default_value(0.0), "frequency")
        ("batch_size", po::value<int>()->default_value(0), "batch size")
        ("log_level", po::value<int>()->default_value(2), "print log level")
    ;
    
    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);
    

    if (vm.count("help")) {
        std::cout << _C_BOLDYELLOW_ << desc << "\n";
        exit(0);
    }

    if(vm.count("config_file")) {
        config_file = vm["config_file"].as<std::string>();
    } else {
        std::cout << "config file not set.\n";
        exit(1);
    }

    if(vm.count("size")) {
        size = vm["size"].as<int>();
    }

    if(vm.count("freq")) {
        frequency = vm["freq"].as<float>();
    }

    if(vm.count("batch_size")) {
        batch_size = vm["batch_size"].as<int>();
    }

    if(vm.count("log_level")) {
        _PROG_LEVEL_ = vm["log_level"].as<int>();
    }

    std::ifstream f(config_file);
    meta_setting = json::parse(f);

    if(size != 0 && frequency != 0.0 && batch_size != 0) {
        using json = nlohmann::basic_json<std::map, std::vector, std::string, bool, std::int64_t, std::uint64_t, float>;
        
        json workload_json;
        workload_json["size"] = size;
        std::stringstream ss;
        ss << std::setprecision(4) << frequency;
        workload_json["freq"] = std::stof(ss.str());
        workload_json["batch_size"] = batch_size;
        workload_json["type"] = "UNIF";

        std::ofstream file(meta_setting["client"]["path"]);
        file << std::setw(4) << workload_json << std::endl;
    }

    server_mgr = std::shared_ptr<server_data_mgr>(new server_data_mgr(meta_setting["server"]));

    fsys::path working_dir = meta_setting["working_dir"];
    if (!fsys::exists(working_dir)) {
        fsys::create_directories(fsys::absolute(working_dir));
    } else if (!fsys::is_directory(working_dir)) {
        level_output(_LERROR_, "Cannot create directory %s\n", fsys::absolute(working_dir).c_str());
        exit(1);
    }

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

    experiment(meta_setting["client"]["path"]);

    return 0;
}
