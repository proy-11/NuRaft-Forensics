#include "leader_certificate.hxx"

namespace nuraft {
ptr<buffer> leader_certificate::serialize() {
    ptr<buffer> buf = certificate::serialize();
    size_t request_size = request_ == nullptr ? 0 : request_->size();
    ptr<buffer> temp = buffer::alloc(buf->size() + sizeof(size_t) + request_size);
    temp->put_raw(buf->data(), buf->size());
    temp->put(request_ == nullptr ? nullptr : request_->data_begin(), request_size);
    temp->pos(0);
    return temp;
}

ptr<leader_certificate> leader_certificate::deserialize(buffer& buf) {
    ptr<certificate> cc = certificate::deserialize(buf);
    ptr<leader_certificate> lcc = cs_new<leader_certificate>(cc);
    size_t req_len;
    const byte* req_raw = buf.get_bytes(req_len);
    if (req_len != 0) {
        ptr<buffer> req = buffer::alloc(req_len);
        req->put_raw(req_raw, req_len);
        req->pos(0);
        lcc->set_request(req);
    }
    return lcc;
}
} // namespace nuraft