#ifndef _MAL_RAFT_SERVER_HXX_
#define _MAL_RAFT_SERVER_HXX_

#include "raft_server.hxx"

#include <set>

enum AttackType {
    SplitBrain,
};

class AttackConfigBase {
public:
    virtual ~AttackConfigBase() = default;
};

class SplitBrainConfig : public AttackConfigBase {
public:
    SplitBrainConfig(std::vector<std::set<int>> groups)
        : groups_(groups)
        , mal_leader_sigs_(groups.size(), nullptr) {}

    std::vector<std::set<int>> get_groups() const { return groups_; }

    std::vector<std::map<nuraft::ulong, nuraft::ptr<nuraft::buffer>>>&
    get_mal_hash_ptrs() {
        return mal_hash_ptrs_;
    }
    const std::vector<std::map<nuraft::ulong, nuraft::ptr<nuraft::buffer>>>&
    get_mal_hash_ptrs() const {
        return mal_hash_ptrs_;
    }

    std::vector<nuraft::ptr<nuraft::buffer>>& get_mal_leader_sigs_() {
        return mal_leader_sigs_;
    }
    const std::vector<nuraft::ptr<nuraft::buffer>>& get_mal_leader_sigs_() const {
        return mal_leader_sigs_;
    }

private:
    std::vector<std::set<int>> groups_;
    std::vector<std::map<nuraft::ulong, nuraft::ptr<nuraft::buffer>>> mal_hash_ptrs_;
    std::vector<nuraft::ptr<nuraft::buffer>> mal_leader_sigs_;
};

namespace nuraft {
class mal_raft_server : public raft_server {
public:
    mal_raft_server(context* ctx, const init_options& opt = init_options()): raft_server(ctx, opt) {}

    void start_attack(AttackType type, AttackConfigBase* config) {
        _malicious = true;
        _attack_types.insert(type);
        _attack_configs[type] = ptr<AttackConfigBase>(config);
        SplitBrainConfig* split_brain_config = dynamic_cast<SplitBrainConfig*>(config);
        switch (type) {
        case SplitBrain: {
            // Split Brain Attack: copy the current hash cache for all groups. This node
            // will use the new hash caches only.
            std::unique_lock<std::mutex> guard(hash_cache_lock_);
            for (size_t i = 0; i < split_brain_config->get_groups().size(); i++) {
                split_brain_config->get_mal_hash_ptrs().push_back(
                    std::map<ulong, ptr<buffer>>());
                for (auto key: hash_cache_) {
                    split_brain_config->get_mal_hash_ptrs()[i][key.first] = key.second;
                }
            }
            break;
        }
        }
    }

    void stop_attack(AttackType type) {
        _attack_types.erase(type);
        _attack_configs.erase(type);
        if (_attack_types.empty()) {
            _malicious = false;
        }
    }

private:
    bool _malicious = false;
    std::set<AttackType> _attack_types;
    std::map<AttackType, ptr<AttackConfigBase>> _attack_configs;

protected:
    ptr<req_msg> create_append_entries_req(peer& p) override;
    ulong store_log_entry(ptr<log_entry>& entry, ulong index) override;
    void incr_payload(buffer& buf);
    void handle_append_entries_resp(resp_msg& resp) override;
};
} // namespace nuraft

#endif
