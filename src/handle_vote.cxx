/************************************************************************
Modifications Copyright 2017-2019 eBay Inc.
Author/Developer(s): Jung-Sang Ahn

Original Copyright:
See URL: https://github.com/datatechnology/cornerstone

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    https://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
**************************************************************************/

#include "raft_server.hxx"

#include "cluster_config.hxx"
#include "event_awaiter.h"
#include "handle_custom_notification.hxx"
#include "leader_certificate.hxx"
#include "peer.hxx"
#include "state_mgr.hxx"
#include "tracer.hxx"

#include <cassert>
#include <sstream>

namespace nuraft {

bool raft_server::check_cond_for_zp_election() {
    ptr<raft_params> params = ctx_->get_params();
    if (params->allow_temporary_zero_priority_leader_ && target_priority_ == 1
        && my_priority_ == 0
        && priority_change_timer_.get_ms()
               > (uint64_t)params->heart_beat_interval_ * 20) {
        return true;
    }
    return false;
}

void raft_server::request_prevote() {
    ptr<raft_params> params = ctx_->get_params();
    ptr<cluster_config> c_config = get_config();
    for (peer_itor it = peers_.begin(); it != peers_.end(); ++it) {
        ptr<peer> pp = it->second;
        if (!is_regular_member(pp)) continue;
        ptr<srv_config> s_config = c_config->get_server(pp->get_id());

        if (s_config) {
            bool recreate = false;
            if (hb_alive_) {
                // First pre-vote request: reset RPC client for all peers.
                recreate = true;

            } else {
                // Since second time: reset only if `rpc_` is null.
                recreate = pp->need_to_reconnect();

                // Or if it is not active long time, reconnect as well.
                int32 last_active_time_ms = pp->get_active_timer_us() / 1000;
                if (last_active_time_ms
                    > params->heart_beat_interval_
                          * raft_server::raft_limits_.reconnect_limit_) {
                    p_wn("connection to peer %d is not active long time: %zu ms, "
                         "need reconnection for prevote",
                         pp->get_id(),
                         last_active_time_ms);
                    recreate = true;
                }
            }

            if (recreate) {
                p_in("reset RPC client for peer %d", s_config->get_id());
                pp->recreate_rpc(s_config, *ctx_);
            }
        }
    }

    int quorum_size = get_quorum_for_election();
    if (pre_vote_.live_ + pre_vote_.dead_ > 0) {
        if (pre_vote_.live_ + pre_vote_.dead_ < quorum_size + 1) {
            // Pre-vote failed due to non-responding voters.
            pre_vote_.failure_count_++;
            p_wn("total %d nodes (including this node) responded for pre-vote "
                 "(term %zu, live %d, dead %d), at least %d nodes should "
                 "respond. failure count %d",
                 pre_vote_.live_.load() + pre_vote_.dead_.load(),
                 pre_vote_.term_,
                 pre_vote_.live_.load(),
                 pre_vote_.dead_.load(),
                 quorum_size + 1,
                 pre_vote_.failure_count_.load());
        } else {
            pre_vote_.failure_count_ = 0;
        }
    }
    int num_voting_members = get_num_voting_members();
    if (params->auto_adjust_quorum_for_small_cluster_ && num_voting_members == 2
        && pre_vote_.failure_count_ > raft_server::raft_limits_.vote_limit_) {
        // 2-node cluster's pre-vote failed due to offline node.
        p_wn("2-node cluster's pre-vote is failing long time, "
             "adjust quorum to 1");
        ptr<raft_params> clone = cs_new<raft_params>(*params);
        clone->custom_commit_quorum_size_ = 1;
        clone->custom_election_quorum_size_ = 1;
        ctx_->set_params(clone);
    }

    hb_alive_ = false;
    leader_ = -1;
    pre_vote_.reset(state_->get_term());
    // Count for myself.
    pre_vote_.dead_++;

    if (my_priority_ < target_priority_) {
        if (check_cond_for_zp_election()) {
            p_in("[PRIORITY] temporarily allow election for zero-priority member");
        } else {
            p_in("[PRIORITY] will not initiate pre-vote due to priority: "
                 "target %d, mine %d",
                 target_priority_,
                 my_priority_);
            restart_election_timer();
            return;
        }
    }

    p_in("[PRE-VOTE INIT] my id %d, my role %s, term %ld, log idx %ld, "
         "log term %ld, priority (target %d / mine %d)\n",
         id_,
         srv_role_to_string(role_).c_str(),
         state_->get_term(),
         log_store_->next_slot() - 1,
         term_for_log(log_store_->next_slot() - 1),
         target_priority_,
         my_priority_);

    for (peer_itor it = peers_.begin(); it != peers_.end(); ++it) {
        ptr<peer> pp = it->second;
        if (!is_regular_member(pp)) {
            // Do not send voting request to learner.
            continue;
        }

        ptr<req_msg> req(cs_new<req_msg>(state_->get_term(),
                                         msg_type::pre_vote_request,
                                         id_,
                                         pp->get_id(),
                                         term_for_log(log_store_->next_slot() - 1),
                                         log_store_->next_slot() - 1,
                                         quick_commit_index_.load()));
        if (pp->make_busy()) {
            pp->send_req(pp, req, resp_handler_);
        } else {
            p_wn("failed to send prevote request: peer %d (%s) is busy",
                 pp->get_id(),
                 pp->get_endpoint().c_str());
        }
    }
}

void raft_server::initiate_vote(bool force_vote) {
    p_tr("Voting initiated");
    int grace_period = ctx_->get_params()->grace_period_of_lagging_state_machine_;
    ulong cur_term = state_->get_term();
    if (!force_vote && grace_period && sm_commit_index_ < lagging_sm_target_index_) {
        p_in("grace period option is enabled, and state machine needs catch-up: "
             "%lu vs. %lu",
             sm_commit_index_.load(),
             lagging_sm_target_index_.load());
        if (vote_init_timer_term_ != cur_term) {
            p_in("grace period: %d, term increment detected %lu vs. %lu, reset timer",
                 grace_period,
                 vote_init_timer_term_.load(),
                 cur_term);
            vote_init_timer_.set_duration_ms(grace_period);
            vote_init_timer_.reset();
            vote_init_timer_term_ = cur_term;
        }

        if (vote_init_timer_term_ == cur_term && !vote_init_timer_.timeout()) {
            // Grace period, do not initiate vote.
            p_in("grace period: %d, term %lu, waited %lu ms, skip initiating vote",
                 grace_period,
                 cur_term,
                 vote_init_timer_.get_ms());
            return;

        } else {
            p_in("grace period: %d, no new leader detected for term %lu for %lu ms",
                 grace_period,
                 cur_term,
                 vote_init_timer_.get_ms());
        }
    }

    if (my_priority_ >= target_priority_ || force_vote || check_cond_for_zp_election()
        || get_quorum_for_election() == 0) {
        // Request vote when
        //  1) my priority satisfies the target, OR
        //  2) I'm the only node in the group.
        ulong last_term = state_->get_term();
        state_->inc_term();
        state_->set_voted_for(-1);
        role_ = srv_role::candidate;
        votes_granted_ = 0;
        votes_responded_ = 0;
        election_completed_ = false;
        ctx_->state_mgr_->save_state(*state_);
        request_vote(force_vote);
    }

    if (role_ != srv_role::leader) {
        hb_alive_ = false;
        leader_ = -1;
    }
}

void raft_server::request_vote(bool force_vote) {
    state_->set_voted_for(id_);
    ctx_->state_mgr_->save_state(*state_);
    votes_granted_ += 1;
    votes_responded_ += 1;
    // FMARK: reset leader certificate for this new election
    new_leader_certificate();

    p_in("[VOTE INIT] my id %d, my role %s, term %ld, log idx %ld, "
         "log term %ld, priority (target %d / mine %d)\n",
         id_,
         srv_role_to_string(role_).c_str(),
         state_->get_term(),
         log_store_->next_slot() - 1,
         term_for_log(log_store_->next_slot() - 1),
         target_priority_,
         my_priority_);

    // is this the only server?
    if (votes_granted_ > get_quorum_for_election()) {
        // TODO: No LC saved for this situation.
        election_completed_ = true;
        become_leader();
        return;
    }

    for (peer_itor it = peers_.begin(); it != peers_.end(); ++it) {
        ptr<peer> pp = it->second;
        if (!is_regular_member(pp)) {
            // Do not send voting request to learner.
            continue;
        }
        ptr<req_msg> req = cs_new<req_msg>(state_->get_term(),
                                           msg_type::request_vote_request,
                                           id_,
                                           pp->get_id(),
                                           term_for_log(log_store_->next_slot() - 1),
                                           log_store_->next_slot() - 1,
                                           quick_commit_index_.load());
        if (force_vote) {
            // Add a special log entry to let receivers ignore the priority.

            // Force vote message, and wrap it using log_entry.
            ptr<force_vote_msg> fv_msg = cs_new<force_vote_msg>();
            ptr<log_entry> fv_msg_le =
                cs_new<log_entry>(0, fv_msg->serialize(), log_val_type::custom);

            // Ship it.
            req->log_entries().push_back(fv_msg_le);
        }

        if (it == peers_.begin()) {
            // FMARK: set request field of leader_cert_ to the serialized vote request
            // (only saved once)
            leader_cert_->set_request(req->serialize());
        }

        p_db("send %s to server %d with term %llu",
             msg_type_to_string(req->get_type()).c_str(),
             it->second->get_id(),
             state_->get_term());
        if (pp->make_busy()) {
            pp->send_req(pp, req, resp_handler_);
        } else {
            p_wn("failed to send vote request: peer %d (%s) is busy",
                 pp->get_id(),
                 pp->get_endpoint().c_str());
        }
    }
}

ptr<resp_msg> raft_server::handle_vote_req(req_msg& req) {
    p_in("[VOTE REQ] my role %s, from peer %d, log term: req %ld / mine %ld\n"
         "last idx: req %ld / mine %ld, term: req %ld / mine %ld\n"
         "priority: target %d / mine %d, voted_for %d",
         srv_role_to_string(role_).c_str(),
         req.get_src(),
         req.get_last_log_term(),
         log_store_->last_entry()->get_term(),
         req.get_last_log_idx(),
         log_store_->next_slot() - 1,
         req.get_term(),
         state_->get_term(),
         target_priority_,
         my_priority_,
         state_->get_voted_for());

    ptr<resp_msg> resp(cs_new<resp_msg>(
        state_->get_term(), msg_type::request_vote_response, id_, req.get_src()));

    bool log_okay = req.get_last_log_term() > log_store_->last_entry()->get_term()
                    || (req.get_last_log_term() == log_store_->last_entry()->get_term()
                        && log_store_->next_slot() - 1 <= req.get_last_log_idx());

    bool grant =
        req.get_term() == state_->get_term() && log_okay
        && (state_->get_voted_for() == req.get_src() || state_->get_voted_for() == -1);

    bool force_vote = (req.log_entries().size() > 0);
    if (force_vote) {
        p_in("[VOTE REQ] force vote request, will ignore priority");
    }

    if (grant) {
        ptr<cluster_config> c_conf = get_config();
        for (auto& entry: c_conf->get_servers()) {
            srv_config* s_conf = entry.get();
            if (!force_vote && s_conf->get_id() == req.get_src() && s_conf->get_priority()
                && s_conf->get_priority() < target_priority_) {
                // NOTE:
                //   If zero-priority member initiates leader election,
                //   that is intentionally triggered by the flag in
                //   `raft_params`. In such case, we don't check the
                //   priority.
                p_in("I (%d) could vote for peer %d, "
                     "but priority %d is lower than %d",
                     id_,
                     s_conf->get_id(),
                     s_conf->get_priority(),
                     target_priority_);
                p_in("decision: X (deny)\n");
                return resp;
            }
        }

        p_in("decision: O (grant), voted_for %d, term %zu",
             req.get_src(),
             resp->get_term());
        resp->accept(log_store_->next_slot());
        // FMARK: set signature field of resp to my signature of the vote request
        resp->set_signature(get_signature(*req.serialize()), 0);
        state_->set_voted_for(req.get_src());
        ctx_->state_mgr_->save_state(*state_);
    } else {
        p_in("decision: X (deny), term %zu", resp->get_term());
    }

    return resp;
}

void raft_server::handle_vote_resp(resp_msg& resp) {
    if (election_completed_) {
        p_in("Election completed, will ignore the voting result from this server");
        return;
    }

    if (resp.get_term() != state_->get_term()) {
        // Vote response for other term. Should ignore it.
        p_in("[VOTE RESP] from peer %d, my role %s, "
             "but different resp term %zu. ignore it.",
             resp.get_src(),
             srv_role_to_string(role_).c_str(),
             resp.get_term());
        return;
    }
    votes_responded_ += 1;

    if (resp.get_accepted()) {
        // FMARK: Save the signature in the vote response to my leader certificate.
        if (!peers_[resp.get_src()]->verify_signature(leader_cert_->get_request(),
                                                      resp.get_signature())) {
            p_er("Invalid vote response, signature mismatch. Vote from peer %d.",
                 resp.get_src());
            return;
        }
        votes_granted_ += 1;
        p_tr("Save signature from peer %d to my leader certificate", resp.get_src());
        leader_cert_->insert(resp.get_src(), resp.get_signature());
    }

    if (votes_responded_ >= get_num_voting_members()) {
        election_completed_ = true;
    }

    int32 election_quorum_size = get_quorum_for_election() + 1;

    p_in("[VOTE RESP] peer %d (%s), resp term %zu, my role %s, "
         "granted %d, responded %d, "
         "num voting members %d, quorum %d\n",
         resp.get_src(),
         (resp.get_accepted()) ? "O" : "X",
         resp.get_term(),
         srv_role_to_string(role_).c_str(),
         (int)votes_granted_,
         (int)votes_responded_,
         get_num_voting_members(),
         election_quorum_size);

    if (votes_granted_ >= election_quorum_size) {
        p_in("Server is elected as leader for term %zu", state_->get_term());
        election_completed_ = true;
        // FMARK: Broadcast Leader Certificate to all peers.
        become_leader();
        p_in("  === LEADER (term %zu) ===\n", state_->get_term());
    }
}

/**
 * @brief FMARK: send leader certificate to one peer
 *
 */
void raft_server::send_leader_certificate(int32 peer_id, ptr<leader_certificate> tmp_lc) {
    ptr<peer> pp = peers_[peer_id];
    send_leader_certificate(pp, tmp_lc);
}

void raft_server::send_leader_certificate(ptr<peer>& pp, ptr<leader_certificate> tmp_lc) {
    ptr<req_msg> req(cs_new<req_msg>(state_->get_term(),
                                     msg_type::broadcast_leader_certificate_request,
                                     id_,
                                     pp->get_id(),
                                     term_for_log(log_store_->next_slot() - 1),
                                     log_store_->next_slot() - 1,
                                     quick_commit_index_.load()));
    ptr<log_entry> lc_msg_le =
        cs_new<log_entry>(0, tmp_lc->serialize(), log_val_type::custom);
    req->log_entries().push_back(lc_msg_le);
    // FMARK: do not set the peer busy. Send LC casually.
    pp->send_req(pp, req, resp_handler_);
    // if (pp->make_busy()) {
    //     pp->send_req(pp, req, resp_handler_);
    // } else {
    //     p_wn("failed to send leader certificate: peer %d (%s) is busy",
    //          pp->get_id(),
    //          pp->get_endpoint().c_str());
    // }
}

/**
 * @brief FMARK: Send Leader Certificate to all peers.
 *
 */
void raft_server::broadcast_leader_certificate() {
    ulong term_ = state_->get_term();
    p_in("Save the LC locally");
    ptr<leader_certificate> tmp_lc = leader_cert_->clone();

    {
        std::lock_guard<std::mutex> guard(election_list_lock_);
        election_list_[term_] = tmp_lc;
    }

    save_verified_term(term_, get_id());

    if (flag_save_election_list()) {
        if (save_and_clean_election_list(1))
            p_in("Election list saved, memory cleaned");
    }

    p_in("Broadcast Leader Certificates to all other peers");
    for (peer_itor it = peers_.begin(); it != peers_.end(); ++it) {
        send_leader_certificate(it->first, tmp_lc);
    }
}

/**
 * @brief FMARK: verify and save leader certificate
 *
 */

bool raft_server::verify_and_save_leader_certificate(req_msg& req,
                                                     ptr<buffer> lc_buffer) {
    ulong term_ = req.get_term();
    ptr<leader_certificate> lc = leader_certificate::deserialize(*lc_buffer);
    // ulong lc_term = req_msg::deserialize(*lc->get_request())->get_term();

    if (lc->get_request() == nullptr) {
        p_wn("Empty leader certificate request, can still be valid if no election was "
             "held for this term");
    } else {

        ptr<req_msg> lc_req = req_msg::deserialize(*lc->get_request());
        ulong lc_term = lc_req->get_term();
        p_tr("Received leader certificate request from peer %d, term %ld, last log term "
             "%ld, last log index %ld, commit index %ld",
             req.get_src(),
             lc_term,
             lc_req->get_last_log_term(),
             lc_req->get_last_log_idx(),
             lc_req->get_commit_idx());

        if (lc_term < state_->get_term()) {
            p_er("Invalid leader certificate request, term mismatch. LC from elected "
                 "server has term %d. My term %ld.",
                 lc_term,
                 state_->get_term());
            return false;
        }

        term_ = lc_term;

        // FMARK: verify all signatures
        std::unordered_map<int32, ptr<buffer>> sigs = lc->get_sigs();
        for (auto& sig: sigs) {
            if (sig.first == id_) continue;
            if (peers_.find(sig.first) == peers_.end()) {
                p_er("Received signature from non-existing peer %d, accept the "
                     "signature...",
                     sig.first);
                continue;
            }
            if (!peers_.find(sig.first)->second->verify_signature(lc->get_request(),
                                                                  sig.second)) {
                p_er("Invalid leader certificate request, signature mismatch. LC from "
                     "elected server %d. Mismatched signature from peer %d.",
                     req.get_src(),
                     sig.first);
                return false;
            }
        }
    }

    p_tr("Saving the leader certificate from peer %d to my election list", req.get_src());
    {
        std::lock_guard<std::mutex> guard(election_list_lock_);
        election_list_[term_] = lc;
    }

    save_verified_term(term_, req.get_src());

    if (flag_save_election_list()) {
        p_tr("Saving the election list to file");
        if (save_and_clean_election_list(1))
            p_in("Election list saved, memory cleaned");
    }

    return true;
}

/**
 * @brief FMARK: handle leader certificate request (broadcast), empty response
 *
 */
ptr<resp_msg> raft_server::handle_leader_certificate_request(req_msg& req) {
    p_in("[LEADER CERTIFICATE REQ] from peer %d, my role %s, log term: req %ld / mine "
         "%ld"
         "last idx: req %ld / mine %ld, term: req %ld / mine %ld\n",
         req.get_src(),
         srv_role_to_string(role_).c_str(),
         req.get_last_log_term(),
         log_store_->last_entry()->get_term(),
         req.get_last_log_idx(),
         log_store_->next_slot() - 1,
         req.get_term(),
         state_->get_term());

    ptr<resp_msg> resp(cs_new<resp_msg>(state_->get_term(),
                                        msg_type::broadcast_leader_certificate_response,
                                        id_,
                                        req.get_src()));

    if (!flag_use_election_list()) {
        p_in("I'm not using election list, ignore the leader certificate");
        return resp;
    }

    if (term_verified(req.get_term(), -1)) {
        p_wn("Leader certificate for term %ld has been verified and saved, replacing the "
             "old LC",
             req.get_term());
        return resp;
    }

    if (state_->get_voted_for() == -1) {
        p_in("I'm voting for no peer, accept the leader certificate from peer %d",
             req.get_src());
    }

    if (req.log_entries().size() != 1) {
        p_er("Invalid leader certificate request, no log entry");
        return resp;
    }

    ptr<buffer> lc_buffer = req.log_entries().at(0)->get_buf_ptr();
    if (!verify_and_save_leader_certificate(req, lc_buffer)) return resp;

    resp->accept(0);
    return resp;
}

void raft_server::handle_leader_certificate_resp(resp_msg& resp) {
    if (resp.get_accepted()) {
        p_in("Leader certificate request accepted by peer %d, now retrying to append "
             "entries",
             resp.get_src());
        request_append_entries(peers_[resp.get_src()]);
    }
}

ptr<resp_msg> raft_server::handle_prevote_req(req_msg& req) {
    ulong next_idx_for_resp = 0;
    auto entry = peers_.find(req.get_src());
    if (entry == peers_.end()) {
        // This node already has been removed, set a special value.
        next_idx_for_resp = std::numeric_limits<ulong>::max();
    }

    p_in("[PRE-VOTE REQ] my role %s, from peer %d, log term: req %ld / mine %ld\n"
         "last idx: req %ld / mine %ld, term: req %ld / mine %ld\n"
         "%s",
         srv_role_to_string(role_).c_str(),
         req.get_src(),
         req.get_last_log_term(),
         log_store_->last_entry()->get_term(),
         req.get_last_log_idx(),
         log_store_->next_slot() - 1,
         req.get_term(),
         state_->get_term(),
         (hb_alive_) ? "HB alive" : "HB dead");

    ptr<resp_msg> resp(cs_new<resp_msg>(req.get_term(),
                                        msg_type::pre_vote_response,
                                        id_,
                                        req.get_src(),
                                        next_idx_for_resp));

    // NOTE:
    //   While `catching_up_` flag is on, this server does not get
    //   normal append_entries request so that `hb_alive_` may not
    //   be cleared properly. Hence, it should accept any pre-vote
    //   requests.
    if (catching_up_) {
        p_in("this server is catching up, always accept pre-vote");
    }
    if (!hb_alive_ || catching_up_) {
        p_in("pre-vote decision: O (grant)");
        resp->accept(log_store_->next_slot());
    } else {
        if (next_idx_for_resp != std::numeric_limits<ulong>::max()) {
            p_in("pre-vote decision: X (deny)");
        } else {
            p_in("pre-vote decision: XX (strong deny, non-existing node)");
        }
    }

    return resp;
}

void raft_server::handle_prevote_resp(resp_msg& resp) {
    if (resp.get_term() != pre_vote_.term_) {
        // Vote response for other term. Should ignore it.
        p_in("[PRE-VOTE RESP] from peer %d, my role %s, "
             "but different resp term %zu (pre-vote term %zu). "
             "ignore it.",
             resp.get_src(),
             srv_role_to_string(role_).c_str(),
             resp.get_term(),
             pre_vote_.term_);
        return;
    }

    if (resp.get_accepted()) {
        // Accept: means that this peer is not receiving HB.
        pre_vote_.dead_++;
    } else {
        if (resp.get_next_idx() != std::numeric_limits<ulong>::max()) {
            // Deny: means that this peer still sees leader.
            pre_vote_.live_++;
        } else {
            // `next_idx_for_resp == MAX`, it is a special signal
            // indicating that this node has been already removed.
            pre_vote_.abandoned_++;
        }
    }

    int32 election_quorum_size = get_quorum_for_election() + 1;

    p_in("[PRE-VOTE RESP] peer %d (%s), term %zu, resp term %zu, "
         "my role %s, dead %d, live %d, "
         "num voting members %d, quorum %d\n",
         resp.get_src(),
         (resp.get_accepted()) ? "O" : "X",
         pre_vote_.term_,
         resp.get_term(),
         srv_role_to_string(role_).c_str(),
         pre_vote_.dead_.load(),
         pre_vote_.live_.load(),
         get_num_voting_members(),
         election_quorum_size);

    if (pre_vote_.dead_ >= election_quorum_size) {
        p_in("[PRE-VOTE DONE] SUCCESS, term %zu", pre_vote_.term_);

        bool exp = false;
        bool val = true;
        if (pre_vote_.done_.compare_exchange_strong(exp, val)) {
            p_in("[PRE-VOTE DONE] initiate actual vote");

            // Immediately initiate actual vote.
            initiate_vote();

            // restart the election timer if this is not yet a leader
            if (role_ != srv_role::leader) {
                restart_election_timer();
            }

        } else {
            p_in("[PRE-VOTE DONE] actual vote is already initiated, do nothing");
        }
    }

    if (pre_vote_.live_ >= election_quorum_size) {
        pre_vote_.quorum_reject_count_.fetch_add(1);
        p_wn("[PRE-VOTE] rejected by quorum, count %zu",
             pre_vote_.quorum_reject_count_.load());
        if (pre_vote_.quorum_reject_count_
            >= raft_server::raft_limits_.pre_vote_rejection_limit_) {
            p_ft("too many pre-vote rejections, probably this node is not "
                 "receiving heartbeat from leader. "
                 "we should re-establish the network connection");
            send_reconnect_request();
        }
    }

    if (pre_vote_.abandoned_ >= election_quorum_size) {
        p_er("[PRE-VOTE DONE] this node has been removed, stepping down");
        steps_to_down_ = 2;
    }
}

bool raft_server::term_verified(ulong term, int32 server_id) {
    if (verified_terms_.find(term) != verified_terms_.end()) {
        if (server_id == -1 || verified_terms_[term] == server_id) {
            return true;
        }
    }
    return false;
}

void raft_server::save_verified_term(ulong term, int32 server_id) {
    if (term < state_->get_term()) {
        p_wn("term %ld is already expired, ignore it", term);
        return;
    }
    if (verified_terms_.find(term) != verified_terms_.end()) {
        p_wn("term %ld is already verified by another server %d, replacing it",
             term,
             verified_terms_[term]);
    }
    verified_terms_[term] = server_id;
}

} // namespace nuraft
