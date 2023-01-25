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

#include "srv_config.hxx"
#include "openssl_ecdsa.hxx"

namespace nuraft {

ptr<srv_config> srv_config::deserialize(buffer& buf) {
    buffer_serializer bs(buf);
    return deserialize(bs);
}

ptr<srv_config> srv_config::deserialize(buffer_serializer& bs) {
    int32 id = bs.get_i32();
    int32 dc_id = bs.get_i32();
    const char* endpoint_char = bs.get_cstr();
    const char* aux_char = bs.get_cstr();
    std::string endpoint((endpoint_char) ? endpoint_char : std::string());
    std::string aux((aux_char) ? aux_char : std::string());
    byte is_learner = bs.get_u8();
    int32 priority = bs.get_i32();

    // FMARK: read pubkey
    size_t keysize = bs.get_i64();
    ptr<pubkey_intf> pubkey;
    if (keysize > 0) {
        ptr<buffer> keybuf = buffer::alloc(keysize);
        bs.get_buffer(keybuf);
        pubkey = std::make_shared<pubkey_t>(*keybuf);
    }
    return cs_new<srv_config>(id, dc_id, endpoint, aux, is_learner, priority, pubkey);
}

ptr<buffer> srv_config::serialize() const {
    // FMARK: save public key
    size_t total_size = sz_int + sz_int + (endpoint_.length() + 1) + (aux_.length() + 1) + 1 + sz_int + sz_ulong;
    ptr<buffer> keybuf = nullptr;
    if (public_key_ != nullptr) {
        keybuf = public_key_->tobuf();
        if(!keybuf) {
            return nullptr;
        }
        total_size += keybuf->size();
    }
    ptr<buffer> buf = buffer::alloc(total_size);
    if(!buf) {
        return nullptr;
    }
    buf->put(id_);
    buf->put(dc_id_);
    buf->put(endpoint_);
    buf->put(aux_);
    buf->put((byte)(learner_ ? (1) : (0)));
    buf->put(priority_);
    if (public_key_ != nullptr) {
        buf->put((ulong)keybuf->size());
        buf->put(*keybuf);
    } else {
        buf->put((ulong)0);
    }
    buf->pos(0);
    return buf;
}

} // namespace nuraft
