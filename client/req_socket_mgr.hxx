#include "libnuraft/json.hpp"
#include "server_data_mgr.hxx"
#include "socket_mgr.hxx"
#include "utils.hxx"
#include "workload.hxx"
#include <boost/asio.hpp>
#include <map>
#include <mutex>
#include <string>
#include <thread>

#define MAX_BUF_SIZE 65536
#define MAX_PENDING_PERIOD 5

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

class req_socket_manager : public socket_mgr {
public:
    req_socket_manager(std::vector<nuraft::request> requests_,
                       int sid,
                       sync_file_obj* arrive_,
                       sync_file_obj* depart_,
                       server_data_mgr* mgr_)
        : server_id(sid)
        , arrive(arrive_)
        , depart(depart_)
        , server_mgr(mgr_) {
        psock = new tcp::socket(io_service);
        my_mgr_index = server_mgr->register_sock_mgr(this);
        for (auto req: requests_) {
            status[req.index] = R_RETRY;
            requests[req.index] = req;
        }
    }

    ~req_socket_manager() { delete psock; }

    void connect() {
        connection_waiter.lock();
        psock->connect(server_mgr->get_leader_endpoint());
    }

    void auto_submit() {
        connect();
        listen();
        submit_all_requests();

        int pending_periods = 0;
        while (true) {
            std::this_thread::sleep_for(std::chrono::milliseconds(meta_setting["missing_leader_retry_ms"]));

            std::vector<int> retries(0), pendings(0);
            mutex.lock();
            for (auto pair: status) {
                if (pair.second == R_RETRY)
                    retries.emplace_back(pair.first);
                else if (pair.second == R_PENDING)
                    pendings.emplace_back(pair.first);
            }
            mutex.unlock();

            if (retries.empty() && pendings.empty()) {
                break;
            }

            pending_periods++;
            submit_requests(retries);
            if (pending_periods >= MAX_PENDING_PERIOD) {
                submit_requests(pendings);
                pending_periods = 0;
            }
        }

        server_mgr->unregister_sock_mgr(my_mgr_index);
    }

    void listen() {
        std::thread thr([this]() -> void {
            error_code ec;

            while (true) {
                asio::streambuf sbuf;
                asio::read_until(*psock, sbuf, "\n", ec);

                if (ec) {
                    level_output(_LERROR_, "<Server %2d> Got error %s\n", ec.what());
                    if (ec == asio::error::bad_descriptor) {
                        connect();
                        continue;
                    }
                    return;
                }

                uint64_t timestamp = now_();
                auto data = sbuf.data();
                std::istringstream iss(std::string(asio::buffers_begin(data), asio::buffers_begin(data) + data.size()));
                std::string line;
                while (std::getline(iss, line)) {
                    process_reply(line, timestamp);
                }
            }
        });
        thr.detach();
    }

    void process_reply(std::string reply, uint64_t timestamp) {
        int rid;
        json reply_data;
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
            set_status(rid, R_COMMITTED);
            arrive->writeline(json({{"index", rid}, {"time", timestamp}}).dump());
            return;
        }

        level_output(
            _LERROR_, "<Server %2d> request #%d failed (%s)\n", server_id, rid, reply_data["error"].dump().c_str());

        if (!reply_data.contains("ec")) {
            set_status(rid, R_ERROR);
            return;
        }

        // Handle error
        nuraft::cmd_result_code ec = static_cast<nuraft::cmd_result_code>(reply_data["ec"]);

        switch (ec) {
        case nuraft::cmd_result_code::NOT_LEADER:
            if (!reply_data.contains("leader")) {
                level_output(
                    _LERROR_, "<Server %2d> request #%d: NOT_LEADER without reporting leader. \n", server_id, rid);
                return;
            } else {
                int leader_id = reply_data["leader"];
                if (leader_id > 0 && leader_id != server_mgr->get_leader_id()) {
                    int leader = server_mgr->get_index(leader_id);
                    mutex.lock();
                    for (auto pair: status) {
                        if (pair.second == R_PENDING) set_status(pair.first, R_RETRY);
                    }
                    mutex.unlock();
                    server_mgr->set_leader(leader);
                    level_output(_LWARNING_, "leader %d -> %d\n", server_mgr->get_leader_id(), leader_id);
                } else
                    set_status(rid, R_RETRY);
            }
            break;
        default:
            set_status(rid, R_ERROR);
            break;
        }
    }

    inline void notify() { connection_waiter.unlock(); }

    inline void set_status(int rid, req_status status_) {
        mutex.lock();
        status[rid] = status_;
        mutex.unlock();
    }

    void submit_request(int rid) {
        const std::string msg = requests[rid].to_json_str();
        set_status(rid, R_PENDING);
        psock->write_some(asio::buffer(msg, msg.length()));
    }

    void submit_requests(std::vector<int>& rids) {
        mutex.lock();
        std::string msg;
        for (int rid: rids) {
            msg += requests[rid].to_json_str();
            set_status(rid, R_PENDING);
        }
        psock->write_some(asio::buffer(msg, msg.length()));
        mutex.unlock();
    }

    void submit_all_requests() {
        mutex.lock();
        std::string msg;
        for (auto pair: requests) {
            msg += pair.second.to_json_str();
            set_status(pair.first, R_PENDING);
        }
        psock->write_some(asio::buffer(msg, msg.length()));
        mutex.unlock();
    }

private:
    std::unordered_map<int, req_status> status;
    std::unordered_map<int, nuraft::request> requests;
    int server_id;
    std::recursive_mutex mutex;
    std::mutex connection_waiter;
    tcp::socket* psock;
    sync_file_obj* depart;
    sync_file_obj* arrive;
    server_data_mgr* server_mgr;
    int my_mgr_index;
};