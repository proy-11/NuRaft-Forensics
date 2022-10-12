#include "libnuraft/json.hpp"
#include "utils.hxx"
#include "workload.hxx"
#include <boost/asio.hpp>
#include <map>
#include <mutex>
#include <string>
#include <thread>

#define MAX_BUF_SIZE 65536

namespace asio = boost::asio;
using boost::asio::ip::tcp;
using boost::system::error_code;
using nlohmann::json;

const std::string ERROR_CONN = "error_conn\n";
extern json meta_setting;

asio::io_service io_service;

enum req_status {
    R_PENDING = 0,
    R_RETRY = 1,
    R_ERROR = 2,
    R_COMMITTED = 3,
};

class req_socket_manager {
public:
    req_socket_manager(std::vector<nuraft::request> requests_,
                       int sid,
                       tcp::endpoint ep,
                       sync_file_obj* arrive_,
                       sync_file_obj* depart_,
                       server_data_mgr* mgr_)
        : server_id(sid)
        , endpoint(ep)
        , arrive(arrive_)
        , depart(depart_)
        , server_mgr(mgr_) {
        psock = new tcp::socket(io_service);
        for (auto req: requests_) {
            status[req.index] = R_RETRY;
            requests[req.index] = req;
        }
    }

    ~req_socket_manager() { delete psock; }

    void reset_endpoint(tcp::endpoint ep) { endpoint = ep; }

    void connect() { psock->connect(endpoint); }

    void submit() {}

    void listen() {
        std::thread thr([this]() -> void {
            error_code ec;

            while (true) {
                asio::streambuf sbuf;
                asio::read_until(*psock, sbuf, "\n", ec);

                if (ec) {
                    level_output(_LERROR_, "<Server %2d> Got error %s\n", ec.what());
                    return;
                }

                auto data = sbuf.data();
                std::istringstream iss(std::string(asio::buffers_begin(data), asio::buffers_begin(data) + data.size()));
                std::string line;
                while (std::getline(iss, line)) {
                    process_reply(line);
                }
            }
        });
    }

    void process_reply(std::string reply) {
        int rid;
        json reply_data;
        uint64_t timestamp = now_();
        try {
            reply_data = json::parse(reply);
        } catch (json::exception& ec) {
            level_output(_LERROR_, "<Server %2d> Got invalid reply \"%s\"\n", server_id, ec.what());
            return;
        }

        if (!reply_data.contains("rid")) {
            level_output(_LERROR_, "<Server %2d> %s\n", server_id, reply_data["error"].dump().c_str());
        }

        rid = reply_data["rid"];
        if (reply_data["success"]) {
            mutex.lock();
            status[rid] = R_COMMITTED;
            mutex.unlock();
            arrive->writeline(json({{"index", rid}, {"time", timestamp}}).dump());
            return;
        }

        level_output(
            _LERROR_, "<Server %2d> request #%d failed (%s)\n", server_id, rid, reply_data["error"].dump().c_str());

        if (!reply_data.contains("ec")) {
            mutex.lock();
            status[rid] = R_ERROR;
            mutex.unlock();
            return;
        }

        // Handle error
        nuraft::cmd_result_code ec = static_cast<nuraft::cmd_result_code>(reply_data["ec"]);

        switch (ec) {
        case nuraft::cmd_result_code::NOT_LEADER:
            if (!reply_data.contains("leader")) {
                level_output(_LERROR_,
                             "<Server %2d> request #%d: Got a NOT_LEADER error but no leader is reported. Given up.\n",
                             server_id,
                             rid);
                return;
            } else {
                int leader_id = reply_data["leader"];
                if (leader_id > 0) {
                    int leader = server_mgr->get_index(leader_id);

                    if (leader_id != server_mgr->get_leader_id()) {
                        level_output(_LWARNING_, "leader %d -> %d\n", server_mgr->get_leader_id(), leader_id);
                        server_mgr->set_leader(leader);
                    }
                } else {
                    std::this_thread::sleep_for(std::chrono::milliseconds(meta_setting["missing_leader_retry_ms"]));
                }
            }
            break;
        default:
            break;
        }
    }

private:
    std::unordered_map<int, req_status> status;
    std::unordered_map<int, nuraft::request> requests;
    int server_id;
    std::mutex mutex;
    tcp::socket* psock;
    tcp::endpoint endpoint;
    sync_file_obj* depart;
    sync_file_obj* arrive;
    server_data_mgr* server_mgr;
};