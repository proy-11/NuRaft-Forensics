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

#ifndef _LOG_ENTRY_HXX_
#define _LOG_ENTRY_HXX_

#include "basic_types.hxx"
#include "buffer.hxx"
#include "log_val_type.hxx"
#include "ptr.hxx"

#ifdef _NO_EXCEPTION
#include <cassert>
#endif
#include <stdexcept>

namespace nuraft {

class log_entry {
public:
    log_entry(ulong term,
              const ptr<buffer>& buff,
              log_val_type value_type = log_val_type::app_log,
              const ptr<buffer>& prev = nullptr,
              const ptr<buffer>& leadersig = nullptr)
        : term_(term)
        , value_type_(value_type)
        , buff_(buff)
        , prev_(prev)
        , leader_sig_(leadersig) {}

    __nocopy__(log_entry);

public:
    ulong get_term() const { return term_; }

    void set_term(ulong term) { term_ = term; }

    // FMARK: set pointer
    void set_prev(const ptr<buffer> prev) { prev_ = buffer::clone(*prev); }

    // FMARK: set signature
    void set_signature(const ptr<buffer> sig) { leader_sig_ = buffer::clone(*sig); }

    log_val_type get_val_type() const { return value_type_; }

    bool is_buf_null() const { return (buff_.get()) ? false : true; }

    buffer& get_buf() const {
        // We accept nil buffer, but in that case,
        // the get_buf() shouldn't be called, throw runtime exception
        // instead of having segment fault (AV on Windows)
        if (!buff_) {
#ifndef _NO_EXCEPTION
            throw std::runtime_error("get_buf cannot be called for a log_entry "
                                     "with nil buffer");
#else
            assert(0);
#endif
        }

        return *buff_;
    }

    ptr<buffer> get_buf_ptr() const { return buff_; }

    ptr<buffer> get_prev_ptr() const { return prev_; }
    ptr<buffer> get_sig_ptr() const { return leader_sig_; }

    // FMARK: serialize for signature
    ptr<buffer> serialize_sig() {
//         if (!buff_) {
//             exit(0);
// #ifndef _NO_EXCEPTION
//             throw std::runtime_error("serialize_sig cannot be called for a log_entry "
//                                      "with nil buffer");
// #else
//             assert(0);
// #endif
//         }
        buff_->pos(0);
        ptr<buffer> buf =
            buffer::alloc(sizeof(ulong) + sizeof(char) + buff_->size() + (prev_ == nullptr ? 0 : prev_->size()));
        buf->put(term_);
        buf->put((static_cast<byte>(value_type_)));
        buf->put(*buff_);
        if (prev_) buf->put(*prev_);
        buf->pos(0);
        return buf;
    }

    // FMARK: serialize prev_pointer and signature
    ptr<buffer> serialize() {
        buff_->pos(0);
        ptr<buffer> buf = buffer::alloc(sizeof(ulong) + sizeof(char) + sizeof(ulong) + buff_->size() + sizeof(ulong)
                                        + (prev_ == nullptr ? 0 : prev_->size()) + sizeof(ulong)
                                        + (leader_sig_ == nullptr ? 0 : leader_sig_->size()));
        buf->put(term_);
        buf->put((static_cast<byte>(value_type_)));
        buf->put((ulong)buff_->size());
        buf->put(*buff_);
        buf->put(prev_ == nullptr ? (ulong)0 : (ulong)prev_->size());
        if (prev_) buf->put(*prev_);
        buf->put(leader_sig_ == nullptr ? (ulong)0 : (ulong)leader_sig_->size());
        if (leader_sig_) buf->put(*leader_sig_);
        buf->pos(0);
        return buf;
    }

    // FMARK: deserialize prev_pointer and signature
    static ptr<log_entry> deserialize(buffer& buf) {
        ulong term = buf.get_ulong();
        log_val_type t = static_cast<log_val_type>(buf.get_byte());
        ulong data_size = buf.get_ulong();
        ptr<buffer> data = buffer::alloc(data_size);
        buf.get(data);

        ulong prev_size = buf.get_ulong();
        ptr<buffer> prev = nullptr;
        if (prev_size) {
            prev = buffer::alloc(prev_size);
            buf.get(prev);
        }

        ulong sig_size = buf.get_ulong();
        ptr<buffer> sig = nullptr;
        if (sig_size) {
            sig = buffer::alloc(sig_size);
            buf.get(sig);
        }

        return cs_new<log_entry>(term, data, t, prev, sig);
    }

    static ulong term_in_buffer(buffer& buf) {
        ulong term = buf.get_ulong();
        buf.pos(0); // reset the position
        return term;
    }

private:
    ulong term_;
    log_val_type value_type_;
    ptr<buffer> buff_;
    ptr<buffer> prev_;
    ptr<buffer> leader_sig_;
};

} // namespace nuraft

#endif //_LOG_ENTRY_HXX_
