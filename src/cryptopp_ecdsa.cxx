
#include "cryptopp_ecdsa.hxx"

namespace nuraft {

ptr<buffer> create_hash(ptr<log_entry> le_, ulong height) {
    ptr<buffer> serial = le_->serialize_sig();
    size_t msgsize = serial->size() + sizeof(ulong);
    ptr<buffer> msg = buffer::alloc(msgsize);
    msg->put(height);
    msg->put(*serial);
    CryptoPP::Keccak_256 hash;
    hash.Update(msg->data_begin(), msgsize);
    ptr<buffer> digest = buffer::alloc(hash.DigestSize());
    hash.Final((CryptoPP::byte*)digest->data_begin());
    return digest;
}

ptr<buffer> create_hash(ptr<log_store> store_) {
    return create_hash(store_->last_app_log_entry(), store_->last_app_log_idx());
}

/**
 * @param height the height of the latest log entry
 */
bool check_hash(ptr<log_entry> appended, ptr<log_entry> latest, ulong height) {
    ptr<buffer> pointer = appended->get_prev_ptr();
    if (pointer == nullptr) {
        return false;
    }
    auto digest = create_hash(latest, height);
    if (digest->size() != pointer->size()) {
        return false;
    }
    for (size_t i = 0; i < digest->size(); i++) {
        if (digest->get_byte() != pointer->get_byte()) {
            pointer->pos(0);
            return false;
        }
    }
    pointer->pos(0);
    return true;
}

bool check_hash(ptr<log_entry> appended, ptr<log_store> store_, ulong pos) {
    if (pos == (ulong)-1) {
        return check_hash(appended, store_->last_entry(), store_->next_slot() - 1);
    } else {
        return check_hash(appended, store_->entry_at(pos), pos);
    }
}

template <class T> static ptr<buffer> serialize_key(T& key) {
    CryptoPP::ByteQueue queue;
    key.Save(queue);
    ptr<buffer> buf = buffer::alloc(queue.MaxRetrievable());
    CryptoPP::ArraySink sink((CryptoPP::byte*)buf->data_begin(), buf->size());
    queue.TransferTo(sink);
    sink.MessageEnd();
    return buf;
}

ptr<buffer> load_key_from_file(const std::string& filename) {
    CryptoPP::FileSource file(filename.c_str(), true);
    CryptoPP::ByteQueue q;
    CryptoPP::Base64Decoder dec;
    file.TransferTo(dec);
    dec.MessageEnd();
    dec.CopyTo(q);
    q.MessageEnd();
    ptr<buffer> buf = buffer::alloc(q.MaxRetrievable());
    CryptoPP::ArraySink sink((CryptoPP::byte*)buf->data_begin(), buf->size());
    q.TransferTo(sink);
    sink.MessageEnd();
    return buf;
}

ptr<buffer> generate_random_seckey() {
    CryptoPP::AutoSeededRandomPool prng;
    ECDSA_t::PrivateKey seckey_obj;
    seckey_obj.Initialize(prng, CryptoPP::ASN1::secp256k1());
    return serialize_key(seckey_obj);
}

ptr<buffer> derive_pubkey(ptr<buffer> seckey) {
    CryptoPP::AutoSeededRandomPool prng;
    CryptoPP::ArraySource source((CryptoPP::byte*)seckey->data_begin(), seckey->size(), true);
    ECDSA_t::PrivateKey seckey_obj;
    ECDSA_t::PublicKey pubkey_obj;
    seckey_obj.Initialize(prng, CryptoPP::ASN1::secp256k1());
    seckey_obj.Load(source);
    seckey_obj.MakePublicKey(pubkey_obj);
    return serialize_key(pubkey_obj);
}

void save_key_to_file(const std::string& filename, const buffer& key) {
    CryptoPP::FileSink file(filename.c_str());
    CryptoPP::ArraySource source((const CryptoPP::byte*)key.data(), key.size(), true);
    CryptoPP::Base64Encoder encoder;
    source.CopyTo(encoder);
    encoder.MessageEnd();
    encoder.TransferTo(file);
    file.MessageEnd();
}

std::string stringify_key(const buffer& key) {
    std::string keystr;
    CryptoPP::ArraySource source((const CryptoPP::byte*)key.data(), key.size(), true);
    CryptoPP::StringSink sink(keystr);
    CryptoPP::Base64Encoder encoder;
    source.CopyTo(encoder);
    encoder.MessageEnd();
    encoder.TransferTo(sink);
    sink.MessageEnd();
    return keystr;
}

ptr<buffer> sign_message(const buffer& msg, const buffer& seckey) {
    CryptoPP::AutoSeededRandomPool prng;
    CryptoPP::ArraySource source((const CryptoPP::byte*)seckey.data(), seckey.size(), true);
    ECDSA_t::PrivateKey key;
    key.Initialize(prng, CryptoPP::ASN1::secp256k1());
    key.Load(source);
    ECDSA_t::Signer signer(key);

    size_t siglen = signer.MaxSignatureLength();
    ptr<buffer> tempbuf = buffer::alloc(siglen);
    siglen = signer.SignMessage(prng, (const CryptoPP::byte*)msg.data(), msg.size(), (CryptoPP::byte*)tempbuf->data());
    ptr<buffer> buf = buffer::alloc(siglen);
    tempbuf->pos(0);
    tempbuf->get(buf);
    return buf;
}

bool verify_signature_util(const buffer& msg, const buffer& sig, const buffer& pubkey) {
    CryptoPP::ArraySource source((const CryptoPP::byte*)pubkey.data(), pubkey.size(), true);
    ECDSA_t::PublicKey key;
    key.Load(source);
    ECDSA_t::Verifier verifier(key);

    return verifier.VerifyMessage(
        (const CryptoPP::byte*)msg.data(), msg.size(), (const CryptoPP::byte*)sig.data(), sig.size());
}

} // namespace nuraft
