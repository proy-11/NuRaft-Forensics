// FMARK:

#ifndef _CERT_HXX_
#define _CERT_HXX_

#include "buffer.hxx"
#include <mutex>
#include <unordered_map>
#include <vector>

namespace nuraft {
class certificate {
public:
    certificate(int num_servers = 0, ulong term = 0, ulong index = 0, int quorum_ratio_reciprocal = 2)
        : num_servers_(num_servers)
        , term_(term)
        , index_(index)
        , quorum_ratio_reciprocal_ (quorum_ratio_reciprocal) {}
    ~certificate() {}

    ptr<certificate> clone() {
        std::lock_guard<std::mutex> guard(mutex_);
        auto new_cert = cs_new<certificate>(num_servers_, term_, index_);
        for (auto& it: signatures_) {
            new_cert->insert(it.first, buffer::clone(*it.second));
        }
        return new_cert;
    }

    void clear() {
        std::lock_guard<std::mutex> guard(mutex_);
        signatures_.clear();
    }

    bool insert(int32 id, ptr<buffer> buf) {
        std::lock_guard<std::mutex> guard(mutex_);
        signatures_[id] = (buf);
        return quorum_ratio_reciprocal_ == 1 ? (int32)signatures_.size() == num_servers_ : quorum_ratio_reciprocal_ * (int32)signatures_.size() > num_servers_;
    }

    inline int32 get_num_servers() { return num_servers_; }

    inline ulong get_term() { return term_; }

    inline ulong get_index() { return index_; }

    inline std::unordered_map<int32, ptr<buffer>> get_sigs() { return signatures_; }

    inline void set_signatures(std::unordered_map<int32, ptr<buffer>> sigs) {
        std::lock_guard<std::mutex> guard(mutex_);
        signatures_ = sigs;
    }

    ptr<buffer> serialize() {
        std::lock_guard<std::mutex> guard(mutex_);
        size_t total_size = sizeof(int32) + 2 * sizeof(ulong) + sizeof(int32);
        for (auto& pair: signatures_) {
            total_size += sizeof(int32) + sizeof(int32) + pair.second->size();
        }

        ptr<buffer> buf = buffer::alloc(total_size);
        buf->put(num_servers_);
        buf->put(term_);
        buf->put(index_);
        buf->put((int32)signatures_.size());

        for (auto& pair: signatures_) {
            buf->put(pair.first);
            buf->put(pair.second->data(), pair.second->size());
        }

        buf->pos(0);
        return buf;
    }

    static ptr<certificate> deserialize(buffer& buf) {
        int32 num_servers = buf.get_int();
        ulong term = buf.get_ulong();
        ulong index = buf.get_ulong();
        size_t nsig = (size_t)buf.get_int();

        ptr<certificate> cert = cs_new<certificate>(num_servers, term, index);

        for (size_t i = 0; i < nsig; i++) {
            int32 id = buf.get_int();
            size_t sig_len;
            const byte* sig_raw = buf.get_bytes(sig_len);
            ptr<buffer> sig = buffer::alloc(sig_len);
            sig->put_raw(sig_raw, sig_len);
            sig->pos(0);
            cert->insert(id, sig);
        }

        return cert;
    }

private:
    int32 num_servers_;
    ulong term_;
    ulong index_;
    std::unordered_map<int32, ptr<buffer>> signatures_;

    std::mutex mutex_;
    int quorum_ratio_reciprocal_;
};

} // namespace nuraft

#endif
