#include "req_socket_mgr.hxx"
#include "server_data_mgr.hxx"
#include "utils.hxx"
#include <thread>

#define MAX_PENDING_PERIOD 5

req_socket_manager::req_socket_manager(std::vector<nuraft::request> requests_,
                                       std::shared_ptr<sync_file_obj> arrive_,
                                       std::shared_ptr<sync_file_obj> depart_,
                                       std::shared_ptr<server_data_mgr> mgr_)
    : my_mgr_index(-1)
    , sock(-1)
    , arrive(arrive_)
    , depart(depart_)
    , server_mgr(mgr_) {
    terminated = server_mgr->terminated;
    start = INT_MAX;
    end = -1;

    for (auto& req: requests_) {
        status[req.index] = R_RETRY;
        requests[req.index] = req;
        start = start < req.index ? start : req.index;
        end = end > req.index ? end : req.index;
    }

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        level_output(_LERROR_, "cannot create socket for reqs #%d -- #%d\n", start, end);
        exit(1);
    }
}

req_socket_manager::~req_socket_manager() { level_output(_LWARNING_, "destroying mgr #%d \n", my_mgr_index); }

void req_socket_manager::self_register() { my_mgr_index = server_mgr->register_sock_mgr(shared_from_this()); }

void req_socket_manager::self_connect() {
    connection_waiter.lock();

    int cfd = connect(sock, (sockaddr*)(server_mgr->get_leader_endpoint().get()), sizeof(sockaddr));
    if (cfd < 0) {
        level_output(
            _LERROR_, "Commander connection to %d error: %s\n", server_mgr->get_leader_id(), std::strerror(errno));
    }
    client_fd = cfd;
}

void req_socket_manager::terminate() {
    level_output(_LWARNING_, "trying to terminate mgr #%d \n", my_mgr_index);
    terminated = true;
    close(client_fd);
    close(sock);
}

inline void req_socket_manager::wait_retry() {
    std::this_thread::sleep_for(std::chrono::milliseconds(meta_setting["submit_retry_ms"]));
}

void req_socket_manager::auto_submit() {
    self_connect();
    auto listener = listen();

    while (!terminated) {
        if (!submit_all_requests()) {
            level_output(_LERROR_, "mgr #%d cannot send: %s\n", my_mgr_index, std::strerror(errno));
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
        if (!submit_requests(retries)) {
            level_output(_LERROR_, "mgr #%d cannot send: %s\n", my_mgr_index, std::strerror(errno));
            continue;
        } else {
            mutex.lock();
            for (auto& rid: retries) {
                set_status(rid, R_PENDING);
            }
            mutex.unlock();
        }
        if (pending_periods >= MAX_PENDING_PERIOD) {
            if (!submit_requests(pendings)) {
                level_output(_LERROR_, "mgr #%d cannot send: %s\n", my_mgr_index, std::strerror(errno));
                continue;
            } else {
                pending_periods = 0;
            }
        }
    }

    terminate();
    listener->join();
    server_mgr->unregister_sock_mgr(my_mgr_index);
}

std::shared_ptr<std::thread> req_socket_manager::listen() {
    return std::shared_ptr<std::thread>(new std::thread([this]() -> void {
        char buffer[BUF_SIZE] = {0};
        std::string line;
        while (!terminated) {
            ssize_t bytes_read = recv(sock, buffer, BUF_SIZE, 0);
            if (terminated) break;
            if (bytes_read < 0) {
                level_output(
                    _LERROR_, "<Server %2d> Got error %s\n", server_mgr->get_leader_id(), std::strerror(errno));

                self_connect();
                continue;
            }

            int start = 0;
            uint64_t timestamp = now_();
            for (int i = 0; i < bytes_read; i++) {
                if (buffer[i] == '\n') {
                    line += std::string(buffer + start, i - start);
                    if (!is_empty(line)) {
                        process_reply(line, timestamp);
                    }
                    start = i + 1;
                    line.clear();
                }
            }
            if (start != bytes_read) {
                line += std::string(buffer + start, bytes_read - start);
            }
        }
    }));
}

void req_socket_manager::process_reply(std::string reply, uint64_t timestamp) {
    if (terminated) return;

    int rid, server_id = server_mgr->get_leader_id();
    json reply_data;
    try {
        reply_data = json::parse(reply);
    } catch (json::exception& ec) {
        level_output(_LERROR_, "<Server %2d> Got invalid reply ~~ %s ~~\n", server_id, reply.c_str());
        return;
    }

    if (!reply_data.contains("rid")) {
        level_output(_LERROR_, "<Server %2d> No request id: %s\n", server_id, reply_data.dump().c_str());
        return;
    }

    rid = reply_data["rid"];
    if (reply_data["success"]) {
        set_status(rid, R_COMMITTED);
        arrive->writeline(json({{"index", rid}, {"time", timestamp}}).dump());
        return;
    }

    if (_ISSUBSTR_(std::string(reply_data["error"]), "request already committed")) {
        set_status(rid, R_COMMITTED);
        return;
    }

    level_output(
        _LERROR_, "<Server %2d> request #%d failed (%s)\n", server_id, rid, reply_data["error"].dump().c_str());

    if (_ISSUBSTR_(std::string(reply_data["error"]), "queue is full")) {
        set_status(rid, R_ERROR);
    }

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

ssize_t req_socket_manager::submit_msg(std::string msg) {
    ssize_t sent;
    ssize_t p = 0, total = msg.length();
    const char* cmsg = msg.c_str();
    while (p < total) {
        sent = send(sock, cmsg + p, total - p, 0);
        if (sent < 0) return sent;
        p += sent;
    }
    return p;
}

bool req_socket_manager::submit_request(int rid) {
    const std::string msg = requests[rid].to_json_str();
    set_status(rid, R_PENDING);
    uint64_t timestamp = now_();
    if (submit_msg(msg) < 0) {
        return false;
    } else {
        depart->writeline(json({{"index", rid}, {"time", timestamp}}).dump());
    }
    return true;
}

bool req_socket_manager::submit_requests(std::vector<int>& rids) {
    mutex.lock();
    std::string msg;
    for (int rid: rids) {
        msg += requests[rid].to_json_str();
        set_status(rid, R_PENDING);
    }
    uint64_t timestamp = now_();
    if (submit_msg(msg) < 0) {
        mutex.unlock();
        return false;
    } else {
        for (int rid: rids)
            depart->writeline(json({{"index", rid}, {"time", timestamp}}).dump());
        mutex.unlock();
        return true;
    }
}

bool req_socket_manager::submit_all_requests() {
    mutex.lock();
    std::string msg;
    for (auto& pair: requests) {
        msg += pair.second.to_json_str();
        set_status(pair.first, R_PENDING);
    }
    uint64_t timestamp = now_();
    if (submit_msg(msg) < 0) {
        mutex.unlock();
        return false;
    } else {
        depart->writeline(json({{"index_start", start}, {"index_end", end}, {"time", timestamp}}).dump());
        mutex.unlock();
        return true;
    }
}

const int req_socket_manager::seqno() { return my_mgr_index; }
