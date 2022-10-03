#include "libnuraft/json.hpp"
#include "workload.hxx"
#include <boost/asio.hpp>
#include <boost/program_options.hpp>
#include <chrono>
#include <csignal>
#include <cstdarg>
#include <fstream>
#include <iostream>
#include <mutex>
#include <signal.h>
#include <sstream>
#include <thread>
#include <unistd.h>

#define _ISSUBSTR_(s1, s2) ((s1).find(s2) != std::string::npos)

namespace po = boost::program_options;
namespace asio = boost::asio;
using boost::asio::ip::tcp;
using json = nlohmann::json;

std::mutex print_mutex;

const std::string INIT_ASK = "check\n";
const std::string NEW_SERVER = "addpeer id=%d ep=%s\n";
const std::string EXIT_COMMAND = "exit\n";

std::vector<int> ids(0);
std::vector<std::thread> server_ends(0);
std::vector<tcp::socket*> psockets(0);
std::vector<std::string> endpoints(0);

void end_srv(int i, bool recv);

std::string endpoint_wrapper(std::string ip, int port) { return ip + ":" + std::to_string(port); }

std::string readline(tcp::socket* psock) {
    std::string message = "";
    char buf[1] = {'k'};
    for (; buf[0] != '\n';) {
        psock->read_some(boost::asio::buffer(buf));
        if (buf[0] != '\0') message += buf[0];
    }
    return message;
}

void create_server(int id, std::string ip, int server_port, int client_port, std::string byz) {

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
        std::stringstream msg;
        if (status < 0) {
            msg << "Error: " << strerror(errno) << '\n';
            std::cerr << msg.str();
            exit(errno);
        } else {
            if (WIFEXITED(status)) {
                msg << "Program returned normally, exit code " << WEXITSTATUS(status) << '\n';
            } else {
                msg << "Program exited abnormally\n";
            }
            std::cerr << msg.str();
            exit(status);
        }
    } else {
        return;
    }
}

std::string send_(std::string msg, int i, bool recv) {
    boost::system::error_code error;
    boost::asio::streambuf receive_buffer;
    std::stringstream ostream;

    boost::asio::write(*psockets[i], boost::asio::buffer(msg), error);
    if (error) {
        ostream << "send failed to " << ids[i] << ": " << error.message() << std::endl;
        std::cerr << ostream.str();
        return std::string("error_send\n");
    }

    if (!recv) {
        return std::string("\n");
    }

    std::string buf_str = readline(psockets[i]);

    if (error && error != boost::asio::error::eof) {
        ostream << "receive from " << ids[i] << " failed: " << error.message() << "\n";
        std::cerr << ostream.str();
        buf_str = std::string("error_recv\n");
    } else {
        const char* data = buf_str.c_str();
        ostream << "[Server " << ids[i] << "] " << data << std::endl;
        std::cout << ostream.str();
    }

    return buf_str;
}

void send_(std::string msg, int i) { send_(msg, i, false); }

void ask_init(int i) { send_(INIT_ASK, i, true); }

bool try_add_server(int i, int ir) {
    char c_msg[1024];
    std::snprintf(c_msg, sizeof(c_msg), NEW_SERVER.c_str(), ids[ir], endpoints[ir].c_str());
    std::string result = send_(c_msg, i, true);

    if (_ISSUBSTR_(result, "error")) {
        std::cerr << result << std::endl;

        for (int i = 0; i < psockets.size(); i++) {
            end_srv(i, false);
        }

        exit(1);
    } else if (_ISSUBSTR_(result, "added")) {
        return true;
    } else {
        return false;
    }
}

void add_server(int i, int ir) {
    while (!try_add_server(i, ir)) {
        std::stringstream msg;
        msg << "Add " << ids[ir] << " failed, trying again...\n";
        std::cerr << msg.str();
        std::this_thread::sleep_for(std::chrono::microseconds(1500));
    }
}

void end_srv(int i, bool recv) { send_(EXIT_COMMAND, i, recv); }

void communicate(int i, nuraft::request& req) { send_(req.payload + "\n", i, true); }

void experiment(std::string path) {
    nuraft::workload load(path);

    while (true) {
        int delay;
        nuraft::request req(0);
        std::tie(req, delay) = load.get_next_req_us();

        std::cout << "Got request " << req.index << " and schedule it with delay " << delay << " us\n";
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
    cbuf << "client/d_raft_client --port " << port << " --ip " << ip << " --path " << path;
    int status = std::system(cbuf.str().c_str());
    if (status < 0)
        std::cout << "Error: " << strerror(errno) << '\n';
    else {
        if (WIFEXITED(status))
            std::cout << "Program returned normally, exit code " << WEXITSTATUS(status) << '\n';
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
        endpoints.emplace_back(endpoint_wrapper(data["server"][i]["ip"], data["server"][i]["port"]));
    }

    for (int i = 0; i < number_of_servers; i++) {
        server_creators[i].join();
    }

    std::cout << "Launched! Now connecting";

    for (int i = 0; i < 5; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        std::cout << ".";
    }
    std::cout << std::endl;

    for (int i = 0; i < number_of_servers; i++) {
        psockets.emplace_back(new tcp::socket(io_service));
        // psockets[i] = new tcp::socket(io_service);
        psockets[i]->connect(
            tcp::endpoint(boost::asio::ip::address::from_string(data["server"][i]["ip"]), data["server"][i]["cport"]));
    }

    std::cout << "Connected to all servers! Now checking initialization..." << std::endl;

    for (int i = 0; i < number_of_servers; i++) {
        server_inits.emplace_back(ask_init, i);
    }

    for (int i = 0; i < number_of_servers; i++) {
        server_inits[i].join();
    }

    std::cout << "All servers initialized! Now adding servers..." << std::endl;

    for (int i = 1; i < number_of_servers; i++) {
        add_server(0, i);
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        // server_adds.emplace_back(add_server_as_peer, 0, i);
    }

    // for (int i = 0; i < number_of_servers - 1; i++) {
    //     server_adds[i].join();
    // }

    std::cout << "Raft cluster initialized! Now launching client..." << std::endl;

    experiment(data["client"]["path"]);

    std::cout << "Experiment ended! Now closing servers..." << std::endl;

    for (int i = 0; i < number_of_servers; i++) {
        server_ends.emplace_back(end_srv, i, true);
    }

    for (int i = 0; i < number_of_servers; i++) {
        server_ends[i].join();
    }

    return 0;
}
