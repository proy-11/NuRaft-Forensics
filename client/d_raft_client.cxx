#include <iostream>
#include <thread>
#include <vector>
#include <boost/asio.hpp>
#include <boost/thread/thread.hpp>
#include <boost/program_options.hpp>

using namespace boost::asio;
using ip::tcp;
using std::cout;
using std::endl;
using std::string;
namespace po = boost::program_options;

const string msg = "Hello from Client!\n";

struct cmargs
{
public:
    cmargs(int p, int n)
    {
        this->start_port = p;
        this->num_servers = n;
    }
    ~cmargs() {}

    int start_port;
    int num_servers;
};

cmargs parse_args(int argc, const char **argv)
{
    po::options_description desc("Allowed options");
    desc.add_options()("help", "produce help message")("port", po::value<int>(), "port number")("num", po::value<int>(), "number of servers");

    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);

    if (vm.count("help"))
    {
        cout << desc << "\n";
        exit(1);
    }

    int start_port, num_servers;
    if (vm.count("port"))
    {
        start_port = vm["port"].as<int>();
        cout << "Starting port was set to "
             << start_port << ".\n";
    }
    else
    {
        cout << "Port was not set.\n";
        exit(1);
    }

    if (vm.count("num"))
    {
        num_servers = vm["num"].as<int>();
        cout << "Number of servers was set to " << num_servers << ".\n";
    }
    else
    {
        cout << "Number of servers was not set.\n";
        exit(1);
    }

    return cmargs(start_port, num_servers);
}

void communicate(int server_id, tcp::socket *psocket)
{
    boost::system::error_code error;

    boost::asio::write(*psocket, boost::asio::buffer(msg), error);
    if (!error)
    {
        cout << "Client sent hello message to " << server_id << "!" << endl;
    }
    else
    {
        cout << "send failed: " << error.message() << endl;
    }
    cout << "hanging..." << endl;
    boost::asio::streambuf receive_buffer;
    std::vector<char> buf(1024);
    size_t len = psocket->read_some(boost::asio::buffer(buf), error);
    std::string buf_str(buf.begin(), buf.end());
    buf_str.resize(len);

    if (error && error != boost::asio::error::eof)
    {
        cout << "receive failed: " << error.message() << endl;
    }
    else
    {
        // const char *data = boost::asio::buffer_cast<const char *>(receive_buffer.data());
        const char *data = buf_str.c_str();
        cout << "receive from server " << server_id << ": " << data << endl;
    }
    return;
}

int main(int argc, const char **argv)
{
    cmargs args = parse_args(argc, argv);

    std::vector<tcp::socket *> sockets(args.num_servers);

    for (int i = 0; i < args.num_servers; i++)
    {
        boost::asio::io_service io_service;
        sockets[i] = new tcp::socket(io_service);
        sockets[i]->connect(tcp::endpoint(boost::asio::ip::address::from_string("127.0.0.1"), args.start_port + i));
    }

    while (true)
    {
        for (int i = 0; i < args.num_servers; i++)
        {
            std::thread thread_(communicate, i, sockets[i]);
            thread_.detach();
        }
        boost::this_thread::sleep(boost::posix_time::seconds(1));
    }

    return 0;
}
