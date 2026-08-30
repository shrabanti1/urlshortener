#include "Jwt.h"

#include <algorithm>

#include <drogon/drogon.h>
#include <jwt-cpp/jwt.h>

#include "Config.h"

namespace {

constexpr size_t kMinSecretLength = 32;
constexpr const char *kIssuer = "url-shortener";

std::string secret()
{
    return config::get("JWT_SECRET", "");
}

int expiryMinutes()
{
    return config::getInt("JWT_EXPIRY_MINUTES", 60);
}

}  // namespace

namespace jwt_util {

bool validateSecretAtStartup(std::string &problemOut)
{
    const std::string s = secret();
    if (s.empty())
    {
        problemOut = "JWT_SECRET is not set";
        return false;
    }
    if (s.size() < kMinSecretLength)
    {
        problemOut = "JWT_SECRET must be at least 32 characters";
        return false;
    }
    // Catch placeholders even when padded to the length requirement.
    static const char *kPlaceholders[] = {
        "changeme", "secret", "password", "your-secret", "xxxxxxxx", "00000000"};
    std::string lower = s;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    for (const char *ph : kPlaceholders)
    {
        if (lower.rfind(ph, 0) == 0)
        {
            problemOut = std::string("JWT_SECRET starts with the placeholder '") + ph + "'";
            return false;
        }
    }

    // A secret made of one repeated character has almost no entropy.
    if (lower.find_first_not_of(lower.substr(0, 1)) == std::string::npos)
    {
        problemOut = "JWT_SECRET is a single repeated character";
        return false;
    }
    return true;
}

std::string issueAccessToken(long long userId, const std::string &email)
{
    const auto now = std::chrono::system_clock::now();

    return jwt::create()
        .set_issuer(kIssuer)
        .set_type("JWT")
        .set_issued_at(now)
        .set_expires_at(now + std::chrono::minutes(expiryMinutes()))
        // "sub" (subject) is the standard claim for who the token is about.
        .set_subject(std::to_string(userId))
        .set_payload_claim("email", jwt::claim(email))
        .sign(jwt::algorithm::hs256{secret()});
}

std::optional<Claims> verifyAccessToken(const std::string &token)
{
    try
    {
        const auto decoded = jwt::decode(token);

        // Pinning the algorithm here is what prevents the "alg confusion"
        // attack, where an attacker re-signs a token as alg:none or swaps
        // RS256 for HS256 and signs with the public key.
        jwt::verify()
            .allow_algorithm(jwt::algorithm::hs256{secret()})
            .with_issuer(kIssuer)
            .verify(decoded);  // also checks "exp"

        Claims c;
        c.userId = std::stoll(decoded.get_subject());
        if (decoded.has_payload_claim("email"))
            c.email = decoded.get_payload_claim("email").as_string();
        return c;
    }
    catch (const std::exception &e)
    {
        // Deliberately vague: never tell a caller whether the signature was
        // wrong, the token expired, or it was malformed.
        LOG_DEBUG << "jwt rejected: " << e.what();
        return std::nullopt;
    }
}

}  // namespace jwt_util
