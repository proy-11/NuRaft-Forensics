#pragma once

#include "libnuraft/json.hpp"
#include "sync_file_obj.hxx"
#include "workload.hxx"

#include <arpa/inet.h>
#include <atomic>
#include <boost/thread.hpp>
#include <map>
#include <mutex>
#include <pthread.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

#ifndef F_REQ_SOCKET_MGR
#define F_REQ_SOCKET_MGR

#define MAX_PENDING_PERIOD 5

using nlohmann::json;

extern json meta_setting;

enum req_status {
    R_PENDING = 0,
    R_RETRY = 1,
    R_ERROR = 2,
    R_COMMITTED = 3,
};

class server_data_mgr;

class req_socket_manager : public std::enable_shared_from_this<req_socket_manager> {
public:
    req_socket_manager(std::vector<nuraft::request> requests_,
                       std::shared_ptr<sync_file_obj> arrive_,
                       std::shared_ptr<sync_file_obj> depart_,
                       std::shared_ptr<server_data_mgr> mgr_);

    ~req_socket_manager();

    void self_register();
    void self_connect();
    void terminate();
    void wait_retry();

    void auto_submit();
    void listen();

    void process_reply(std::string reply, uint64_t timestamp);

    void notify();

    void set_status(int rid, req_status status_);

    ssize_t submit_msg(std::string msg);
    bool submit_request(int rid);
    bool submit_requests(std::vector<int>& rids);
    bool submit_all_requests();

    const int seqno();

    // private:
    int my_mgr_index;
    int start;
    int end;
    int sock;
    int client_fd;
    int listener_tid;
    pthread_t listener_thread;
    std::atomic<bool> terminated;
    std::atomic<bool> ended_listening;
    std::unordered_map<int, req_status> status;
    std::map<int, nuraft::request> requests;
    std::recursive_mutex mutex;
    std::mutex connection_waiter;
    std::shared_ptr<sync_file_obj> arrive;
    std::shared_ptr<sync_file_obj> depart;
    std::shared_ptr<server_data_mgr> server_mgr;
};

#endif