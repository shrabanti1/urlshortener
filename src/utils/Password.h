#pragma once
#include <string>

namespace password {

// Hashes with Argon2id (libsodium). The returned string embeds the algorithm,
// parameters and a per-user random salt, so nothing else needs storing.
// Returns an empty string if hashing fails (out of memory).
std::string hash(const std::string &plaintext);

// Constant-time verification. Safe to call with any stored string.
bool verify(const std::string &plaintext, const std::string &storedHash);

// Must be called once at startup, before any hash()/verify().
bool init();

}  // namespace password
