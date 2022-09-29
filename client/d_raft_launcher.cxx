#include <iostream>
#include <thread>
#include <fstream>
#include <signal.h>
#include <csignal>
#include <sstream>
#include <boost/program_options.hpp>

namespace po = boost::program_options;

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

    int id, port, cport, num;
    std::string ipaddr, byzantine;
    po::options_description desc("Allowed options");
    desc.add_options()("help",
                       "produce help message")("id", po::value<int>(), "server id")(
        "ip", po::value<std::string>(), "IP address")(
        "port", po::value<int>(), "port number")(
        "cport", po::value<int>(), "Client port number")(
        "byz", po::value<std::string>(), "Byzantine status")(
        "num", po::value<int>(), "Number of clients");

    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);

    if (vm.count("help")) {
        std::cout << desc << "\n";
        exit(0);
    }
      if (vm.count("id")) {
        id = vm["id"].as<int>();
    } else {
        std::cout << "Server ID was not set.\n";
        exit(1);
    }
    if (vm.count("port")) {
        port = vm["port"].as<int>();
    } else {
        std::cout << "Port was not set.\n";
        exit(1);
    }
    if (vm.count("cport")) {
        cport = vm["cport"].as<int>();
    } else {
        std::cout << "Client port was not set.\n";
        exit(1);
    }
    if (vm.count("ip")) {
        ipaddr = vm["ip"].as<std::string>();
    } else {
        std::cout << "IP address not set.\n";
        exit(1);
    }
    if (vm.count("byz")) {
        byzantine = vm["byz"].as<std::string>();
    } else {
        std::cout << "Byzantine status not set.\n";
        exit(1);
    }
    if (vm.count("num")) {
        num = vm["num"].as<int>();
    } else {
        std::cout << "num of client not set.\n";
        exit(1);
    }

    std::thread s(create_server,id, ipaddr, port, cport, byzantine);
    
    std::this_thread::sleep_for(std::chrono::seconds(5));
    
    std::thread c(create_client, cport, num); 

    c.join();
    return 0;
}
