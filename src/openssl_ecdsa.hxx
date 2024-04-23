// +build linux

#ifndef _ECDSA_SSL_HXX
#define _ECDSA_SSL_HXX

#include <boost/filesystem.hpp>
#include <chrono>
#include <openssl/bio.h>
#include <openssl/ec.h>
#include <openssl/evp.h>
#include <openssl/obj_mac.h>
#include <openssl/pem.h>
#include <openssl/sha.h>
#include <openssl/err.h>

#include "key.hxx"
#include "log_entry.hxx"
#include "log_store.hxx"

using std::chrono::duration;
using std::chrono::duration_cast;
using std::chrono::high_resolution_clock;
using std::chrono::microseconds;

namespace nuraft {
EVP_PKEY_CTX* new_evp_pkey_ctx();

class pubkey_t : public pubkey_intf {
public:
    pubkey_t(EVP_PKEY* key_);
    pubkey_t(const buffer& keybuf);
    virtual ~pubkey_t();

    virtual ptr<buffer> tobuf();
    virtual std::string str();
    virtual bool verify_md(const buffer& msg, const buffer& sig);

    static ptr<pubkey_t> frombuf(const buffer& keybuf);

private:
    EVP_PKEY* key;
};

class seckey_t : public seckey_intf {
public:
    seckey_t();
    seckey_t(const buffer& keybuf);
    // seckey_t(const std::string& filename);
    seckey_t(std::string priv_key);
    virtual ~seckey_t();

    virtual ptr<buffer> tobuf();
    virtual std::string str();
    virtual void tofile(const std::string& filename);
    virtual ptr<pubkey_intf> derive();
    virtual ptr<buffer> sign_md(const buffer& msg);

    static ptr<seckey_t> generate();
    static ptr<seckey_t> frombuf(const buffer& keybuf);
    static ptr<seckey_t> fromfile(const std::string& filename);

public:
    EVP_PKEY* key;
};

std::string tobase64(const buffer& key);

ptr<buffer> create_hash(const char* source);

ptr<buffer> create_hash(ptr<log_entry> le_, ulong height);

ptr<buffer> create_hash(ptr<log_store> store_);

// bool check_hash(ptr<log_entry> appended, ptr<log_entry> latest, ulong height);

bool check_hash(std::vector<ptr<log_entry>>& entries, ptr<buffer>& base_hash, ptr<buffer> target_hash, ulong starting_idx); 

bool check_hash(ptr<log_entry> appended, ptr<log_store> store_, ulong pos);

ptr<buffer> create_hash(ptr<log_entry> new_entry, ptr<buffer> curr_ptr, ulong idx);
} // namespace nuraft
#endif