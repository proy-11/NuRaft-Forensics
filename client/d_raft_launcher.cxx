#include <iostream>
#include <thread>
#include <fstream>
#include <signal.h>
#include <csignal>
#include <sstream>
#include <boost/program_options.hpp>
#include "libnuraft/json.hpp"

namespace po = boost::program_options;
using json = nlohmann::json;

void create_server(int id, std::string ip, int server_port, int client_port, std::string byz) {
    std::ostringstream sbuf;
    sbuf << "client/d_raft --id " << id << " --ip " << ip \
    <<" --port " << server_port << " --cport " << client_port <<" --byz " << "false";
    int status = std::system(sbuf.str().c_str());
    if (status < 0)
        std::cout << "Error: " << strerror(errno) << '\n';
    else {
        if (WIFEXITED(status))
            std::cout << "Program returned normally, exit code " << WEXITSTATUS(status) << '\n';
        else
            std::cout << "Program exited abnormaly\n";
    }
}

void create_client(int port, int num) {
    std::ostringstream cbuf;
    cbuf << "client/d_raft_client --port " << port << " --num " << num;
    int status = std::system(cbuf.str().c_str());
    if (status < 0)
        std::cout << "Error: " << strerror(errno) << '\n';
    else {
        if (WIFEXITED(status))
            std::cout << "Program returned normally, exit code " << WEXITSTATUS(status) << '\n';
        else
            std::cout << "Program exited abnormaly\n";
    }
}

void signal_handler(int signal) {
    std::cout << "Terminating...\n";
    fflush(stdout);
}


int main(int argc, const char **argv)
{
    std::signal(SIGINT,signal_handler);

    std::string config_file = "";
    if( argc == 2 ) {
        config_file = argv[1];
    }
    else {
      std::cout << "Usage: ./d_raft_launcher config_file\n";
      return 1;
    }
    std::ifstream f(config_file);
    json data = json::parse(f);

    int number_of_servers = data["server"].size();
    int number_of_clients = data["client"].size();

    std::vector<std::thread> servers(number_of_servers);
    std::vector<std::thread> clients(number_of_clients);

    for(int i = 0; i < number_of_servers; i++) {
        servers.emplace_back(create_server,data["server"][i]["id"], data["server"][i]["ip"], data["server"][i]["port"], data["client"][i]["cport"],data["server"][i]["byzantine"]);
    }
    
    std::this_thread::sleep_for(std::chrono::seconds(10));
    
    for(int i = 0; i < number_of_clients; i++) {
        clients.emplace_back(create_client, data["client"][i]["cport"], data["client"][i]["num"]); 
    }

    for(int i = 0; i < number_of_clients; i++) {
        clients[i].join();
    }
    return 0;
}
