#include "libnuraft/json.hpp"
#include "libnuraft/nuraft.hxx"

namespace nuraft {
enum WORKLOAD_TYPE {
    UNIF,
};

struct request {
public:
    request();
    request(int index);
    request(int ind_, ptr<buffer> payload_);
    ~request();

    int index;
    ptr<buffer> payload;
};

class workload {
public:
    workload(std::string path);
    ~workload();
    std::tuple<request, float> get_next_req_ms();

private:
    WORKLOAD_TYPE type;
    int size;
    int current;
    float freq;
};
} // namespace nuraft
