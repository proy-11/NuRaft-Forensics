#ifndef _ECDSA_HXX
#define _ECDSA_HXX

#include <boost/filesystem.hpp>
#include <cryptopp/base64.h>
#include <cryptopp/eccrypto.h>
#include <cryptopp/files.h>
#include <cryptopp/hex.h>
#include <cryptopp/keccak.h>
#include <cryptopp/oids.h>
#include <cryptopp/osrng.h>

#include "buffer.hxx"
#include "log_entry.hxx"
#include "log_store.hxx"

typedef CryptoPP::ECDSA<CryptoPP::ECP, CryptoPP::Keccak_256> ECDSA_t;

namespace nuraft {

ptr<buffer> create_hash(ptr<log_entry> le_, ulong height);

ptr<buffer> create_hash(ptr<log_store> store_);

/**
 * @param height the height of the latest log entry
 */
bool check_hash(ptr<log_entry> appended, ptr<log_entry> latest, ulong height);

bool check_hash(ptr<log_entry> appended, ptr<log_store> store_, ulong pos = -1);

ptr<buffer> load_key_from_file(const std::string& filename);

ptr<buffer> generate_random_seckey();

ptr<buffer> derive_pubkey(ptr<buffer> seckey);

void save_key_to_file(const std::string& filename, const buffer& key);

std::string stringify_key(const buffer& key);

ptr<buffer> sign_message(const buffer& msg, const buffer& seckey);

bool verify_signature_util(const buffer& msg, const buffer& sig, const buffer& pubkey);

} // namespace nuraft
#endif