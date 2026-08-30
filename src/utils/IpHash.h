#pragma once
#include <string>

namespace iphash {

// HMAC-SHA256(secret, ip), truncated to 16 hex characters.
//
// Storing raw IPs makes the table personal data under GDPR and similar laws.
// A plain SHA-256 would NOT be enough: the IPv4 space is only 2^32, so an
// attacker could hash every address and reverse the whole column in minutes.
// Keying it with a server-side secret makes that infeasible without the key.
std::string anonymize(const std::string &ip);

// Fails fast if IP_HASH_SECRET is unusable.
bool validateSecretAtStartup(std::string &problemOut);

}  // namespace iphash
