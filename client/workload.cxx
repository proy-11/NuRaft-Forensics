#include "workload.hxx"
#include <boost/program_options.hpp>
#include <fstream>
#include <iostream>
#include <sstream>
#include <tuple>

using json = nlohmann::json;
namespace po = boost::program_options;

namespace nuraft {
// request::request(int ind_) {
//     index = ind_;
//     payload = std::string("+1");
// }
request::request(int ind_):
    index(ind_),
    payload(std::string("+1"))
{
    
}
request::request() {}
request::~request() {}

/* Send serialized string with line break
 */
std::string request::to_json_str() {
    std::stringstream ss;
    ss << "{\"index\": " << index << ", \"payload\": \"" << payload << "\"}\n";
    return ss.str();
}

std::string workload::to_jsonl(std::vector<nuraft::request>& requests) {
    std::stringstream ss;
    for (request req: requests) {
        ss << req.to_json_str();
    }
    return ss.str();
}

// workload::workload(std::string path) {
//     std::ifstream file(path.c_str());
//     json object = json::parse(file);

//     current = 0;
//     size = object["size"];
//     freq = object["freq"];
//     batch_size = object["batch_size"];
//     delay = -1;
//     batch_delay = std::vector<int>(batch_size);

//     std::string type_str = object.value("type", "NULL");
//     if (std::string(type_str) == "UNIF") {
//         type = UNIF;
//     } else {
//         std::fprintf(stderr, "Cannot read %s", type_str.c_str());
//     }

//     resample_delays(0);
// }

workload::workload(std::string path):
    type(WORKLOAD_TYPE::UNIF),
    size(-1),
    current(-1),
    batch_size(-1),
    delay(-1),
    batch_delay(),
    freq(-1)
{    
    std::ifstream file(path.c_str());
    json object = json::parse(file);

    current = 0;
    size = object["size"];
    freq = object["freq"];
    batch_size = object["batch_size"];
    batch_delay = std::vector<int>(batch_size);
    // current_batch = 0;

    std::string type_str = object.value("type", "NULL");
    if (std::string(type_str) == "UNIF") {
        type = UNIF;
    } else {
        std::fprintf(stderr, "Cannot read %s", type_str.c_str());
    }

    resample_delays(0);
}

workload::~workload() {}

void workload::proceed(int step) {
    if (current >= size) return;

    if (current + step > size) {
        step = size - current;
    }

    resample_delays(step);
    current += step;
}

void workload::proceed() { proceed(1); }
void workload::proceed_batch() { proceed(batch_size); }

request workload::get_next_req() { return request(current); }

// int workload::get_current_batch() {
//     return current_batch;
// }
int workload::get_total_num_batch() {
    return batch_size;
}
std::vector<request> workload::get_next_batch(int batch) {
    // std::cout << "current batch: " << get_current_batch();
    // if(current_batch > batch_size) {
    //     std::cout << "returning empty req curr batch > batch size\n";
    //     return {};
    // }

    if(batch > batch_size) { return {};}
    
    // std::cout << "get_next_batch current: " << current << "\n";
    std::vector<request> requests(0);
    // for (int i = current; i < current + batch_size && i < size; i++) {
    for (int i = current; i < batch*200 && i < size; i++) {
        requests.emplace_back(i);
    }
    // current_batch++;
    return requests;
}

int workload::get_next_delay_us() { return delay; }
int workload::get_next_batch_delay_us() {
    int sum_delays = 0;
    for (int d: batch_delay) {
        sum_delays += d;
    }
    return sum_delays;
}

void workload::resample_delays(int step) {
    if (step <= 0) {
        for (int i = 0; i < batch_size; i++) {
            batch_delay[i] = sample_single_delay_us();
        }
        delay = batch_delay[current % batch_size];
    } else {
        for (int i = 0; i < batch_size && i < step; i++) {
            batch_delay[(current + i) % batch_size] = sample_single_delay_us();
        }
        delay = batch_delay[(current + step) % batch_size];
    }
}

int workload::sample_single_delay_us() {
    switch (this->type) {
    case UNIF:
        return int(1000000 / this->freq);
    default:
        return -1;
    }
}

} // namespace nuraft

// int main(int argc, const char** argv) {

//     std::string input;
//     // std::string output;

//     po::options_description desc("Allowed options");
//     desc.add_options()("help,h",
//                        "print usage message")("input,i", po::value(&input), "Input
//                        file");

//     po::variables_map vm;
//     po::store(po::command_line_parser(argc, argv).options(desc).run(), vm);
//     po::notify(vm);

//     if (vm.count("help")) {
//         std::cout << desc << "\n";
//         return 0;
//     }
//     if (!vm.count("input")) {
//         std::cerr << desc << "\n";
//         return 1;
//     }

//     nuraft::workload w(input);
//     for (int i = 0; i < 10; i++) {
//         nuraft::request req(0);
//         int time;
//         std::tie(req, time) = w.get_next_req_us();
//         std::printf("%5d  %5d %d\n", i, req.index, time);
//     }
// }