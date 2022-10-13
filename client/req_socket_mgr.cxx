#include "req_socket_mgr.hxx"
#include "server_data_mgr.hxx"
#include "utils.hxx"
#include <thread>

#define MAX_PENDING_PERIOD 5

req_socket_manager::req_socket_manager(std::vector<nuraft::request> requests_,
                                       sync_file_obj* arrive_,
                                       sync_file_obj* depart_,
                                       server_data_mgr* mgr_)
    : arrive(arrive_)
    , depart(depart_)
    , server_mgr(mgr_) {
    my_mgr_index = server_mgr->register_sock_mgr(this);
    terminated = server_mgr->terminated;

    asio::io_service io_service;
    psock = new tcp::socket(io_service);
    start = INT_MAX;
    end = -1;
    for (auto& req: requests_) {
        status[req.index] = R_RETRY;
        requests[req.index] = req;
        start = start < req.index ? start : req.index;
        end = end > req.index ? end : req.index;
    }
}

req_socket_manager::~req_socket_manager() {
    level_output(_LWARNING_, "destroying mgr #%d \n", my_mgr_index);
    if (psock != nullptr) {
        psock->close();
        delete psock;
    }
}

void req_socket_manager::connect() {
    connection_waiter.lock();
    psock->connect(server_mgr->get_leader_endpoint());
}

void req_socket_manager::terminate() {
    terminated = true;
    psock->close();
}

inline void req_socket_manager::wait_retry() {
    std::this_thread::sleep_for(std::chrono::milliseconds(meta_setting["submit_retry_ms"]));
}

void req_socket_manager::auto_submit() {
    boost::system::error_code ec;
    connect();
    listen();

    while (!terminated) {
        submit_all_requests(ec);
        if (ec) {
            level_output(_LERROR_, "mgr #%d cannot send: %s\n", my_mgr_index, ec.message().c_str());
            wait_retry();
        } else {
            break;
        }
    }

    int pending_periods = 0;
    while (!terminated) {
        wait_retry();

        if (terminated) break;

        std::vector<int> retries(0), pendings(0);
        mutex.lock();
        for (auto& pair: status) {
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
        submit_requests(retries, ec);
        if (ec) {
            level_output(_LERROR_, "mgr #%d cannot send: %s\n", my_mgr_index, ec.message().c_str());
            continue;
        } else {
            mutex.lock();
            for (auto& rid: retries) {
                set_status(rid, R_PENDING);
            }
            mutex.unlock();
        }
        if (pending_periods >= MAX_PENDING_PERIOD) {
            submit_requests(pendings, ec);
            if (ec) {
                level_output(_LERROR_, "mgr #%d cannot send: %s\n", my_mgr_index, ec.message().c_str());
                continue;
            } else {
                pending_periods = 0;
            }
        }
    }

    server_mgr->unregister_sock_mgr(my_mgr_index);
}

void req_socket_manager::listen() {
    std::thread thr([this]() -> void {
        boost::system::error_code ec;

        while (!terminated) {
            asio::streambuf sbuf;
            asio::read_until(*psock, sbuf, "\n", ec);

            if (ec) {
                level_output(
                    _LERROR_, "<Server %2d> Got error %s\n", server_mgr->get_leader_id(), ec.message().c_str());
                if ((ec == asio::error::bad_descriptor || ec == asio::error::eof) && !terminated) {
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
                if (is_empty(line)) {
                    continue;
                }
                process_reply(line, timestamp);
            }
        }
    });
    thr.detach();
}

void req_socket_manager::process_reply(std::string reply, uint64_t timestamp) {
    int rid, server_id = server_mgr->get_leader_id();
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
            level_output(_LERROR_, "<Server %2d> request #%d: NOT_LEADER without reporting leader. \n", server_id, rid);
            return;
        } else {
            int leader_id = reply_data["leader"];
            if (leader_id > 0 && leader_id != server_mgr->get_leader_id()) {
                int leader = server_mgr->get_index(leader_id);
                mutex.lock();
                for (auto& pair: status) {
                    if (pair.second == R_PENDING) set_status(pair.first, R_RETRY);
                }
                mutex.unlock();
                server_mgr->set_leader(leader);
            } else
                set_status(rid, R_RETRY);
        }
        break;
    default:
        set_status(rid, R_ERROR);
        break;
    }
}

void req_socket_manager::notify() { connection_waiter.unlock(); }

inline void req_socket_manager::set_status(int rid, req_status status_) {
    mutex.lock();
    status[rid] = status_;
    mutex.unlock();
}

void req_socket_manager::submit_request(int rid, boost::system::error_code& ec) {
    const std::string msg = requests[rid].to_json_str();
    set_status(rid, R_PENDING);
    uint64_t timestamp = now_();
    psock->write_some(asio::buffer(msg, msg.length()), ec);
    if (!ec) depart->writeline(json({{"index", rid}, {"time", timestamp}}).dump());
}

void req_socket_manager::submit_requests(std::vector<int>& rids, boost::system::error_code& ec) {
    mutex.lock();
    std::string msg;
    for (int rid: rids) {
        msg += requests[rid].to_json_str();
        set_status(rid, R_PENDING);
    }
    uint64_t timestamp = now_();
    psock->write_some(asio::buffer(msg, msg.length()), ec);
    if (!ec) {
        for (int rid: rids)
            depart->writeline(json({{"index", rid}, {"time", timestamp}}).dump());
    }
    mutex.unlock();
}

void req_socket_manager::submit_all_requests(boost::system::error_code& ec) {
    mutex.lock();
    std::string msg;
    for (auto& pair: requests) {
        msg += pair.second.to_json_str();
        set_status(pair.first, R_PENDING);
    }
    uint64_t timestamp = now_();
    psock->write_some(asio::buffer(msg, msg.length()), ec);
    if (!ec) depart->writeline(json({{"index_start", start}, {"index_end", end}, {"time", timestamp}}).dump());
    mutex.unlock();
}

const int req_socket_manager::seqno() { return my_mgr_index; }
