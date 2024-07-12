// +build linux

#include "openssl_ecdsa.hxx"
#include <iostream>
#include <map>

#define BASE64_ENC_SIZE(size) (4 * (((size) + 2) / 3))
#define BASE64_DEC_SIZE(size) (3 * (size) / 4)

using std::cout;

const EVP_MD* (*HASH_FN)(void) = &EVP_sha256;
const int CURVE_NID = NID_X9_62_prime256v1;

namespace nuraft {
EVP_PKEY_CTX* new_evp_pkey_ctx() {
    EVP_PKEY_CTX* kctx = NULL;

    if (!(kctx = EVP_PKEY_CTX_new_id(EVP_PKEY_EC, NULL))
        || EVP_PKEY_keygen_init(kctx) <= 0
        || EVP_PKEY_CTX_set_ec_paramgen_curve_nid(kctx, CURVE_NID) <= 0) {
        throw crypto_exception("new EVP_PKEY_CTX");
    }
    return kctx;
}

// ==============================================================================================================
// =================================================== PUBKEY
// ===================================================
// ==============================================================================================================

pubkey_t::pubkey_t(EVP_PKEY* key_) {
    BIO* bio = BIO_new(BIO_s_mem());
    PEM_write_bio_PrivateKey(bio, key_, NULL, NULL, 0, NULL, NULL);
    key = PEM_read_bio_PrivateKey(bio, NULL, NULL, NULL);
    if (bio) {
        BIO_free(bio);
    }
}

pubkey_t::pubkey_t(const buffer& keybuf) {
    if (!keybuf.data()) {
        return;
    }
    EVP_PKEY* temp = NULL;
    auto ctx = new_evp_pkey_ctx();
    if (ctx == NULL || EVP_PKEY_keygen(ctx, &temp) <= 0) {
        throw crypto_exception("pubkey init from ctx");
    }
    const unsigned char* keydata = keybuf.data();
    key = d2i_PublicKey(EVP_PKEY_EC, &temp, &keydata, keybuf.size());
    if (key == NULL) {
        throw crypto_exception("pubkey from buffer");
    }
    if (ctx != NULL) EVP_PKEY_CTX_free(ctx);
}

// pubkey_t::pubkey_t(const buffer& keybuf) {
//     EVP_PKEY* temp = NULL;
//     auto ctx = new_evp_pkey_ctx();
//     if (ctx == NULL || EVP_PKEY_keygen(ctx, &temp) <= 0) {
//         throw crypto_exception("pubkey init from ctx");
//     }
//     const unsigned char* keydata = keybuf.data();
//     key = d2i_PublicKey(EVP_PKEY_EC, &temp, &keydata, keybuf.size());
//     if (key == NULL) {
//         throw crypto_exception("pubkey from buffer");
//     }
//     if (ctx != NULL) EVP_PKEY_CTX_free(ctx);
// }

pubkey_t::~pubkey_t() {
    if (key) EVP_PKEY_free(key);
}

ptr<buffer> pubkey_t::tobuf() {
    if (!key) {
        return nullptr;
    }
    int size = i2d_PublicKey(key, NULL);
    if (!size) {
        return nullptr;
    }
    ptr<buffer> buf = buffer::alloc(size);
    unsigned char* data = buf->data();
    buf->pos(0);
    i2d_PublicKey(key, &data);
    return buf;
}

std::string pubkey_t::str() {
    int dsize = i2d_PublicKey(key, NULL);
    int csize = BASE64_ENC_SIZE(dsize);

    unsigned char* data = new unsigned char[dsize];
    char* encoded = new char[csize];

    dsize = i2d_PublicKey(key, &data);
    data -= dsize;

    csize = EVP_EncodeBlock((unsigned char*)encoded, (const unsigned char*)data, dsize);
    auto encstr = std::string(encoded, csize);

    delete[] data;
    delete[] encoded;
    return encstr;
}

bool pubkey_t::verify_md(const buffer& msg, const buffer& sig) {
    EVP_MD_CTX* mdctx = NULL;
    if (!(mdctx = EVP_MD_CTX_create())
        || 1 != EVP_DigestVerifyInit(mdctx, NULL, HASH_FN(), NULL, key)
        || 1 != EVP_DigestVerifyUpdate(mdctx, (const void*)msg.data(), msg.size())) {
        throw crypto_exception("verify_md");
    }
    int res = EVP_DigestVerifyFinal(mdctx, (const unsigned char*)sig.data(), sig.size());

    EVP_MD_CTX_destroy(mdctx);
    return (1 == res);
}

ptr<pubkey_t> pubkey_t::frombuf(const buffer& keybuf) {
    return std::make_shared<pubkey_t>(keybuf);
}

// ==============================================================================================================
// =================================================== SECKEY
// ===================================================
// ==============================================================================================================

seckey_t::seckey_t() {
    auto ctx = new_evp_pkey_ctx();
    key = NULL;
    if (ctx == NULL || EVP_PKEY_keygen(ctx, &key) <= 0) {
        throw crypto_exception("seckey random generation");
    }
    if (ctx) {
        EVP_PKEY_CTX_free(ctx);
    }
}

seckey_t::seckey_t(const buffer& keybuf) {
    unsigned char* data = keybuf.data();
    key = d2i_AutoPrivateKey(NULL, (const unsigned char**)&data, keybuf.size());
    if (key == NULL) {
        throw crypto_exception("seckey from buf");
    }
}

// seckey_t::seckey_t(const std::string& filename) {
//     FILE *fp = fopen(filename.c_str(), "r");
//     if (fp == NULL) {
//         throw crypto_exception("cannot read from file");
//         return;
//     }
//     fclose(fp);
//     BIO* bio = BIO_new_file(filename.c_str(), "r");
//     if(bio == NULL) {
//         throw crypto_exception("bio is null");
//     }
//     if (PEM_read_bio_PrivateKey(bio, &key, NULL, NULL) == NULL) {
//         throw crypto_exception("seckey from file");
//     }
//     if(bio) {
//         BIO_free(bio);
//     }
// }

seckey_t::seckey_t(std::string priv_key) {
    OpenSSL_add_all_algorithms();
    OpenSSL_add_all_ciphers();
    OpenSSL_add_all_digests();

    if (priv_key.empty()) {
        throw crypto_exception("private key string is empty");
    }
    BIO* bio = BIO_new_mem_buf((void*)priv_key.data(), -1);
    if (bio == NULL) {
        throw crypto_exception("bio is null");
    }
    key = PEM_read_bio_PrivateKey(bio, NULL, NULL, NULL);
    if (key == NULL) {
        bio = BIO_new_file(priv_key.c_str(), "r");
        if (bio == NULL || PEM_read_bio_PrivateKey(bio, &key, NULL, NULL) == NULL) {
            throw crypto_exception("seckey from string or file");
        }
    }

    if (bio) {
        BIO_free(bio);
    }
}

seckey_t::~seckey_t() {
    if (key) EVP_PKEY_free(key);
}

ptr<buffer> seckey_t::tobuf() {
    int size = i2d_PrivateKey(key, NULL);
    ptr<buffer> buf = buffer::alloc(size);
    for (int i = 0; i < size; i++) {
        buf->put((byte)0);
    }
    buf->pos(0);
    unsigned char* data = buf->data();
    i2d_PrivateKey(key, &data);
    return buf;
}

std::string seckey_t::str() {
    int dsize = i2d_PrivateKey(key, NULL);
    int csize = BASE64_ENC_SIZE(dsize);

    unsigned char* data = new unsigned char[dsize];
    char* encoded = new char[csize];

    dsize = i2d_PrivateKey(key, &data);
    data -= dsize;

    csize = EVP_EncodeBlock((unsigned char*)encoded, (const unsigned char*)data, dsize);
    auto encstr = std::string(encoded, csize);

    delete[] data;
    delete[] encoded;
    return encstr;
}

void seckey_t::tofile(const std::string& filename) {
    BIO* bio = BIO_new_file(filename.c_str(), "w");
    if (bio == NULL || !PEM_write_bio_PrivateKey(bio, key, NULL, NULL, 0, NULL, NULL)) {
        throw crypto_exception("seckey from file");
    }
    BIO_free(bio);
}

ptr<pubkey_intf> seckey_t::derive() {
    if (!key) {
        return nullptr;
    }
    ptr<pubkey_t> pubkey = std::make_shared<pubkey_t>(key);
    return pubkey;
}

ptr<buffer> seckey_t::sign_md(const buffer& msg) {
    EVP_MD_CTX* mdctx = NULL;
    size_t slen;

    if (!(mdctx = EVP_MD_CTX_create())
        || 1 != EVP_DigestSignInit(mdctx, NULL, HASH_FN(), NULL, key)
        || 1 != EVP_DigestSignUpdate(mdctx, (const void*)msg.data(), msg.size())
        || 1 != EVP_DigestSignFinal(mdctx, NULL, &slen)) {
        throw crypto_exception("sign_md");
    }

    // slen may again change in this step, so we must allocate a temp buffer
    auto temp = new unsigned char[slen];
    if (1 != EVP_DigestSignFinal(mdctx, temp, &slen)) {
        throw crypto_exception("sign_md");
    }

    auto sig = buffer::alloc(slen);
    sig->put_raw(temp, slen);
    sig->pos(0);

    delete[] temp;
    if (mdctx) EVP_MD_CTX_destroy(mdctx);

    return sig;
}

ptr<seckey_t> seckey_t::generate() { return std::make_shared<seckey_t>(); }

ptr<seckey_t> seckey_t::frombuf(const buffer& keybuf) {
    return std::make_shared<seckey_t>(keybuf);
}

ptr<seckey_t> seckey_t::fromfile(const std::string& filename) {
    return std::make_shared<seckey_t>(filename);
}

// ==============================================================================================================
// =================================================== UTILS
// ====================================================
// ==============================================================================================================

std::string tobase64(const buffer& buf) {
    char* encoded = new char[BASE64_ENC_SIZE(buf.size())];
    int size = EVP_EncodeBlock(
        (unsigned char*)encoded, (const unsigned char*)buf.data(), buf.size());
    auto encstr = std::string(encoded, size);
    delete[] encoded;
    return encstr;
}

ptr<buffer> create_hash(const char* source) {
    uint32_t digest_length = SHA256_DIGEST_LENGTH;
    ptr<buffer> digest = buffer::alloc(digest_length);
    EVP_MD_CTX* context = EVP_MD_CTX_new();
    EVP_DigestInit_ex(context, HASH_FN(), nullptr);
    EVP_DigestUpdate(context, source, strlen(source));
    EVP_DigestFinal_ex(context, digest->data(), &digest_length);
    EVP_MD_CTX_destroy(context);

    digest->pos(0);
    return digest;
}

ptr<buffer> create_hash(ptr<log_entry> le_, ulong height) {
    if (!le_) {
        return nullptr;
    }

    ptr<buffer> serial = le_->serialize_sig();
    size_t msgsize;
    if (serial == nullptr) {
        msgsize = sizeof(ulong);
    } else {
        msgsize = serial->size() + sizeof(ulong);
    }
    // size_t msgsize = serial->size() + sizeof(ulong);
    ptr<buffer> msg = buffer::alloc(msgsize);
    msg->put(height);
    if (serial != nullptr) {
        msg->put(*serial);
    } else {
        msg->put((byte)0);
    }
    // msg->put(*serial);
    msg->pos(0);

    uint32_t digest_length = SHA256_DIGEST_LENGTH;
    ptr<buffer> digest = buffer::alloc(digest_length);
    EVP_MD_CTX* context = EVP_MD_CTX_new();
    EVP_DigestInit_ex(context, HASH_FN(), nullptr);
    EVP_DigestUpdate(context, msg->data(), msg->size());
    EVP_DigestFinal_ex(context, digest->data(), &digest_length);
    EVP_MD_CTX_destroy(context);

    digest->pos(0);
    return digest;
}

ptr<buffer> create_hash(ptr<log_store> store_) {
    return create_hash(store_->last_app_log_entry(), store_->last_app_log_idx());
}

/**
 * @param height the height of the latest log entry
 */
// bool check_hash(ptr<log_entry> appended, ptr<log_entry> latest, ulong height) {
//     ptr<buffer> pointer = appended->get_prev_ptr();
//     if (pointer == nullptr) {
//         return false;
//     }
//     auto digest = create_hash(latest, height);
//     if (digest->size() != pointer->size()) {
//         return false;
//     }
//     for (size_t i = 0; i < digest->size(); i++) {
//         if (digest->get_byte() != pointer->get_byte()) {
//             pointer->pos(0);
//             return false;
//         }
//     }
//     pointer->pos(0);
//     return true;
// }

bool check_hash(std::vector<ptr<log_entry>>& entries,
                ptr<buffer>& base_hash,
                ptr<buffer> target_hash,
                ulong starting_idx,
                std::map<ulong, ptr<buffer>>& hash_map_to_update) {
    ptr<buffer> curr_hash = base_hash;
    for (size_t i = 0; i < entries.size(); i++) {
        // TODO: iteratively hash the entries to the last one. Need to modify the
        // log_entry and raft_server:
        //   1. do not include prv_ptr in the log_entry structure. Only include the last
        //   hash pointer in the req msg.
        //   2. each node stores only the hash pointers of the latest entry and also the
        //   latest comitted entry. intermediate ckpt

        if (entries[i]->get_val_type() != log_val_type::app_log) {
            hash_map_to_update[starting_idx + i] = curr_hash;
            continue;
        }
        curr_hash = create_hash(entries[i], curr_hash, starting_idx + i);
        hash_map_to_update[starting_idx + i] = curr_hash;
    }

    if (curr_hash->size() != target_hash->size()) {
        base_hash = nullptr;
        return false;
    }
    curr_hash->pos(0);
    target_hash->pos(0);
    for (size_t j = 0; j < curr_hash->size(); j++) {
        if (curr_hash->get_byte() != target_hash->get_byte()) {
            base_hash = curr_hash;
            curr_hash->pos(0);
            target_hash->pos(0);
            return false;
        }
    }
    curr_hash->pos(0);
    target_hash->pos(0);
    return true;
}

// bool check_hash(ptr<log_entry> appended, ptr<log_store> store_, ulong pos) {
//     if (pos == (ulong)-1) {
//         return check_hash(appended, store_->last_entry(), store_->next_slot() - 1);
//     } else {
//         return check_hash(appended, store_->entry_at(pos), pos);
//     }
// }

ptr<buffer> create_hash(ptr<log_entry> new_entry, ptr<buffer> curr_ptr, ulong idx) {
    ptr<buffer> serial = new_entry->serialize_sig();
    size_t msgsize;
    if (serial == nullptr) {
        msgsize = sizeof(ulong);
    } else {
        msgsize = serial->size() + sizeof(ulong);
    }
    if (curr_ptr != nullptr) msgsize += curr_ptr->size();
    ptr<buffer> msg = buffer::alloc(msgsize);
    msg->put(idx);
    if (serial != nullptr) {
        msg->put(*serial);
    } else {
        msg->put((byte)0);
    }
    if (curr_ptr != nullptr) curr_ptr->pos(0);
    if (curr_ptr != nullptr) msg->put(*curr_ptr);
    msg->pos(0);

    uint32_t digest_length = SHA256_DIGEST_LENGTH;
    ptr<buffer> digest = buffer::alloc(digest_length);
    EVP_MD_CTX* context = EVP_MD_CTX_new();
    EVP_DigestInit_ex(context, HASH_FN(), nullptr);
    EVP_DigestUpdate(context, msg->data(), msg->size());
    EVP_DigestFinal_ex(context, digest->data(), &digest_length);
    EVP_MD_CTX_destroy(context);

    digest->pos(0);
    return digest;
}
} // namespace nuraft
