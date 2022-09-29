#include "libnuraft/json.hpp"
#include "libnuraft/nuraft.hxx"
#include <string>

namespace nuraft {
enum WORKLOAD_TYPE {
    UNIF,
};

struct request {
public:
    request(int index);
    ~request();

    int index;
    std::string payload;
};

class workload {
public:
    workload(std::string path);
    ~workload();
    std::tuple<request, int> get_next_req_us();

private:
    WORKLOAD_TYPE type;
    int size;
    int current;
    float freq;
};
} // namespace nuraft
