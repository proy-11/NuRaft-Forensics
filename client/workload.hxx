#include "libnuraft/json.hpp"
#include "libnuraft/nuraft.hxx"
#include <string>
#include <vector>

#ifndef D_RAFT_WORKLOAD
#define D_RAFT_WORKLOAD

namespace nuraft {
enum WORKLOAD_TYPE {
    UNIF,
};

struct request {
public:
    request(int index);
    ~request();

    std::string to_json_str();

    int index;
    std::string payload;
};

class workload {
public:
    workload(std::string path);
    ~workload();
    void proceed(int step);
    void proceed();
    void proceed_batch();
    request get_next_req();
    std::vector<request> get_next_batch();
    int get_next_delay_us();
    int get_next_batch_delay_us();

    void resample_delays(int step);
    int sample_single_delay_us();

private:
    WORKLOAD_TYPE type;
    int size;
    int current;
    int batch_size;
    int delay;
    std::vector<int> batch_delay;
    float freq;
};

std::string to_jsonl(std::vector<request> requests);
} // namespace nuraft

#endif // D_RAFT_WORKLOAD
