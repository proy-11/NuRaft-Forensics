#include "libnuraft/json.hpp"
#include "workload.hxx"
#include <boost/asio.hpp>
#include <boost/program_options.hpp>
#include <csignal>
#include <fstream>
#include <iostream>
#include <signal.h>
#include <sstream>
#include <thread>

namespace po = boost::program_options;
using boost::asio::ip::tcp;
using json = nlohmann::json;

const std::string INIT_ASK = "check\n";
const std::string NEW_SERVER = "addpeer id=%d ep=%s\n";
const std::string EXIT_COMMAND = "exit\n";

std::string endpoint_wrapper(std::string ip, int port) {
    return ip + ":" + std::to_string(port);
}

void create_server(
    int id, std::string ip, int server_port, int client_port, std::string byz) {
    std::ostringstream sbuf;
    // sbuf << "echo wwf";
    sbuf << "client/d_raft --id " << id << " --ip " << ip << " --port " << server_port
         << " --cport " << client_port << " --byz " << byz << " 1> server_" << id
         << ".log 2> err_server_" << id << ".log";
    int status = std::system(sbuf.str().c_str());
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

void ask_init(int id, tcp::socket* psock) {
    boost::system::error_code error;
    boost::asio::streambuf receive_buffer;

    boost::asio::write(*psock, boost::asio::buffer(INIT_ASK), error);
    if (error) {
        std::cerr << "send failed to " << id << ": " << error.message() << std::endl;
    }

    std::vector<char> buf(1024);
    size_t len = psock->read_some(boost::asio::buffer(buf), error);
    std::string buf_str(buf.begin(), buf.end());
    buf_str.resize(len);

    if (error && error != boost::asio::error::eof) {
        std::cerr << "receive from " << id << " failed: " << error.message() << std::endl;
    } else {
        const char* data = buf_str.c_str();
        std::cout << "receive from server " << id << ": " << data << std::endl;
    }
    return;
}

void add_srv(int is, int ir, std::string endpoint, tcp::socket* psock_sender) {
    boost::system::error_code error;
    boost::asio::streambuf receive_buffer;

    char c_msg[1024];
    std::snprintf(c_msg, sizeof(c_msg), NEW_SERVER.c_str(), ir, endpoint.c_str());
    boost::asio::write(*psock_sender, boost::asio::buffer(c_msg), error);
    if (error) {
        std::cerr << is << " send failed to " << ir << ": " << error.message()
                  << std::endl;
    }

    std::vector<char> buf(1024);
    size_t len = psock_sender->read_some(boost::asio::buffer(buf), error);
    std::string buf_str(buf.begin(), buf.end());
    buf_str.resize(len);

    if (error && error != boost::asio::error::eof) {
        std::cerr << is << " receive from " << ir << " failed: " << error.message()
                  << std::endl;
    } else {
        const char* data = buf_str.c_str();
        std::cout << is << " receive from server " << ir << ": " << data << std::endl;
    }
    return;
}

void end_srv(int id, tcp::socket* psock) {
    boost::system::error_code error;
    boost::asio::streambuf receive_buffer;

    boost::asio::write(*psock, boost::asio::buffer(EXIT_COMMAND), error);
    if (error) {
        std::cerr << " send failed to " << id << ": " << error.message() << std::endl;
    }

    std::vector<char> buf(1024);
    size_t len = psock->read_some(boost::asio::buffer(buf), error);
    std::string buf_str(buf.begin(), buf.end());
    buf_str.resize(len);

    if (error && error != boost::asio::error::eof) {
        std::cerr << "receive from " << id << " failed: " << error.message() << std::endl;
    } else {
        const char* data = buf_str.c_str();
        std::cout << "receive from server " << id << ": " << data << std::endl;
    }
    return;
}

void communicate(tcp::socket* psocket, nuraft::request& req) {
    boost::system::error_code error;

    boost::asio::write(*psocket, boost::asio::buffer(req.payload + "\n"), error);
    if (error) {
        std::cout << "send failed: " << error.message() << std::endl;
    }
    boost::asio::streambuf receive_buffer;
    std::vector<char> buf(1024);
    size_t len = psocket->read_some(boost::asio::buffer(buf), error);
    std::string buf_str(buf.begin(), buf.end());
    buf_str.resize(len);

    if (error && error != boost::asio::error::eof) {
        std::cout << "receive failed: " << error.message() << std::endl;
    } else {
        const char* data = buf_str.c_str();
        std::cout << "receive from server: " << data << std::endl;
    }
    return;
}
void experiment(std::string ip, int port, std::string path) {
    boost::asio::io_service io_service;
    tcp::socket sock(io_service);
    sock.connect(tcp::endpoint(boost::asio::ip::address::from_string(ip), port));

    nuraft::workload load(path);

    while (true) {
        int delay;
        nuraft::request req(0);
        std::tie(req, delay) = load.get_next_req_us();

        if (req.index < 0) {
            break;
        }

        std::thread thread_(communicate, &sock, std::ref(req));
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
    std::cout << "Terminating...\n";
    fflush(stdout);
}

int main(int argc, const char** argv) {
    std::signal(SIGINT, signal_handler);

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
    std::vector<std::thread> server_ends(0);
    std::vector<tcp::socket*> psockets(number_of_servers);
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
    }

    for (int i = 0; i < number_of_servers; i++) {
        server_creators[i].join();
    }

    std::cout << "Launched! Now connecting..." << std::endl;

    for (int i = 0; i < number_of_servers; i++) {
        psockets[i] = new tcp::socket(io_service);
        psockets[i]->connect(
            tcp::endpoint(boost::asio::ip::address::from_string(data["server"][i]["ip"]),
                          data["server"][i]["cport"]));
    }

    std::cout << "Connected to all servers! Now checking initialization..." << std::endl;

    for (int i = 0; i < number_of_servers; i++) {
        server_inits.emplace_back(ask_init, data["server"][i]["id"], psockets[i]);
    }

    for (int i = 0; i < number_of_servers; i++) {
        server_inits[i].join();
    }

    std::cout << "All servers initialized! Now adding servers..." << std::endl;

    // for (int i = 1; i < number_of_servers; i++) {
    //     server_adds.emplace_back(
    //         add_srv,
    //         data["server"][0]["id"],
    //         data["server"][i]["id"],
    //         endpoint_wrapper(data["server"][i]["ip"], data["server"][i]["port"]),
    //         psockets[0]);
    // }

    // for (int i = 0; i < number_of_servers - 1; i++) {
    //     server_adds[i].join();
    // }

    // std::cout << "Raft cluster initialized! Now launching client..." << std::endl;

    // // std::this_thread::sleep_for(std::chrono::seconds(10));
    // //     clients.emplace_back(
    // //         create_client, data["client"][i]["cport"], data["client"][i]["path"]);
    // // }

    // experiment(
    //     data["server"][0]["ip"], data["server"][0]["cport"], data["client"]["path"]);

    // std::thread client(create_client,
    //                    data["server"][0]["cport"],
    //                    data["server"][0]["ip"],
    //                    data["client"]["path"]);

    // client.join();

    std::cout << "Experiment ended! Now closing servers..." << std::endl;

    for (int i = 0; i < number_of_servers; i++) {
        server_ends.emplace_back(end_srv, data["server"][i]["id"], psockets[i]);
    }

    for (int i = 0; i < number_of_servers; i++) {
        server_ends[i].join();
    }

    // for (int i = 0; i < number_of_clients; i++) {
    //     clients[i].join();
    // }
    return 0;
}
