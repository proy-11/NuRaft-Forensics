#pragma once

#include "certificate.hxx" // Include the base class header
#include "buffer.hxx"

namespace nuraft {

/**
 * @brief FMARK
 * 
 */
class leader_certificate : public certificate {
public:
    leader_certificate(/* parameters */) 
        : certificate(0, 0, 0),
          request_(nullptr){
    }

    leader_certificate(const ptr<certificate>& cert)
        : certificate(cert->get_num_servers(), cert->get_term(), cert->get_index()),
          request_(nullptr){
            for (auto& it: cert->get_sigs()) {
                insert(it.first, buffer::clone(*it.second));
            }
    }

    ptr<buffer> get_request() {
        if (request_ == nullptr) {
            return nullptr;
        }
        request_->pos(0);
        return request_;
    }

    void set_request(ptr<buffer> req) {
        request_ = req;
    }

    ptr<leader_certificate> clone() {
        ptr<leader_certificate> new_cert = cs_new<leader_certificate>(certificate::clone());
        if (request_ != nullptr) {
            new_cert->set_request(buffer::clone(*request_));
        }
        return new_cert;
    }

    ptr<buffer> serialize();

    static ptr<leader_certificate> deserialize(buffer& buf);

private:
    ptr<buffer> request_;

};
} // namespace nuraft

