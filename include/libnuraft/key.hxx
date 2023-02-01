// FMARK:

#ifndef _KEY_HXX
#define _KEY_HXX

#include <exception>
#include <string>

#include "buffer.hxx"

namespace nuraft {
class crypto_exception : public std::exception {
public:
    crypto_exception()
        : msg(NULL) {}
    crypto_exception(const char* msg_)
        : msg(msg_) {}
    virtual const char* what() const throw() {
        if (msg == NULL)
            return "default crypto exception";
        else
            return msg;
    }

private:
    const char* msg;
};

class pubkey_intf {
public:
    virtual ~pubkey_intf() {}
    virtual ptr<buffer> tobuf() = 0;
    virtual std::string str() = 0;
    virtual bool verify_md(const buffer& msg, const buffer& sig) = 0;
};

class seckey_intf {
public:
    virtual ~seckey_intf() {}
    virtual ptr<buffer> tobuf() = 0;
    virtual std::string str() = 0;
    virtual void tofile(const std::string& filename) = 0;
    virtual ptr<pubkey_intf> derive() = 0;
    virtual ptr<buffer> sign_md(const buffer& msg) = 0;
};

// template <class TClass, class TIntf> class key_factory {
// public:
//     static ptr<TIntf> from_buffer(const buffer& keybuf) { return TClass::frombuf(keybuf); };
//     static ptr<TIntf> from_file(const std::string& filename) { return TClass::fromfile(filename); };
//     static ptr<TIntf> generate_random() { return TClass::generate(); };
// };
} // namespace nuraft
#endif