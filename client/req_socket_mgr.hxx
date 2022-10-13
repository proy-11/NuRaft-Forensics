#pragma once

#include "libnuraft/json.hpp"
#include "sync_file_obj.hxx"
#include "workload.hxx"
#include <boost/asio.hpp>
#include <map>
#include <mutex>
#include <string>

#ifndef F_REQ_SOCKET_MGR
#define F_REQ_SOCKET_MGR

#define MAX_PENDING_PERIOD 5

namespace asio = boost::asio;
using boost::asio::ip::tcp;
using boost::system::error_code;
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
    void connect();
    void terminate();
    void wait_retry();

    void auto_submit();
    void listen();

    void process_reply(std::string reply, uint64_t timestamp);

    void notify();

    void set_status(int rid, req_status status_);

    void submit_request(int rid, boost::system::error_code& ec);
    void submit_requests(std::vector<int>& rids, boost::system::error_code& ec);
    void submit_all_requests(boost::system::error_code& ec);

    const int seqno();

private:
    int my_mgr_index;
    int start;
    int end;
    bool terminated;
    std::unordered_map<int, req_status> status;
    std::unordered_map<int, nuraft::request> requests;
    std::recursive_mutex mutex;
    std::mutex connection_waiter;
    std::unique_ptr<tcp::socket> psock;
    std::shared_ptr<sync_file_obj> arrive;
    std::shared_ptr<sync_file_obj> depart;
    std::shared_ptr<server_data_mgr> server_mgr;
};

#endif