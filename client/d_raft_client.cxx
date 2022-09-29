#include "workload.hxx"
#include <boost/asio.hpp>
#include <boost/program_options.hpp>
#include <boost/thread/thread.hpp>
#include <iostream>
#include <thread>
#include <tuple>
#include <vector>
#include <thread>

using namespace boost::asio;
using ip::tcp;
using std::cout;
using std::endl;
using std::string;
namespace po = boost::program_options;

const string msg = "Hello from Client!\n";

struct cmargs {
public:
    cmargs(int p, std::string path) {
        this->port = p;
        this->config_path = path;
    }
    ~cmargs() {}

    int port;
    std::string config_path;
};

cmargs parse_args(int argc, const char** argv) {
    po::options_description desc("Allowed options");
    desc.add_options()("help",
                       "produce help message")("port", po::value<int>(), "port number")(
        "path", po::value<std::string>(), "workload config path");

    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);

    if (vm.count("help")) {
        cout << desc << "\n";
        exit(1);
    }

    int port;
    std::string path;
    if (vm.count("port")) {
        port = vm["port"].as<int>();
        cout << "Port was set to " << port << ".\n";
    } else {
        cout << "Port was not set.\n";
        exit(1);
    }

    if (vm.count("path")) {
        path = vm["path"].as<std::string>();
        cout << "Config file was set to " << path << ".\n";
    } else {
        cout << "Config file was not set.\n";
        exit(1);
    }

    return cmargs(port, path);
}

void communicate(tcp::socket* psocket, nuraft::request& req) {
    boost::system::error_code error;

    boost::asio::write(*psocket, boost::asio::buffer(req.payload), error);
    if (!error) {
        cout << "Client sent hello message to "
             << "!" << endl;
    } else {
        cout << "send failed: " << error.message() << endl;
    }
    cout << "hanging..." << endl;
    boost::asio::streambuf receive_buffer;
    std::vector<char> buf(1024);
    size_t len = psocket->read_some(boost::asio::buffer(buf), error);
    std::string buf_str(buf.begin(), buf.end());
    buf_str.resize(len);

    if (error && error != boost::asio::error::eof) {
        cout << "receive failed: " << error.message() << endl;
    } else {
        // const char *data = boost::asio::buffer_cast<const char
        // *>(receive_buffer.data());
        const char* data = buf_str.c_str();
        cout << "receive from server "
             << ": " << data << endl;
    }
    return;
}

int main(int argc, const char** argv) {
    cmargs args = parse_args(argc, argv);

    boost::asio::io_service io_service;
    tcp::socket sock(io_service);
    sock.connect(
        tcp::endpoint(boost::asio::ip::address::from_string("127.0.0.1"), args.port));

    nuraft::workload load(args.config_path);

    while (true) {
        int delay;
        nuraft::request req(0);
        std::tie(req, delay) = load.get_next_req_us();

        if (req.index < 0) {
            break;
        }

        std::thread thread_(communicate, &sock, std::ref(req));
        thread_.detach();
        boost::this_thread::sleep(boost::posix_time::microseconds(delay));
    }

    return 0;
}
