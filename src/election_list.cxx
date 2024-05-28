#include "context.hxx"
#include "raft_server.hxx"
#include "tracer.hxx"

#include <boost/filesystem.hpp>
#include <chrono>
#include <fstream>
#include <sstream>

namespace fs = boost::filesystem;

namespace nuraft {
bool raft_server::save_and_clean_election_list(ulong threshold) {
    std::unordered_map<ulong, ptr<leader_certificate>> temp_list;
    std::string filename;
    std::string dir = ctx_->get_params()->forensics_output_path_;
    {
        std::lock_guard<std::mutex> guard(election_list_lock_);
        if (election_list_.size() < threshold) {
            return false;
        }
        if (!boost::filesystem::exists(dir)) {
            boost::filesystem::create_directory(dir);
        }
        temp_list = std::move(election_list_);
        filename =
            get_election_list_file_name(dir); // file name should be unique by using
                                              // timestamp, server id (and also the lock)
    }

    // in case of writing failure, we will not be able to recover the election list
    std::ofstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        p_er("cannot open file %s for saving election list", filename.c_str());
        return false;
    }
    uint8_t size_t_size = sizeof(size_t);
    file.write(reinterpret_cast<const char*>(&size_t_size),
               1); // Write system size_t size to file first
    for (const auto& pair: temp_list) {
        file.write(reinterpret_cast<const char*>(&pair.first), sizeof(ulong));
        ptr<buffer> serialized_lc = pair.second->serialize();
        size_t len = serialized_lc->size();
        file.write(reinterpret_cast<const char*>(&len), sizeof(size_t));
        file.write(reinterpret_cast<const char*>(serialized_lc->data_begin()), len);
    }

    file.close();
    return true;
}

std::string raft_server::get_election_list_file_name(const std::string& data_dir) {
    auto now = std::chrono::system_clock::now();

    // Convert to a duration since the epoch in microseconds
    auto microsecondsSinceEpoch =
        std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch())
            .count();

    std::stringstream filename;
    filename << "el_" << microsecondsSinceEpoch << "_p" << get_id() << ".dat";

    return ((boost::filesystem::path)data_dir / filename.str()).string();
}
} // namespace nuraft