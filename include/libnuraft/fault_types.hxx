#ifndef _FAULT_TYPES_HXX_
#define _FAULT_TYPES_HXX_

namespace nuraft {
    enum class fault_type {
        none,
        sleep,
        kill_self,
        vote_monopoly,
        signal_false_commitments,
        // Leader
        ret_invalid_resp_to_client,
        drop_random_incoming_messages,
        corrupt_random_incoming_messages,
        delay_processing_of_incoming_messages,
        send_delayed_logs_to_followers,
        send_corrupt_logs_to_followers,
        send_invalid_request_to_followers,
        send_diff_requests_to_diff_followers,
        // Followers
        follower_returns_invalid_response,
    };
} // namespace nuraft

#endif // _FAULT_TYPES_HXX_