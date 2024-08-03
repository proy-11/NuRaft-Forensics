#include "context.hxx"
#include "raft_server.hxx"
#include "tracer.hxx"

#include <boost/filesystem.hpp>
#include <chrono>
#include <fstream>
#include <sstream>

namespace fs = boost::filesystem;

namespace nuraft {
bool raft_server::save_leader_cert(ptr<leader_certificate> lc) {
    std::string filename;
    std::string dir = ctx_->get_params()->forensics_output_path_;
    {
        std::lock_guard<std::mutex> guard(election_list_lock_);
        if (!boost::filesystem::exists(dir)) {
            boost::filesystem::create_directory(dir);
        }
        filename =
            get_election_list_file_name(dir); // file name should be unique by using
                                              // timestamp, server id (and also the lock)
    }

    std::ofstream file(filename, std::ios::binary | std::ios::app);
    if (!file.is_open()) {
        p_er("cannot open file %s for saving election list", filename.c_str());
        return false;
    }
    uint8_t size_t_size = sizeof(size_t);

    file.write(reinterpret_cast<const char*>(&size_t_size),
            1); // Write system size_t size to file first
    auto term = lc->get_term();
    file.write(reinterpret_cast<const char*>(&term), sizeof(ulong));
    ptr<buffer> serialized_lc = lc->serialize();
    size_t len = serialized_lc->size();
    file.write(reinterpret_cast<const char*>(&len), sizeof(size_t));
    file.write(reinterpret_cast<const char*>(serialized_lc->data_begin()), len);

    file.close();
    return true;
}

std::string raft_server::get_election_list_file_name(const std::string& data_dir) {
    std::stringstream filename;
    filename << "el_" << init_timestamp_ << "_p" << get_id() << ".dat";

    return ((boost::filesystem::path)data_dir / filename.str()).string();
}

std::string raft_server::get_leader_sig_file_name(const std::string& data_dir) {
    std::stringstream filename;
    filename << "ls_" << init_timestamp_ << "_p" << get_id() << ".dat";

    return ((boost::filesystem::path)data_dir / filename.str()).string();
}

std::string raft_server::get_commit_cert_file_name(const std::string& data_dir) {
    std::stringstream filename;
    filename << "cc_" << init_timestamp_ << "_p" << get_id() << ".dat";

    return ((boost::filesystem::path)data_dir / filename.str()).string();
}
} // namespace nuraft