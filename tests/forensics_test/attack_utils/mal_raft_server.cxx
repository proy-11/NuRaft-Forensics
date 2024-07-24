/************************************************************************
 *
 * This file contains the implementation of the malicious raft server.
 * Update log:
 *  - 2024/7: Split Brain Attack implemented. Ronghao Ni <
 *
 **************************************************************************/

#include "mal_raft_server.hxx"

#include "cluster_config.hxx"
#include "context.hxx"
#include "error_code.hxx"
#include "global_mgr.hxx"
#include "handle_client_request.hxx"
#include "handle_custom_notification.hxx"
#include "openssl_ecdsa.hxx"
#include "peer.hxx"
#include "snapshot.hxx"
#include "stat_mgr.hxx"
#include "state_machine.hxx"
#include "state_mgr.hxx"
#include "tracer.hxx"

#include <cassert>
#include <fstream>
#include <iostream>
#include <limits>
#include <random>
#include <sstream>
#include <thread>

namespace nuraft {

// MAL_RAFT_SERVER: this malicious create_append_entries_req function is based on
// commit fa4f549353199f0d6009c8779c126a4a19751b3e
/**
 * @brief MAL_RAFT_SERVER: the following attacks are implemented in this function:
 *
 *      - Split Brain Attack: this server sends the unmodified log entries to the first
 *      group and sends modified malicious log entries to the second group. The modified
 *      payloads are the original payloads (treated as an int) plus 1. Accordingly, this
 *      server keeps track of two hash pointer caches.
 *
 */
ptr<req_msg> mal_raft_server::create_append_entries_req(peer& p) {
    ulong cur_nxt_idx(0L);
    ulong commit_idx(0L);
    ulong last_log_idx(0L);
    ulong term(0L);
    ulong starting_idx(1L);

    starting_idx = log_store_->start_index();
    cur_nxt_idx = precommit_index_ + 1;
    commit_idx = quick_commit_index_;
    term = state_->get_term();
    p_in("Current next index of peer #%d: %llu (%llu)",
         p.get_id(),
         cur_nxt_idx,
         p.get_next_log_idx());

    std::map<ulong, ptr<buffer>>* hash_ptr_cache_to_use_ = nullptr;
    bool to_modify_entries_payload = false;
    int group_idx = -1;
    if (_malicious) {
        for (auto attack: _attack_types) {
            switch (attack) {
            case SplitBrain: {
                SplitBrainConfig* split_brain_config =
                    dynamic_cast<SplitBrainConfig*>(_attack_configs[SplitBrain].get());
                auto groups = split_brain_config->get_groups();
                // check which group is p in
                for (size_t i = 0; i < groups.size(); ++i) {
                    if (groups[i].find(p.get_id()) != groups[i].end()) {
                        group_idx = i;
                        break;
                    }
                }
                if (group_idx == -1) {
                    p_er("Peer #%d is not in any group. Split Brain Attack configuration "
                         "error.",
                         p.get_id());
                    ctx_->state_mgr_->system_exit(raft_err::N9_receive_unknown_request);
                }
                p_in("Split Brain Attack: sending entries to peer %d of group %d",
                     p.get_id(),
                     group_idx);
                hash_ptr_cache_to_use_ =
                    &split_brain_config->get_mal_hash_ptrs()[group_idx];
                if (group_idx != 0) {
                    to_modify_entries_payload = true;
                }
            }
            }
        }
    }

    if (hash_ptr_cache_to_use_ == nullptr) {
        // in case of benign, use the original hash cache
        hash_ptr_cache_to_use_ = &hash_cache_;
    }

    {
        std::lock_guard<std::mutex> guard(p.get_lock());
        if (p.get_next_log_idx() == 0L) {
            p_in("Set peer #%d's lastLogIndex to %llu", p.get_id(), cur_nxt_idx);
            p.set_next_log_idx(cur_nxt_idx);
        }

        last_log_idx = p.get_next_log_idx() - 1;
    }

    if (last_log_idx >= cur_nxt_idx) {
        // LCOV_EXCL_START
        p_er("Peer #%d's lastLogIndex is too large %llu v.s. %llu, ",
             p.get_id(),
             last_log_idx,
             cur_nxt_idx);
        ctx_->state_mgr_->system_exit(raft_err::N8_peer_last_log_idx_too_large);
        ::exit(-1);
        return ptr<req_msg>();
        // LCOV_EXCL_STOP
    }

    // cur_nxt_idx: last log index of myself (leader).
    // starting_idx: start log index of myself (leader).
    // last_log_idx: last log index of replica (follower).
    // end_idx: if (cur_nxt_idx - last_log_idx) > max_append_size, limit it.

    p_tr("last_log_idx: %d, starting_idx: %d, cur_nxt_idx: %d\n",
         last_log_idx,
         starting_idx,
         cur_nxt_idx);

    // Verify log index range.
    bool entries_valid = (last_log_idx + 1 >= starting_idx);

    // Read log entries. The underlying log store may have removed some log entries
    // causing some of the requested entries to be unavailable. The log store should
    // return nullptr to indicate such errors.
    ulong end_idx =
        std::min(cur_nxt_idx, last_log_idx + 1 + ctx_->get_params()->max_append_size_);

    // NOTE: If this is a retry, probably the follower is down.
    //       Send just one log until it comes back
    //       (i.e., max_append_size_ = 1).
    //       Only when end_idx - start_idx > 1, and 5th try.
    ulong peer_last_sent_idx = p.get_last_sent_idx();
    if (last_log_idx + 1 == peer_last_sent_idx && last_log_idx + 2 < end_idx) {
        int32 cur_cnt = p.inc_cnt_not_applied();
        p_db("last sent log (%zu) to peer %d is not applied, cnt %d",
             peer_last_sent_idx,
             p.get_id(),
             cur_cnt);
        if (cur_cnt >= 5) {
            ulong prev_end_idx = end_idx;
            end_idx = std::min(cur_nxt_idx, last_log_idx + 1 + 1);
            p_db("reduce end_idx %zu -> %zu", prev_end_idx, end_idx);
        }
    } else {
        p.reset_cnt_not_applied();
    }

    ptr<std::vector<ptr<log_entry>>> log_entries;
    if ((last_log_idx + 1) >= cur_nxt_idx) {
        log_entries = ptr<std::vector<ptr<log_entry>>>();
    } else if (entries_valid) {
        log_entries = log_store_->log_entries_ext(
            last_log_idx + 1, end_idx, p.get_next_batch_size_hint_in_bytes());
        if (log_entries == nullptr) {
            p_wn("failed to retrieve log entries: %zu - %zu", last_log_idx + 1, end_idx);
            entries_valid = false;
        }
    }

    if (!entries_valid) {
        // Required log entries are missing. First, we try to use snapshot to recover.
        // To avoid inconsistency due to smart pointer, should have local varaible
        // to increase its ref count.
        ptr<snapshot> snp_local = get_last_snapshot();

        // Modified by Jung-Sang Ahn (Oct 11 2017):
        // As `reserved_log` has been newly added, need to check snapshot
        // in addition to `starting_idx`.
        if (snp_local && last_log_idx < starting_idx
            && last_log_idx < snp_local->get_last_log_idx()) {
            p_db("send snapshot peer %d, peer log idx: %zu, my starting idx: %zu, "
                 "my log idx: %zu, last_snapshot_log_idx: %zu\n",
                 p.get_id(),
                 last_log_idx,
                 starting_idx,
                 cur_nxt_idx,
                 snp_local->get_last_log_idx());
            return create_sync_snapshot_req(p, last_log_idx, term, commit_idx);
        }

        // Cannot recover using snapshot. Return here to protect the leader.
        static timer_helper msg_timer(5000000);
        int log_lv = msg_timer.timeout_and_reset() ? L_ERROR : L_TRACE;
        p_lv(log_lv,
             "neither snapshot nor log exists, peer %d, last log %zu, "
             "leader's start log %zu",
             p.get_id(),
             last_log_idx,
             starting_idx);

        // Send out-of-log-range notification to this follower.
        ptr<req_msg> req = cs_new<req_msg>(term,
                                           msg_type::custom_notification_request,
                                           id_,
                                           p.get_id(),
                                           0,
                                           last_log_idx,
                                           commit_idx);

        // Out-of-log message.
        ptr<out_of_log_msg> ool_msg = cs_new<out_of_log_msg>();
        ool_msg->start_idx_of_leader_ = starting_idx;

        // Create a notification containing OOL message.
        ptr<custom_notification_msg> custom_noti = cs_new<custom_notification_msg>(
            custom_notification_msg::out_of_log_range_warning);
        custom_noti->ctx_ = ool_msg->serialize();

        // Wrap it using log_entry.
        ptr<log_entry> custom_noti_le =
            cs_new<log_entry>(0, custom_noti->serialize(), log_val_type::custom);

        req->log_entries().push_back(custom_noti_le);
        return req;
    }

    // increment payloads by 1 if sent to malicious groups
    if (to_modify_entries_payload && log_entries) {
        for (auto& le: *log_entries) {
            if (le->get_val_type() == log_val_type::app_log) incr_payload(le->get_buf());
        }
    }

    ulong last_log_term = term_for_log(last_log_idx);
    ulong adjusted_end_idx = end_idx;
    if (log_entries) adjusted_end_idx = last_log_idx + 1 + log_entries->size();
    if (adjusted_end_idx != end_idx) {
        p_tr("adjusted end_idx due to batch size hint: %zu -> %zu",
             end_idx,
             adjusted_end_idx);
    }

    ptr<buffer> hash_ptr_buf = nullptr;
    {
        std::unique_lock<std::mutex> guard(hash_cache_lock_);
        if (hash_ptr_cache_to_use_->find(adjusted_end_idx - 1)
                != hash_ptr_cache_to_use_->end()
            && hash_ptr_cache_to_use_->at(adjusted_end_idx - 1) != nullptr) {
            hash_ptr_buf =
                buffer::clone(*hash_ptr_cache_to_use_->at(adjusted_end_idx - 1));
            p_in("hash pointer (%s) found for idx %zu",
                 tobase64(*hash_ptr_buf).c_str(),
                 adjusted_end_idx - 1);
        } else {
            p_in("hash pointer not found for idx %zu", adjusted_end_idx - 1);
        }
    }

    p_db("append_entries for %d with LastLogIndex=%llu, "
         "LastLogTerm=%llu, EntriesLength=%d, CommitIndex=%llu, "
         "Term=%llu, peer_last_sent_idx %zu",
         p.get_id(),
         last_log_idx,
         last_log_term,
         (log_entries ? log_entries->size() : 0),
         commit_idx,
         term,
         peer_last_sent_idx);
    if (last_log_idx + 1 == adjusted_end_idx) {
        p_tr("EMPTY PAYLOAD");
    } else if (last_log_idx + 1 + 1 == adjusted_end_idx) {
        p_db("idx: %zu", last_log_idx + 1);
    } else {
        p_db("idx range: %zu-%zu", last_log_idx + 1, adjusted_end_idx - 1);
    }

    ptr<req_msg> req(cs_new<req_msg>(term,
                                     msg_type::append_entries_request,
                                     id_,
                                     p.get_id(),
                                     last_log_term,
                                     last_log_idx,
                                     commit_idx));
    std::vector<ptr<log_entry>>& v = req->log_entries();
    if (log_entries) {
        // FMARK: RN: use the first log entry to transmit hash pointer
        if (flag_use_ptr() && hash_ptr_buf != nullptr) {
            ptr<log_entry> ptr_msg_le =
                cs_new<log_entry>(0, hash_ptr_buf, log_val_type::hash_ptr);
            v.push_back(ptr_msg_le);
            p_in("hash pointer (%s) sent to peer %d",
                 tobase64(*hash_ptr_buf).c_str(),
                 p.get_id());
        }
    }

    // FMARK: RN: new leader sig
    if (log_entries && flag_use_leader_sig()) {
        // FMARK: RN: shared the leader signatures of the last committed log entry instead
        // of the last shared log entry
        auto entry_to_sign = log_entries->rbegin();
        p_in("log_entries size %zu", log_entries->size());
        while (entry_to_sign != log_entries->rend()) {
            if ((*entry_to_sign)->get_val_type() == log_val_type::app_log) {
                // insert the leader signature for the last app_log entry into v
                auto entry_serialized = (*entry_to_sign)->serialize_sig();
                auto sig = this->get_signature(*entry_serialized);
                ptr<log_entry> leader_sig_le =
                    cs_new<log_entry>(0, sig, log_val_type::leader_sig);
                v.push_back(leader_sig_le);
                p_in("leader signature on log %zu sent to peer %d", adjusted_end_idx - (log_entries->rend() - entry_to_sign), p.get_id());
                dump_leader_signatures(adjusted_end_idx - (log_entries->rend() - entry_to_sign), last_log_term, sig);
                break;
            }
            // Process the entry and then move to the previous one
            ++entry_to_sign;
        }
        if (v.empty() || v.back()->get_val_type() != log_val_type::leader_sig) {
            p_in("leader signature not sent to peer %d", p.get_id());
        }
    }

    if (log_entries) {
        v.insert(v.end(), log_entries->begin(), log_entries->end());
        // FMARK: TODO: correctness
        if (flag_use_cc() && commit_cert_) {
            // ptr<timer_t> timer = cs_new<timer_t>();
            // timer->start_timer();
            ptr<certificate> clone_cert_;
            {
                std::lock_guard<std::mutex> guard(cert_lock_);
                // in case of malicious, only share the signatures from the nodes of the
                // same group
                if (_malicious && _attack_types.find(SplitBrain) != _attack_types.end()) {
                    auto group_members =
                        dynamic_cast<SplitBrainConfig*>(_attack_configs[SplitBrain].get())
                            ->get_groups()[group_idx];
                    for (auto& it: commit_cert_->get_sigs()) {
                        if (it.first == id_
                            || group_members.find(it.first) != group_members.end())
                            clone_cert_->insert(it.first, buffer::clone(*it.second));
                    }
                } else {
                    clone_cert_ = commit_cert_->clone();
                }
            }
            p_db("[CC] attached of term %zu, index %zu",
                 clone_cert_->get_term(),
                 clone_cert_->get_index());

            req->set_certificate(clone_cert_);
            // timer->add_record("cc.attach");
            // t_->add_sess(timer);
            dump_commit_cert(role_, commit_cert_);
        }
    }
    p.set_last_sent_idx(last_log_idx + 1);

    return req;
}

ulong mal_raft_server::store_log_entry(ptr<log_entry>& entry, ulong index) {
    ulong log_index = index;

    if (index == 0) {
        log_index = log_store_->append(entry);
    } else {
        log_store_->write_at(log_index, entry);
    }

    {
        std::unique_lock<std::mutex> lock(hash_cache_lock_);
        if (!(_malicious && _attack_types.find(SplitBrain) != _attack_types.end())) {
            if (hash_cache_.find(log_index) == hash_cache_.end()) {
                // not in cache
                ptr<buffer> prev_hash = nullptr;
                if (hash_cache_.find(log_index - 1) != hash_cache_.end()) {
                    // previous hash is in cachef
                    prev_hash = hash_cache_[log_index - 1];
                }
                ptr<buffer> hash = nullptr;
                if (entry->get_val_type() == log_val_type::app_log) {
                    hash = create_hash(entry, prev_hash, log_index);
                    hash_cache_[log_index] = hash;
                } else
                    hash_cache_[log_index] = prev_hash;
                p_in("hash cache updated for log index %zu -> %s",
                     log_index,
                     (hash != nullptr) ? tobase64(*hash).c_str() : "null");
            }
        } else {
            // update all hash pointer caches for attacks
            SplitBrainConfig* split_brain_config =
                dynamic_cast<SplitBrainConfig*>(_attack_configs[SplitBrain].get());
            auto& mal_hash_ptrs = split_brain_config->get_mal_hash_ptrs();
            for (size_t i = 0; i < mal_hash_ptrs.size(); ++i) {
                auto& cache_to_use = mal_hash_ptrs[i];
                ptr<log_entry> le_to_use = entry;
                if (i != 0) {
                    // use malicious payload
                    auto mal_payload = buffer::clone(le_to_use->get_buf());
                    incr_payload(*mal_payload);
                    le_to_use = cs_new<log_entry>(
                        le_to_use->get_term(), mal_payload, le_to_use->get_val_type());
                }

                if (cache_to_use.find(log_index) == cache_to_use.end()) {
                    // not in cache
                    ptr<buffer> prev_hash = nullptr;
                    if (cache_to_use.find(log_index - 1) != cache_to_use.end()) {
                        // previous hash is in cachef
                        prev_hash = cache_to_use[log_index - 1];
                    }
                    ptr<buffer> hash = nullptr;
                    if (le_to_use->get_val_type() == log_val_type::app_log) {
                        hash = create_hash(le_to_use, prev_hash, log_index);
                        cache_to_use[log_index] = hash;
                    } else
                        cache_to_use[log_index] = prev_hash;
                    p_in("hash cache updated for log index %zu -> %s",
                         log_index,
                         (hash != nullptr) ? tobase64(*hash).c_str() : "null");
                }
            }
        }
    }

    if (entry->get_val_type() == log_val_type::conf) {
        // Force persistence of config_change logs to guarantee the durability of
        // cluster membership change log entries.  Losing cluster membership log
        // entries may lead to split brain.
        if (!log_store_->flush()) {
            // LCOV_EXCL_START
            p_ft("log store flush failed");
            ctx_->state_mgr_->system_exit(N21_log_flush_failed);
            // LCOV_EXCL_STOP
        }

        if (role_ == srv_role::leader) {
            // Need to progress precommit index for config.
            try_update_precommit_index(log_index);
        }
    }

    return log_index;
}

void mal_raft_server::incr_payload(buffer& buf) {
    byte* data = buf.data_begin();
    // treat data as an int and increment by 1
    int* intData = reinterpret_cast<int*>(data);
    (*intData)++;
    p_in("Payload incremented to %d", *reinterpret_cast<int*>(buf.data_begin()));
}

void mal_raft_server::handle_append_entries_resp(resp_msg& resp) {
    peer_itor it = peers_.find(resp.get_src());
    if (it == peers_.end()) {
        p_in("the response is from an unknown peer %d", resp.get_src());
        return;
    }

    check_srv_to_leave_timeout();
    if (srv_to_leave_ && srv_to_leave_->get_id() == resp.get_src()
        && srv_to_leave_->is_stepping_down()
        && resp.get_next_idx() > srv_to_leave_target_idx_) {
        // Catch-up is done.
        p_in("server to be removed %d fully caught up the "
             "target config log %zu",
             srv_to_leave_->get_id(),
             srv_to_leave_target_idx_);
        remove_peer_from_peers(srv_to_leave_);
        reset_srv_to_leave();
        return;
    }

    // If there are pending logs to be synced or commit index need to be advanced,
    // continue to send appendEntries to this peer
    bool need_to_catchup = true;

    ptr<peer> p = it->second;
    p_tr("handle append entries resp (from %d), resp.get_next_idx(): %zu\n",
         (int)p->get_id(),
         resp.get_next_idx());

    int64 bs_hint = resp.get_next_batch_size_hint_in_bytes();
    p_tr("peer %d batch size hint: %ld bytes", p->get_id(), bs_hint);
    p->set_next_batch_size_hint_in_bytes(bs_hint);

    if (resp.get_accepted()) {
        uint64_t prev_matched_idx = 0;
        uint64_t new_matched_idx = 0;
        {
            std::lock_guard<std::mutex>(p->get_lock());
            p->set_next_log_idx(resp.get_next_idx());
            prev_matched_idx = p->get_matched_idx();
            new_matched_idx = resp.get_next_idx() - 1;
            p_tr("peer %d, prev matched idx: %ld, new matched idx: %ld",
                 p->get_id(),
                 prev_matched_idx,
                 new_matched_idx);
            p->set_matched_idx(new_matched_idx);
            // p->set_last_accepted_log_idx(new_matched_idx);
        }
        cb_func::Param param(id_, leader_, p->get_id());
        param.ctx = &new_matched_idx;
        CbReturnCode rc =
            ctx_->cb_func_.call(cb_func::GotAppendEntryRespFromPeer, &param);
        (void)rc;

        // FMARK: verify signature
        // TODO: there needs an accurate predicate to tell whether a signature is expected
        if (!resp.get_signature()) {
            p_in("resp get signature is null");
        }
        if (flag_use_cc() && resp.get_signature() != nullptr) {
            // auto timer = cs_new<timer_t>();
            // timer->start_timer();

            ptr<log_entry> entry = log_store_->entry_at(resp.get_sig_index());
            if (_malicious && _attack_types.find(SplitBrain) != _attack_types.end()) {
                int group_idx = -1;
                SplitBrainConfig* split_brain_config =
                    dynamic_cast<SplitBrainConfig*>(_attack_configs[SplitBrain].get());
                auto groups = split_brain_config->get_groups();
                for (size_t i = 0; i < groups.size(); ++i) {
                    if (groups[i].find(resp.get_src()) != groups[i].end()) {
                        group_idx = i;
                        break;
                    }
                }
                if (group_idx == -1) {
                    p_er("Peer #%d is not in any group. Split Brain Attack configuration "
                         "error.",
                         resp.get_src());
                }
                if (group_idx != 0) {
                    // maliciously modify the payload
                    auto mal_payload = buffer::clone(entry->get_buf());
                    incr_payload(*mal_payload);
                    entry = cs_new<log_entry>(entry->get_term(), mal_payload);
                }
            }
            if (!p->verify_signature(entry->serialize_sig(), resp.get_signature())) {
                p_er("peer #%d's signature on entry %d is invalid: %s",
                     p->get_id(),
                     resp.get_sig_index(),
                     resp.get_signature() == nullptr
                         ? "null"
                         : tobase64(*resp.get_signature()).c_str());
                return;
            }
            p_in("Pushing new cert signature");
            push_new_cert_signature(
                resp.get_signature(),
                p->get_id(),
                entry->get_term(),
                resp.get_sig_index(),
                (_malicious && _attack_types.find(SplitBrain) != _attack_types.end())
                    ? 1
                    : 2);

            // timer->add_record("cc.push");
            // t_->add_sess(timer);
        }

        // Try to commit with this response.
        // if (flag_use_cc()) {
        //     cert_lock_.lock();
        // }
        ulong committed_index = get_expected_committed_log_idx();
        commit(committed_index);
        // if (flag_use_cc()) {
        //     cert_lock_.unlock();
        // }
        need_to_catchup =
            p->clear_pending_commit() || resp.get_next_idx() < log_store_->next_slot();
    } else {
        std::lock_guard<std::mutex> guard(p->get_lock());
        ulong prev_next_log = p->get_next_log_idx();
        if (resp.get_next_idx() > 0 && prev_next_log > resp.get_next_idx()) {
            // fast move for the peer to catch up
            p->set_next_log_idx(resp.get_next_idx());
        } else {
            // if not, move one log backward.
            // WARNING: Make sure that `next_log_idx_` shouldn't be smaller than 0.
            if (prev_next_log) {
                p->set_next_log_idx(prev_next_log - 1);
            }
        }
        bool suppress = p->need_to_suppress_error();

        // To avoid verbose logs here.
        static timer_helper log_timer(500 * 1000, true);
        int log_lv = suppress ? L_INFO : L_WARN;
        if (log_lv == L_WARN) {
            if (!log_timer.timeout_and_reset()) {
                log_lv = L_TRACE;
            }
        }
        p_lv(log_lv,
             "declined append: peer %d, prev next log idx %zu, "
             "resp next %zu, new next log idx %zu",
             p->get_id(),
             prev_next_log,
             resp.get_next_idx(),
             p->get_next_log_idx());
    }

    // NOTE:
    //   If all other followers are not responding, we may not make
    //   below condition true. In that case, we check the timeout of
    //   re-election timer in heartbeat handler, and do force resign.
    ulong p_matched_idx = p->get_matched_idx();
    if (write_paused_ && p->get_id() == next_leader_candidate_ && p_matched_idx
        && p_matched_idx == log_store_->next_slot() - 1 && p->make_busy()) {
        // NOTE:
        //   If `make_busy` fails (very unlikely to happen), next
        //   response handler (of heartbeat, append_entries ..) will
        //   retry this.
        p_in("ready to resign, server id %d, "
             "latest log index %zu, "
             "%zu us elapsed, resign now",
             next_leader_candidate_.load(),
             p_matched_idx,
             reelection_timer_.get_us());
        leader_ = -1;

        // To avoid this node becomes next leader again, set timeout
        // value bigger than any others, just once at this time.
        rand_timeout_ = [this]() -> int32 {
            return this->ctx_->get_params()->election_timeout_upper_bound_
                   + this->ctx_->get_params()->election_timeout_lower_bound_;
        };
        become_follower();
        update_rand_timeout();

        // Clear live flag to avoid pre-vote rejection.
        hb_alive_ = false;

        // Send leadership takeover request to this follower.
        ptr<req_msg> req = cs_new<req_msg>(state_->get_term(),
                                           msg_type::custom_notification_request,
                                           id_,
                                           p->get_id(),
                                           term_for_log(log_store_->next_slot() - 1),
                                           log_store_->next_slot() - 1,
                                           quick_commit_index_.load());

        // Create a notification.
        ptr<custom_notification_msg> custom_noti =
            cs_new<custom_notification_msg>(custom_notification_msg::leadership_takeover);

        // Wrap it using log_entry.
        ptr<log_entry> custom_noti_le =
            cs_new<log_entry>(0, custom_noti->serialize(), log_val_type::custom);

        req->log_entries().push_back(custom_noti_le);

        p->send_req(p, req, resp_handler_);
        return;
    }

    if (bs_hint < 0) {
        // If hint is a negative number, we should set `need_to_catchup`
        // to `false` to avoid sending meaningless messages continuously
        // which eats up CPU. Then the leader will send heartbeats only.
        need_to_catchup = false;
    }

    // This may not be a leader anymore,
    // such as the response was sent out long time ago
    // and the role was updated by UpdateTerm call
    // Try to match up the logs for this peer
    if (role_ == srv_role::leader) {
        if (need_to_catchup) {
            p_db("reqeust append entries need to catchup, p %d\n", (int)p->get_id());
            request_append_entries(p);
        }
        if (status_check_timer_.timeout_and_reset()) {
            check_overall_status();
        }
    }
}

} // namespace nuraft
