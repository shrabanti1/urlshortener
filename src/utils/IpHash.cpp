#include "IpHash.h"

#include <openssl/hmac.h>

#include <array>
#include <cstdio>

#include "Config.h"

namespace {
constexpr size_t kMinSecretLength = 16;
constexpr size_t kHexChars = 16;  // 64 bits: ample for counting, not for lookup
}  // namespace

namespace iphash {

bool validateSecretAtStartup(std::string &problemOut)
{
    const std::string s = config::get("IP_HASH_SECRET", "");
    if (s.empty())
    {
        problemOut = "IP_HASH_SECRET is not set";
        return false;
    }
    if (s.size() < kMinSecretLength)
    {
        problemOut = "IP_HASH_SECRET must be at least 16 characters";
        return false;
    }
    return true;
}

std::string anonymize(const std::string &ip)
{
    if (ip.empty()) return "";

    const std::string secret = config::get("IP_HASH_SECRET", "");

    std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
    unsigned int digestLen = 0;

    ::HMAC(::EVP_sha256(),
           secret.data(),
           static_cast<int>(secret.size()),
           reinterpret_cast<const unsigned char *>(ip.data()),
           ip.size(),
           digest.data(),
           &digestLen);

    std::string hex;
    hex.reserve(kHexChars);
    for (size_t i = 0; i < kHexChars / 2 && i < digestLen; ++i)
    {
        char buf[3];
        std::snprintf(buf, sizeof(buf), "%02x", digest[i]);
        hex += buf;
    }
    return hex;
}

}  // namespace iphash
