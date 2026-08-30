#include "Password.h"

#include <sodium.h>

#include <vector>

namespace password {

bool init()
{
    // sodium_init() is not thread-safe, so it must run before the event loop
    // starts. Returns 1 if another caller already initialised it.
    return ::sodium_init() >= 0;
}

std::string hash(const std::string &plaintext)
{
    // crypto_pwhash_str writes an ASCII string that already contains the
    // algorithm id, the cost parameters and a fresh random salt.
    std::vector<char> out(crypto_pwhash_STRBYTES);

    // INTERACTIVE limits: ~64 MiB of memory and ~0.1s per hash on typical
    // hardware. The memory cost is the point: it makes GPU cracking expensive.
    if (::crypto_pwhash_str(out.data(),
                            plaintext.c_str(),
                            plaintext.size(),
                            crypto_pwhash_OPSLIMIT_INTERACTIVE,
                            crypto_pwhash_MEMLIMIT_INTERACTIVE) != 0)
    {
        return "";  // out of memory
    }
    return std::string(out.data());
}

bool verify(const std::string &plaintext, const std::string &storedHash)
{
    if (storedHash.empty()) return false;

    // Re-derives the hash using the parameters embedded in storedHash and
    // compares in constant time, so timing cannot leak how much matched.
    return ::crypto_pwhash_str_verify(storedHash.c_str(),
                                      plaintext.c_str(),
                                      plaintext.size()) == 0;
}

}  // namespace password
