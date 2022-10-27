#include "workload.hxx"
#include <fstream>
#include <iostream>
#include <sstream>
#include <tuple>

namespace nuraft {
request::request(int ind_) {
    index = ind_;
    payload = std::string("+1");
}

request::~request() {}

/* Send serialized string with line break
 */
std::string request::to_json_str() {
    std::stringstream ss;
    ss << "{\"index\": " << index << ", \"payload\": \"" << payload << "\"}\n";
    return ss.str();
}

std::string to_jsonl(std::vector<request>& requests) {
    std::stringstream ss;
    for (request req: requests) {
        ss << req.to_json_str();
    }
    return ss.str();
}

workload::workload(std::string path) {
    std::ifstream file(path.c_str());
    json object = json::parse(file);

    current = 0;
    size = object["size"];
    freq = object["freq"];
    batch_size = object["batch_size"];
    delay = -1;
    batch_delay = std::vector<int>(batch_size);

    std::string type_str = object.value("type", "NULL");
    if (std::string(type_str) == "UNIF") {
        type = UNIF;
    } else {
        std::fprintf(stderr, "Cannot read %s", type_str.c_str());
    }

    resample_delays(0);
}

workload::workload(json settings) {
    try {
        current = 0;
        size = settings["size"];
        freq = settings["freq"];
        batch_size = settings["batch_size"];
        delay = -1;
        batch_delay = std::vector<int>(batch_size);

        std::string type_str = settings.value("type", "NULL");
        if (std::string(type_str) == "UNIF") {
            type = UNIF;
        } else {
            std::fprintf(stderr, "Cannot read %s", type_str.c_str());
        }
        resample_delays(0);
    } catch (json::exception& e) {
        std::fprintf(stderr, "Error reading workload settings from json obj:\n%s\n", settings.dump().c_str());
        exit(1);
    }
}
workload::~workload() {}

bool workload::proceed(int step) {
    if (current >= size) return false;

    if (current + step > size) {
        step = size - current;
    }

    resample_delays(step);
    current += step;
    return true;
}

bool workload::proceed_batch() { return proceed(batch_size); }

request workload::get_next_req() { return request(current); }

std::vector<request> workload::get_next_batch() {
    std::vector<request> requests(0);
    for (int i = current; i < current + batch_size && i < size; i++) {
        requests.emplace_back(i);
    }
    return requests;
}

int workload::get_next_delay_us() { return delay; }
int workload::get_next_batch_delay_us() {
    // int sum_delays = 0;
    // for (int d: batch_delay) {
    //     sum_delays += d;
    // }
    // return sum_delays;

    return delay;
}

void workload::resample_delays(int step) {
    // if (step <= 0) {
    //     for (int i = 0; i < batch_size; i++) {
    //         batch_delay[i] = sample_single_delay_us();
    //     }
    //     delay = batch_delay[current % batch_size];
    // } else {
    //     for (int i = 0; i < batch_size && i < step; i++) {
    //         batch_delay[(current + i) % batch_size] = sample_single_delay_us();
    //     }
    //     delay = batch_delay[(current + step) % batch_size];
    // }
    delay = sample_single_delay_us();
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