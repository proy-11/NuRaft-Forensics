#include "libnuraft/json.hpp"
#include "workload.hxx"
#include <boost/asio.hpp>
#include <boost/program_options.hpp>
#include <chrono>
#include <csignal>
#include <fstream>
#include <iostream>
#include <signal.h>
#include <sstream>
#include <thread>
#include <unistd.h>

namespace po = boost::program_options;
using boost::asio::ip::tcp;
using json = nlohmann::json;

const std::string INIT_ASK = "check\n";
const std::string NEW_SERVER = "addpeer id=%d ep=%s\n";
const std::string EXIT_COMMAND = "exit\n";

std::vector<int> ids(0);
std::vector<std::thread> server_ends(0);
std::vector<tcp::socket*> psockets(0);
std::vector<std::string> endpoints(0);

std::string endpoint_wrapper(std::string ip, int port) {
    return ip + ":" + std::to_string(port);
}

void create_server(
    int id, std::string ip, int server_port, int client_port, std::string byz) {

    char cmd[1024];
    std::snprintf(cmd,
                  sizeof(cmd),
                  "client/d_raft --id %d --ip %s --port %d "
                  "--cport %d --byz %s 1> server_%d.log 2> err_server_%d.log",
                  id,
                  ip.c_str(),
                  server_port,
                  client_port,
                  byz.c_str(),
                  id,
                  id);
    pid_t pid = fork();

    if (pid == 0) {
        int status = std::system(cmd);
        if (status < 0) {
            std::cout << "Error: " << strerror(errno) << '\n';
            exit(errno);
        } else {
            if (WIFEXITED(status))
                std::cout << "Program returned normally, exit code "
                          << WEXITSTATUS(status) << '\n';
            else
                std::cout << "Program exited abnormally\n";
            exit(status);
        }
    } else {
        return;
    }
}

void send_(std::string msg, int i, bool recv) {
    boost::system::error_code error;
    boost::asio::streambuf receive_buffer;

    boost::asio::write(*psockets[i], boost::asio::buffer(msg), error);
    if (error) {
        std::cerr << " send failed to " << ids[i] << ": " << error.message() << std::endl;
    }

    if (!recv) {
        return;
    }

    std::vector<char> buf(1024);
    size_t len = psockets[i]->read_some(boost::asio::buffer(buf), error);
    std::string buf_str(buf.begin(), buf.end());
    buf_str.resize(len);

    if (error && error != boost::asio::error::eof) {
        std::cerr << "receive from " << ids[i] << " failed: " << error.message()
                  << std::endl;
    } else {
        const char* data = buf_str.c_str();
        std::cout << "receive from server " << ids[i] << ": " << data << std::endl;
    }
    return;
}

void send_(std::string msg, int i) { send_(msg, i, false); }

void ask_init(int i) { send_(INIT_ASK, i, true); }

void add_srv(int i, int ir) {
    char c_msg[1024];
    std::snprintf(c_msg, sizeof(c_msg), NEW_SERVER.c_str(), ir, endpoints[ir].c_str());
    send_(c_msg, i, true);
}

void end_srv(int i, bool recv) { send_(EXIT_COMMAND, i, recv); }

void communicate(int i, nuraft::request& req) { send_(req.payload + "\n", i, true); }

void experiment(std::string path) {
    nuraft::workload load(path);

    while (true) {
        int delay;
        nuraft::request req(0);
        std::tie(req, delay) = load.get_next_req_us();

        if (req.index < 0) {
            break;
        }

        std::thread thread_(communicate, 0, std::ref(req));
        thread_.detach();
        std::this_thread::sleep_for(std::chrono::microseconds(delay));
    }
}

void create_client(int port, std::string ip, std::string path) {
    std::ostringstream cbuf;
    cbuf << "client/d_raft_client --port " << port << " --ip " << ip << " --path "
         << path;
    int status = std::system(cbuf.str().c_str());
    if (status < 0)
        std::cout << "Error: " << strerror(errno) << '\n';
    else {
        if (WIFEXITED(status))
            std::cout << "Program returned normally, exit code " << WEXITSTATUS(status)
                      << '\n';
        else
            std::cout << "Program exited abnormally\n";
    }
}

void signal_handler(int signal) {
    std::cout << "Terminating servers...";

    for (int i = 0; i < psockets.size(); i++) {
        end_srv(i, false);
    }
    std::cout << "Finished!\n";
    fflush(stdout);
    exit(0);
}

int main(int argc, const char** argv) {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGABRT, signal_handler);

    std::string config_file = "";
    if (argc == 2) {
        config_file = argv[1];
    } else {
        std::cout << "Usage: ./d_raft_launcher config_file\n";
        return 1;
    }
    std::ifstream f(config_file);
    json data = json::parse(f);

    int number_of_servers = data["server"].size();
    // int number_of_clients = data["client"].size();

    if (number_of_servers < 1) {
        std::cerr << "Number of servers must be at least 1!" << std::endl;
        exit(1);
    }

    std::vector<std::thread> server_creators(0);
    std::vector<std::thread> server_inits(0);
    std::vector<std::thread> server_adds(0);
    boost::asio::io_service io_service;
    // std::vector<std::thread> clients(number_of_clients);

    std::cout << "Launching servers..." << std::endl;

    for (int i = 0; i < number_of_servers; i++) {
        server_creators.emplace_back(create_server,
                                     data["server"][i]["id"],
                                     data["server"][i]["ip"],
                                     data["server"][i]["port"],
                                     data["server"][i]["cport"],
                                     data["server"][i]["byzantine"]);
        ids.emplace_back(data["server"][i]["id"]);
        endpoints.emplace_back(
            endpoint_wrapper(data["server"][i]["ip"], data["server"][i]["port"]));
    }

    for (int i = 0; i < number_of_servers; i++) {
        server_creators[i].join();
    }

    std::cout << "Launched! Now connecting..." << std::endl;

    std::this_thread::sleep_for(std::chrono::seconds(3));

    for (int i = 0; i < number_of_servers; i++) {
        psockets.emplace_back(new tcp::socket(io_service));
        // psockets[i] = new tcp::socket(io_service);
        psockets[i]->connect(
            tcp::endpoint(boost::asio::ip::address::from_string(data["server"][i]["ip"]),
                          data["server"][i]["cport"]));
    }

    std::cout << "Connected to all servers! Now checking initialization..." << std::endl;

    for (int i = 0; i < number_of_servers; i++) {
        server_inits.emplace_back(ask_init, i);
    }

    for (int i = 0; i < number_of_servers; i++) {
        server_inits[i].join();
    }

    std::cout << "All servers initialized! Now adding servers..." << std::endl;

    // for (int i = 1; i < number_of_servers; i++) {
    //     server_adds.emplace_back(add_srv, 0, i)
    // }

    // for (int i = 0; i < number_of_servers - 1; i++) {
    //     server_adds[i].join();
    // }

    // std::cout << "Raft cluster initialized! Now launching client..." << std::endl;

    // // std::this_thread::sleep_for(std::chrono::seconds(10));
    // //     clients.emplace_back(
    // //         create_client, data["client"][i]["cport"], data["client"][i]["path"]);
    // // }

    // experiment(data["client"]["path"]);

    std::cout << "Experiment ended! Now closing servers..." << std::endl;

    for (int i = 0; i < number_of_servers; i++) {
        server_ends.emplace_back(end_srv, i, true);
    }

    for (int i = 0; i < number_of_servers; i++) {
        server_ends[i].join();
    }

    return 0;
}
