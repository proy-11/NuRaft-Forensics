#include "workload.hxx"
#include <boost/program_options.hpp>
#include <fstream>
#include <iostream>
#include <tuple>

using json = nlohmann::json;
namespace po = boost::program_options;

namespace nuraft {
request::request(int ind_) {
    index = ind_;
    payload = buffer::alloc(sizeof(byte));
    payload->put(byte(10));
}

request::~request() {}

workload::workload(std::string path) {
    std::ifstream file(path.c_str());
    json object = json::parse(file);

    current = 0;
    size = object.value("size", -1);
    freq = object.value("freq", -1);
    std::string type_str = object.value("type", "NULL");
    if (std::string(type_str).compare("UNIF") == 0) {
        type = UNIF;
    } else {
        std::fprintf(stderr, "Cannot read %s", type_str.c_str());
    }
}

workload::~workload() {}

std::tuple<request, float> workload::get_next_req_ms() {
    request req(current);
    float next_arrival;

    switch (type) {
    case UNIF:
    default:
        next_arrival = 1000 / freq;
        break;
    }

    current++;
    return std::make_tuple(req, next_arrival);
}
} // namespace nuraft

int main(int argc, const char** argv) {

    std::string input;
    // std::string output;

    po::options_description desc("Allowed options");
    desc.add_options()("help,h",
                       "print usage message")("input,i", po::value(&input), "Input file");

    po::variables_map vm;
    po::store(po::command_line_parser(argc, argv).options(desc).run(), vm);
    po::notify(vm);

    if (vm.count("help")) {
        std::cout << desc << "\n";
        return 0;
    }
    if (!vm.count("input")) {
        std::cerr << desc << "\n";
        return 1;
    }

    nuraft::workload w(input);
    for (int i = 0; i < 10; i++) {
        nuraft::request req(0);
        float time;
        std::tie(req, time) = w.get_next_req_ms();
        std::printf("%5d  %5d %f\n", i, req.index, time);
    }
}