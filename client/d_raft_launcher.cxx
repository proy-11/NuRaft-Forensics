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
        std::cout << "config file " << config_file;
    }
    else {
      std::cout << "Usage: ./d_raft_launcher config_file\n";
      return 1;
    }
    json data = json::parse(config_file);

    // std::cout << data["server"][1]["id"] ;

    // std::thread s(create_server,data["server"][1]["id"], data["server"][1]["ip"], data["server"][1]["port"], data["client"][1]["cport"],data["server"][1]["byzantine"] );
    
    // std::this_thread::sleep_for(std::chrono::seconds(5));
    
    // std::thread c(create_client, data["client"][1]["cport"], data["client"][1]["num"]); 

    // c.join();
    return 0;
}
