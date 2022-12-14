// FMARK:

#ifndef _CERT_HXX_
#define _CERT_HXX_

#include "buffer.hxx"
#include <unordered_map>
#include <vector>

namespace nuraft {
class certificate {
public:
    certificate()
        : num_servers_(0) {}
    certificate(buffer& buf) {
        num_servers_ = buf.get_int();
        term_ = buf.get_ulong();
        index_ = buf.get_ulong();
        size_t nsig = (size_t)buf.get_ulong();
        std::vector<int32> ids;
        std::vector<size_t> sizes;
        for (size_t i = 0; i < nsig; i++) {
            ids.emplace_back(buf.get_int());
            sizes.emplace_back((size_t)buf.get_ulong());
        }
        for (size_t i = 0; i < nsig; i++) {
            ptr<buffer> sig = buffer::alloc(sizes[i]);
            buf.get(sig);
            signatures_[ids[i]] = sig;
        }
    }
    ~certificate() {}

    void clear() { signatures_.clear(); }

    void insert(int32 id, ptr<buffer> buf) { signatures_[id] = buf; }

    ulong get_term() { return term_; }
    ulong get_index() { return index_; }

    ptr<buffer> serialize() {
        size_t total_size = sizeof(int32) + 2 * sizeof(ulong) + sizeof(size_t);
        for (auto& pair: signatures_) {
            total_size += sizeof(int32) + sizeof(size_t) + pair.second->size();
        }

        ptr<buffer> buf = buffer::alloc(total_size);
        buf->put(num_servers_);
        buf->put(term_);
        buf->put(index_);
        buf->put((ulong)signatures_.size());

        for (auto& pair: signatures_) {
            buf->put(pair.first);
            buf->put((ulong)pair.second->size());
        }

        for (auto& pair: signatures_) {
            buf->put(*pair.second);
        }
        buf->pos(0);
        return buf;
    }

private:
    int32 num_servers_;
    ulong term_;
    ulong index_;
    std::unordered_map<int32, ptr<buffer>> signatures_;
};

} // namespace nuraft

#endif
